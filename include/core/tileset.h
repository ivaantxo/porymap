#pragma once
#ifndef TILESET_H
#define TILESET_H

#include "metatile.h"
#include "tile.h"
#include <QImage>
#include <QHash>

struct MetatileLabelPair {
    QString owned;
    QString shared;
};

class Tileset
{
public:
    Tileset() = default;
    Tileset(const Tileset &other);
    Tileset &operator=(const Tileset &other);
    ~Tileset();

public:
    QString name;
    bool is_secondary;
    QString tiles_label;
    QString palettes_label;
    QString metatiles_label;
    QString metatiles_path;
    QString metatile_attrs_label;
    QString metatile_attrs_path;
    QString tilesImagePath;
    QStringList palettePaths;

    // Palette pool (albor, one tileset per layout): the tileset keeps the palettes of
    // all its maps, as many as MAX_PALS_IN_TILESET, and each map loads only the ones
    // its metatiles use, up to NUM_PALS_IN_PRIMARY at a time. The palettes are every
    // palettes/NN.pal in palettesFolder (graphics.h INCBINs them together, from
    // palettes.gbapal), and each metatile tile's palette is a byte in metatile_palettes.bin.
    QString metatile_palettes_label;
    QString metatile_palettes_path;
    QString palettesFolder;
    bool usesPalettePool() const { return !this->palettesFolder.isEmpty() && !this->metatile_palettes_path.isEmpty(); }

    // Tile animations (albor): animations.bin, from the tileset's `animations` member. Kept
    // as the raw file; the mapeado library (src/lib/mapeado) reads and writes it.
    QString animations_label;
    QString animations_path;
    QByteArray animationsData;
    bool loadAnimations();
    bool saveAnimations();
    // How many palettes it has, and can have. Without a pool, the project's fixed number.
    int numPalettes() const;
    int paletteLimit() const;
    static QString palettePathInFolder(const QString &folder, int paletteId);
    // How many palettes a tile can choose from with these tilesets.
    static int numBlockPalettes(const Tileset *primaryTileset);

    QHash<int, QString> metatileLabels;
    QList<QList<QRgb>> palettes;
    QList<QList<QRgb>> palettePreviews;

    static QString stripPrefix(const QString &fullName);
    static Tileset* getPaletteTileset(int, Tileset*, Tileset*);
    static const Tileset* getPaletteTileset(int, const Tileset*, const Tileset*);
    static Tileset* getMetatileTileset(int, Tileset*, Tileset*);
    static const Tileset* getMetatileTileset(int, const Tileset*, const Tileset*);
    static Tileset* getTileTileset(int, Tileset*, Tileset*);
    static const Tileset* getTileTileset(int, const Tileset*, const Tileset*);
    static Metatile* getMetatile(int, Tileset*, Tileset*);
    static const Metatile* getMetatile(int, const Tileset*, const Tileset*);
    static Tileset* getMetatileLabelTileset(int, Tileset*, Tileset*);
    static QString getMetatileLabel(int, Tileset *, Tileset *);
    static QString getOwnedMetatileLabel(int, Tileset *, Tileset *);
    static MetatileLabelPair getMetatileLabelPair(int metatileId, Tileset *primaryTileset, Tileset *secondaryTileset);
    static bool setMetatileLabel(int, QString, Tileset *, Tileset *);
    QString getMetatileLabelPrefix();
    static QString getMetatileLabelPrefix(const QString &name);
    static QList<QList<QRgb>> getBlockPalettes(const Tileset*, const Tileset*, bool useTruePalettes = false);
    static QList<QRgb> getPalette(int, const Tileset*, const Tileset*, bool useTruePalettes = false);
    static bool metatileIsValid(uint16_t metatileId, const Tileset*, const Tileset*);
    static QHash<int, QString> getHeaderMemberMap(bool usingAsm);
    static QString getExpectedDir(QString tilesetName, bool isSecondary);
    QString getExpectedDir();

    bool load();
    bool loadMetatiles();
    bool loadMetatileAttributes();
    bool loadTilesImage(QImage *importedImage = nullptr);
    bool loadPalettes();

    bool save();
    bool saveMetatileAttributes();
    bool saveMetatiles();
    bool saveTilesImage();
    bool savePalettes();

    bool appendToHeaders(const QString &filepath, const QString &friendlyName, bool usingAsm);
    bool appendToGraphics(const QString &filepath, const QString &friendlyName, bool usingAsm);
    bool appendToMetatiles(const QString &filepath, const QString &friendlyName, bool usingAsm);

    void setTilesImage(const QImage &image);
    const QImage &tilesImage() const { return m_tilesImage; }

    // Set when porymap edits the tileset in place outside the Tileset Editor
    // (stamping pieces onto a map), so that it gets saved along with the layouts.
    bool hasUnsavedChanges() const { return m_hasUnsavedChanges; }
    void setHasUnsavedChanges(bool hasUnsavedChanges) { m_hasUnsavedChanges = hasUnsavedChanges; }

    void setMetatiles(const QList<Metatile*> &metatiles);
    void addMetatile(Metatile* metatile);

    const QList<Metatile*> &metatiles() const { return m_metatiles; }
    const Metatile* metatileAt(unsigned int i) const { return m_metatiles.at(i); }

    void clearMetatiles();
    void resizeMetatiles(int newNumMetatiles);
    int numMetatiles() const { return m_metatiles.length(); }
    int maxMetatiles() const;

    uint16_t firstMetatileId() const;
    uint16_t lastMetatileId() const;
    bool containsMetatileId(uint16_t metatileId) const { return metatileId >= firstMetatileId() && metatileId <= lastMetatileId(); }

    uint16_t firstTileId() const;
    uint16_t lastTileId() const;
    bool containsTileId(uint16_t tileId) const { return tileId >= firstTileId() && tileId <= lastTileId(); }

    int numTiles() const { return m_tiles.length(); }
    int maxTiles() const;

    QImage tileImage(uint16_t tileId) const { return m_tiles.value(Tile::getIndexInTileset(tileId)); }

    QSet<int> getUnusedColorIds(int paletteId, const Tileset *pairedTileset, const QSet<int> &searchColors = {}) const;
    QList<uint16_t> findMetatilesUsingColor(int paletteId, int colorId, const Tileset *pairedTileset) const;

    static constexpr int maxPalettes() { return 16; }
    static constexpr int numColorsPerPalette() { return 16; }

private:
    QList<Metatile*> m_metatiles;

    QList<QImage> m_tiles;
    QImage m_tilesImage;
    bool m_hasUnsavedTilesImage = false;
    bool m_hasUnsavedChanges = false;
};

#endif // TILESET_H
