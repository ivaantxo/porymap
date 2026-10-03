#include "stamping.h"
#include "project.h"
#include "config.h"
#include "log.h"
#include "prefab.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

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
    return qMin(tileset->palettes.length(), Project::getNumPalettesPrimary());
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

mapeado::Formato format() {
    mapeado::Formato f;
    f.maxTiles = Project::getNumTilesPrimary();
    f.maxMetatiles = Project::getNumMetatilesPrimary();
    f.maxPaletas = Project::getNumPalettesPrimary();
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
    return piece;
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
        for (int i = 0; i < kTilesPerMetatile && i < metatile->tiles.length(); i++)
            entries[i] = metatile->tiles.at(i).rawValue();
        data.metatiles.push_back(entries);
        data.atributos.push_back(static_cast<uint16_t>(metatile->getAttributes()));
    }
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
        for (int i = 0; i < kTilesPerMetatile; i++)
            tiles.append(Tile(data.metatiles[m][i]));
        if (metatile->tiles != tiles) {
            metatile->tiles = tiles;
            changed = true;
        }
        if (metatile->getAttributes() != data.atributos[m]) {
            metatile->setAttributes(data.atributos[m]);
            changed = true;
        }
    }

    if (changed)
        tileset->setHasUnsavedChanges(true);
    return changed;
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
    u.tiles = tileUsed.count(true);
    for (const QSet<int> &colors : colorsUsed) {
        if (!colors.isEmpty()) u.palettes++;
        u.colors += colors.size();
    }
    return u;
}

} // namespace Stamping
