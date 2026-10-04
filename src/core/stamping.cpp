#include "stamping.h"
#include "project.h"
#include "config.h"
#include "log.h"
#include "prefab.h"
#include "maplayout.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <algorithm>

namespace Stamping {

namespace {

const int kTilesPerMetatile = 12; // The library only knows triple-layer metatiles.

QRgb toQRgb(mapeado::Color color) {
    int r, g, b;
    mapeado::ARgb(color, &r, &g, &b);
    return qRgb(r, g, b);
}

mapeado::Color fromQRgb(QRgb color) {
    return mapeado::DeRgb(qRed(color), qGreen(color), qBlue(color));
}

int numPalettes(const Tileset *tileset) {
    return qMin(tileset->palettes.length(), tileset->paletteLimit());
}

std::vector<uint8_t> toBytes(const QByteArray &data) {
    return std::vector<uint8_t>(data.constBegin(), data.constEnd());
}

QList<QRgb> newPalette(const mapeado::Paleta &palette) {
    QList<QRgb> colors;
    for (int i = 0; i < Tileset::numColorsPerPalette(); i++)
        colors.append(palette[i] == mapeado::TRANSPARENTE ? qRgb(248, 0, 248) : toQRgb(palette[i]));
    return colors;
}

} // namespace

QString unsupportedReason() {
    if (projectConfig.getNumTilesInMetatile() != kTilesPerMetatile)
        return QStringLiteral("Estampar piezas necesita metatiles de triple capa.");
    uint16_t idMask = projectConfig.blockMetatileIdMask;
    if (!idMask || (idMask & 1) == 0 || (idMask & (idMask + 1)) != 0)
        return QStringLiteral("Estampar piezas necesita que el ID del metatile ocupe los bits bajos del bloque.");
    return QString();
}

QString limitedReason(const Tileset *tileset) {
    if (!tileset)
        return QString();
    if (!Project::usingSingleTileset())
        return QString("Este proyecto usa dos tilesets por layout (en include/fieldmap.h no está "
                       "<tt>#define NUM_TILESETS_PER_LAYOUT 1</tt>): las piezas solo pueden usar las %1 paletas "
                       "del tileset primario, sin paletas por mapa. ¿Está abierta la versión de albor con un solo tileset?")
            .arg(tileset->paletteLimit());
    if (!tileset->usesPalettePool())
        return QString("El tileset %1 no tiene paletas por mapa (le falta <tt>.metatilePalettes</tt> en headers.h "
                       "o su carpeta palettes): las piezas solo pueden usar sus %2 paletas.")
            .arg(tileset->name.toHtmlEscaped()).arg(tileset->paletteLimit());
    return QString();
}

mapeado::Formato format(const Tileset *tileset) {
    mapeado::Formato f;
    f.maxTiles = Project::getNumTilesPrimary();
    f.maxMetatiles = Project::getNumMetatilesPrimary();
    f.maxPaletas = Project::getNumPalettesPrimary();
    f.maxPaletasTileset = tileset ? tileset->paletteLimit() : Project::getNumPalettesPrimary();
    f.mascaraId = projectConfig.blockMetatileIdMask;
    // The library keeps these bits of each block as they are: collision, and elevation too.
    f.mascaraColision = ~projectConfig.blockMetatileIdMask;
    return f;
}

mapeado::Imagen pieceFromImage(const QImage &image) {
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    const mapeado::Color magenta = mapeado::DeRgb(248, 0, 248);
    mapeado::Imagen piece(argb.width(), argb.height());
    for (int y = 0; y < argb.height(); y++) {
        const QRgb *line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); x++) {
            mapeado::Color c = fromQRgb(line[x]);
            piece.en(x, y) = (qAlpha(line[x]) < 128 || c == magenta) ? mapeado::TRANSPARENTE : c;
        }
    }
    // With a palette, its indices and colors too, 16 at a time: if each 8x8 tile uses one
    // row, the piece brings its own palettes (mapeado::PiezaConPaletas).
    const QVector<QRgb> table = image.colorTable();
    if (image.format() == QImage::Format_Indexed8 && !table.isEmpty()) {
        piece.indices.resize(piece.px.size());
        for (int y = 0; y < image.height(); y++) {
            const uchar *line = image.constScanLine(y);
            for (int x = 0; x < image.width(); x++)
                piece.indices[y * piece.ancho + x] = line[x];
        }
        mapeado::Paleta empty;
        empty.fill(0);
        piece.filas.assign((table.size() + 15) / 16, empty);
        for (int i = 0; i < table.size(); i++)
            piece.filas[i / 16][i % 16] = fromQRgb(table.at(i));
    }
    return piece;
}

QImage displayImage(const QImage &image) {
    QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    const mapeado::Imagen piece = pieceFromImage(image);
    if (mapeado::PiezaConPaletas(piece, nullptr)) {
        // As in the game: color 0 of each row is transparent.
        for (int y = 0; y < argb.height(); y++)
            for (int x = 0; x < argb.width(); x++)
                if (piece.indices[y * piece.ancho + x] % 16 == 0)
                    argb.setPixel(x, y, qRgba(0, 0, 0, 0));
    }
    return argb;
}

mapeado::Tileset fromTileset(const Tileset *tileset) {
    mapeado::Tileset data;

    for (int i = 0; i < tileset->numTiles(); i++) {
        const QImage image = tileset->tileImage(i);
        mapeado::Tile tile;
        tile.fill(0);
        if (image.format() == QImage::Format_Indexed8) {
            for (int y = 0; y < Tile::pixelHeight() && y < image.height(); y++)
            for (int x = 0; x < Tile::pixelWidth() && x < image.width(); x++)
                tile[y * Tile::pixelWidth() + x] = image.pixelIndex(x, y) & 0xF;
        }
        data.tiles.push_back(tile);
    }

    for (int p = 0; p < numPalettes(tileset); p++) {
        mapeado::Paleta palette;
        palette.fill(0);
        const QList<QRgb> &colors = tileset->palettes.at(p);
        for (int i = 0; i < Tileset::numColorsPerPalette() && i < colors.length(); i++)
            palette[i] = fromQRgb(colors.at(i));
        data.paletas.push_back(palette);
    }

    for (const Metatile *metatile : tileset->metatiles()) {
        mapeado::Metatile entries;
        entries.fill(0);
        for (int i = 0; i < kTilesPerMetatile && i < metatile->tiles.length(); i++) {
            const Tile &tile = metatile->tiles.at(i);
            entries[i] = (tile.rawValue() & 0xFFF) | (static_cast<uint32_t>(tile.palette) << 12);
        }
        data.metatiles.push_back(entries);
        data.atributos.push_back(static_cast<uint16_t>(metatile->getAttributes()));
    }
    mapeado::AnimacionesDeBytes(toBytes(tileset->animationsData), data.animaciones);
    return data;
}

bool applyToTileset(const mapeado::Tileset &data, Tileset *tileset) {
    bool changed = false;

    // Tiles: rebuild the image only if some tile differs.
    const mapeado::Tileset before = fromTileset(tileset);
    if (before.tiles != data.tiles) {
        const QImage &oldImage = tileset->tilesImage();
        int tilesWide = qMax(1, oldImage.width() / Tile::pixelWidth());
        if (oldImage.isNull())
            tilesWide = 16;
        int tilesTall = qMax(1, (static_cast<int>(data.tiles.size()) + tilesWide - 1) / tilesWide);
        QImage image(tilesWide * Tile::pixelWidth(), tilesTall * Tile::pixelHeight(), QImage::Format_Indexed8);
        QVector<QRgb> colorTable = oldImage.colorTable();
        if (colorTable.length() < Tileset::numColorsPerPalette()) {
            colorTable.clear();
            for (int i = 0; i < Tileset::numColorsPerPalette(); i++)
                colorTable.append(qRgb(i * 16, i * 16, i * 16));
        }
        image.setColorTable(colorTable);
        image.fill(0);
        for (size_t t = 0; t < data.tiles.size(); t++) {
            int x0 = (t % tilesWide) * Tile::pixelWidth();
            int y0 = (t / tilesWide) * Tile::pixelHeight();
            for (int y = 0; y < Tile::pixelHeight(); y++)
            for (int x = 0; x < Tile::pixelWidth(); x++)
                image.setPixel(x0 + x, y0 + y, data.tiles[t][y * Tile::pixelWidth() + x]);
        }
        tileset->setTilesImage(image);
        changed = true;
    }

    // Palettes: only the colors that really changed, so the rest keep their exact values.
    // With a palette pool there may be new palettes at the end, or fewer after optimizing.
    if (tileset->usesPalettePool()) {
        const int count = qMax(1, static_cast<int>(data.paletas.size()));
        while (tileset->palettes.length() > count) {
            tileset->palettes.removeLast();
            tileset->palettePreviews.removeLast();
            changed = true;
        }
        for (int p = tileset->palettes.length(); p < static_cast<int>(data.paletas.size()); p++) {
            tileset->palettes.append(newPalette(data.paletas[p]));
            tileset->palettePreviews.append(newPalette(data.paletas[p]));
            changed = true;
        }
    }
    for (int p = 0; p < numPalettes(tileset) && p < static_cast<int>(data.paletas.size()); p++) {
        for (int i = 1; i < Tileset::numColorsPerPalette() && i < tileset->palettes[p].length(); i++) {
            mapeado::Color color = data.paletas[p][i];
            if (color == mapeado::TRANSPARENTE || color == fromQRgb(tileset->palettes[p][i]))
                continue;
            tileset->palettes[p][i] = toQRgb(color);
            if (p < tileset->palettePreviews.length() && i < tileset->palettePreviews[p].length())
                tileset->palettePreviews[p][i] = toQRgb(color);
            changed = true;
        }
    }

    // Metatiles: edited in place, so nothing holding on to one is left dangling.
    const int numMetatiles = static_cast<int>(qMin(data.metatiles.size(), data.atributos.size()));
    if (tileset->numMetatiles() != numMetatiles) {
        tileset->resizeMetatiles(numMetatiles);
        changed = true;
    }
    for (int m = 0; m < numMetatiles; m++) {
        Metatile *metatile = const_cast<Metatile*>(tileset->metatileAt(m));
        QList<Tile> tiles;
        for (int i = 0; i < kTilesPerMetatile; i++) {
            Tile tile(static_cast<uint16_t>(data.metatiles[m][i] & 0xFFF));
            tile.palette = mapeado::PaletaDeEntrada(data.metatiles[m][i]);
            tiles.append(tile);
        }
        if (metatile->tiles != tiles) {
            metatile->tiles = tiles;
            changed = true;
        }
        if (metatile->getAttributes() != data.atributos[m]) {
            metatile->setAttributes(data.atributos[m]);
            changed = true;
        }
    }

    // Animations: the file as the game reads it.
    if (!tileset->animations_path.isEmpty()) {
        const std::vector<uint8_t> bytes = mapeado::BytesDeAnimaciones(data.animaciones);
        const QByteArray animations(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
        const QByteArray before = tileset->animationsData.isEmpty() ? QByteArray(32, '\0') : tileset->animationsData;
        if (animations != before) {
            tileset->animationsData = animations;
            changed = true;
        }
    }

    if (changed)
        tileset->setHasUnsavedChanges(true);
    return changed;
}

std::vector<mapeado::Animacion> animations(const Tileset *tileset) {
    std::vector<mapeado::Animacion> result;
    if (tileset)
        mapeado::AnimacionesDeBytes(toBytes(tileset->animationsData), result);
    return result;
}

QList<QImage> animationFrames(const Tileset *tileset, const mapeado::Animacion &animation) {
    QList<QImage> frames;
    const QList<QRgb> palette = tileset->palettes.value(animation.paleta);
    for (const auto &frame : animation.fotogramas) {
        QImage image(animation.ancho * Tile::pixelWidth(), animation.alto * Tile::pixelHeight(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        for (int t = 0; t < static_cast<int>(frame.size()); t++)
        for (int i = 0; i < Tile::numPixels(); i++) {
            int index = frame[t][i];
            if (index)
                image.setPixel((t % animation.ancho) * Tile::pixelWidth() + i % Tile::pixelWidth(),
                               (t / animation.ancho) * Tile::pixelHeight() + i / Tile::pixelWidth(),
                               palette.value(index, qRgb(0, 0, 0)) | 0xFF000000);
        }
        frames.append(image);
    }
    return frames;
}

std::vector<mapeado::Imagen> framesFromFolder(const QString &folder, QString *error) {
    std::vector<mapeado::Imagen> frames;
    QMap<int, QString> files;
    for (const QString &name : QDir(folder).entryList({"*.png"}, QDir::Files)) {
        bool ok = false;
        int number = name.section('.', 0, 0).toInt(&ok);
        if (ok)
            files.insert(number, name);
    }
    for (const QString &name : files) {
        QImage image(QDir(folder).filePath(name));
        if (image.isNull()) {
            if (error) *error = QString("No se puede leer %1.").arg(name);
            return {};
        }
        frames.push_back(pieceFromImage(image));
    }
    if (frames.empty() && error)
        *error = QString("La carpeta no tiene fotogramas: 00.png, 01.png… (o 0.png, 1.png…).");
    return frames;
}

std::vector<int> fixedTiles(const Tileset *tileset) {
    std::vector<int> tiles;
    if (tileset->tilesImagePath.isEmpty())
        return tiles;
    QFile file(QFileInfo(tileset->tilesImagePath).dir().filePath("tiles_fijos.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return tiles;
    static const QRegularExpression range("^\\s*(\\d+)\\s*(?:-\\s*(\\d+))?\\s*$");
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().section('#', 0, 0);
        QRegularExpressionMatch match = range.match(line);
        if (!match.hasMatch())
            continue;
        int first = match.captured(1).toInt();
        int last = match.captured(2).isEmpty() ? first : match.captured(2).toInt();
        for (int t = first; t <= last; t++)
            tiles.push_back(t);
    }
    return tiles;
}

std::vector<mapeado::Fijado> pinnedMetatiles(const Tileset *tileset) {
    std::vector<mapeado::Fijado> pinned;
    QSet<int> seen;
    for (auto it = tileset->metatileLabels.constBegin(); it != tileset->metatileLabels.constEnd(); it++) {
        if (!it.value().isEmpty()) {
            pinned.push_back(mapeado::Fijado{it.value().toStdString(), it.key()});
            seen.insert(it.key());
        }
    }
    // Smart paths painted with metatiles go by their numbers: a prefab keeps all of its
    // metatiles, used on a map or not, in their place.
    const QMap<uint16_t, QString> prefabMetatiles = prefab.metatilesInTileset(tileset);
    for (auto it = prefabMetatiles.constBegin(); it != prefabMetatiles.constEnd(); it++) {
        int metatile = Metatile::getIndexInTileset(it.key());
        if (!seen.contains(metatile)) {
            pinned.push_back(mapeado::Fijado{QString("del prefab %1").arg(it.value()).toStdString(), metatile});
            seen.insert(metatile);
        }
    }
    return pinned;
}

std::vector<uint16_t> fromBlockdata(const Blockdata &blockdata) {
    std::vector<uint16_t> blocks;
    blocks.reserve(blockdata.size());
    for (const Block &block : blockdata)
        blocks.push_back(block.rawValue());
    return blocks;
}

Blockdata toBlockdata(const std::vector<uint16_t> &blocks) {
    Blockdata blockdata;
    blockdata.reserve(blocks.size());
    for (uint16_t raw : blocks)
        blockdata.append(Block(raw));
    return blockdata;
}

bool collectMaps(Project *project, const Tileset *tileset, TilesetMaps *out, QString *error) {
    out->layouts.clear();
    out->maps.clear();
    for (const QString &id : project->layoutIds()) {
        Layout *layout = project->getLayout(id);
        if (!layout || layout->tileset_primary_label != tileset->name)
            continue;
        if (!project->loadLayout(id)) {
            if (error) *error = QString("No se puede cargar el layout %1.").arg(id);
            return false;
        }
        mapeado::MapaDelTileset map;
        map.nombre = layout->name.toStdString();
        map.ancho = layout->getWidth();
        map.alto = layout->getHeight();
        map.bloques = fromBlockdata(layout->blockdata);
        map.borde = fromBlockdata(layout->border);
        out->layouts.append(layout);
        out->maps.push_back(map);
    }
    return true;
}

Usage usage(const Tileset *tileset) {
    Usage u;
    const int numTiles = tileset->numTiles();
    const int palettes = numPalettes(tileset);
    QVector<bool> tileUsed(numTiles, false);
    QVector<QSet<int>> colorsUsed(palettes);
    QVector<QSet<int>> tileColors(numTiles);
    for (int t = 0; t < numTiles; t++) {
        const QImage image = tileset->tileImage(t);
        if (image.format() != QImage::Format_Indexed8)
            continue;
        for (int y = 0; y < image.height(); y++)
        for (int x = 0; x < image.width(); x++) {
            int index = image.pixelIndex(x, y) & 0xF;
            if (index) tileColors[t].insert(index);
        }
    }
    if (numTiles) tileUsed[0] = true;
    for (const Metatile *metatile : tileset->metatiles()) {
        bool empty = metatile->getAttributes() == 0;
        for (const Tile &tile : metatile->tiles) {
            if (tile.rawValue() != 0) empty = false;
            if (tile.tileId >= numTiles)
                continue;
            tileUsed[tile.tileId] = true;
            if (tile.palette < palettes)
                colorsUsed[tile.palette] += tileColors[tile.tileId];
        }
        if (!empty) u.metatiles++;
    }
    // The tiles of the animations, and the colors of all their frames, are in use too.
    for (const mapeado::Animacion &animation : animations(tileset)) {
        for (int t = animation.tile; t < animation.tile + animation.ancho * animation.alto && t < numTiles; t++)
            tileUsed[t] = true;
        for (const auto &frame : animation.fotogramas)
            for (const mapeado::Tile &tile : frame)
                for (uint8_t index : tile)
                    if (index && animation.paleta < palettes)
                        colorsUsed[animation.paleta].insert(index);
    }
    u.tiles = tileUsed.count(true);
    for (const QSet<int> &colors : colorsUsed) {
        if (!colors.isEmpty()) u.palettes++;
        u.colors += colors.size();
    }
    return u;
}

QList<int> mapPalettes(const Layout *layout) {
    QSet<int> palettes;
    const Tileset *tileset = layout ? layout->tileset_primary : nullptr;
    if (!tileset)
        return {};
    QSet<int> seen;
    for (const Blockdata *blocks : {&layout->blockdata, &layout->border}) {
        for (const Block &block : *blocks) {
            const int metatileId = Metatile::getIndexInTileset(block.metatileId());
            if (seen.contains(metatileId) || metatileId >= tileset->numMetatiles())
                continue;
            seen.insert(metatileId);
            for (const Tile &tile : tileset->metatileAt(metatileId)->tiles)
                if (tile.tileId != 0)
                    palettes.insert(tile.palette);
        }
    }
    QList<int> sorted(palettes.constBegin(), palettes.constEnd());
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

} // namespace Stamping
