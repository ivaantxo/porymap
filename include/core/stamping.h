#pragma once
#ifndef STAMPING_H
#define STAMPING_H

#include "mapeado.h"
#include "blockdata.h"

#include <QImage>
#include <QList>
#include <QString>

class Project;
class Layout;
class Tileset;

// Glue between porymap and the mapeado library (src/lib/mapeado), which paints free art
// straight onto a map: a piece (any image whose sides are multiples of 8, with no fixed
// palette) is stamped onto one metatile layer, and the colors, tiles and metatiles it
// needs are added to the layout's primary tileset on the spot. If they don't fit,
// nothing is painted and the library says what's missing.
namespace Stamping {

// Empty if pieces can be stamped in this project; otherwise, why not.
QString unsupportedReason();

mapeado::Formato format();

// Alpha below 128, or the magenta 248,0,248, is transparent.
mapeado::Imagen pieceFromImage(const QImage &image);

mapeado::Tileset fromTileset(const Tileset *tileset);
// Writes back into `tileset` whatever differs. Returns whether anything changed.
bool applyToTileset(const mapeado::Tileset &data, Tileset *tileset);

// Tiles listed in tiles_fijos.txt next to tiles.png (numbers or a-b ranges, # comments),
// which are never reused (animations).
std::vector<int> fixedTiles(const Tileset *tileset);
// Metatiles with a label, which code may use even if no map does, and metatiles used by
// prefabs (smart paths go by metatile numbers).
std::vector<mapeado::Fijado> pinnedMetatiles(const Tileset *tileset);

std::vector<uint16_t> fromBlockdata(const Blockdata &blockdata);
Blockdata toBlockdata(const std::vector<uint16_t> &blocks);

// Every layout whose primary tileset is `tileset`, loaded, as the library sees them.
struct TilesetMaps {
    QList<Layout*> layouts;
    std::vector<mapeado::MapaDelTileset> maps;
    int indexOf(const Layout *layout) const { return layouts.indexOf(const_cast<Layout*>(layout)); }
};
bool collectMaps(Project *project, const Tileset *tileset, TilesetMaps *out, QString *error);

struct Usage {
    int tiles = 0;         // in use by some metatile, tile 0 included
    int palettes = 0;      // with some color in use
    int colors = 0;        // in use, over all palettes
    int metatiles = 0;     // not empty
};
Usage usage(const Tileset *tileset);

} // namespace Stamping

#endif // STAMPING_H
