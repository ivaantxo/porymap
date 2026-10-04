// Mapeado: arte libre por capas dentro del tileset, como porytiles pero pintando el mapa.
//
// Es la biblioteca. No lee ni escribe archivos: trabaja con lo que le pasan en memoria,
// para que la pueda usar igual la linea de comandos (main.cpp) que porymap.
//
// Cada mapa tiene tres capas de pixeles (baja, media, alta). Se pinta estampando piezas
// de arte libre (Estampar), y el tileset se va rellenando solo: colores, tiles de 8x8
// con volteos y metatiles. Optimizar lo reempaqueta cuando se llena.
//
// Por debajo las dos usan Compilar, que trocea el arte de los mapas en casillas de
// 16x16 y saca de ahi los metatiles, los tiles y las paletas, y reescribe el blockdata.
//
// Paletas: el tileset guarda las de todos sus mapas, y cada mapa carga solo las que
// usan sus metatiles (los de sus casillas y su borde), como mucho 15 a la vez. Que
// paletas carga un mapa no se guarda en ningun sitio: sale de sus metatiles cada vez,
// asi que no se arrastran de un mapa a otro. Al pintar, un trozo de 8x8 va primero a
// una paleta que el mapa ya carga.
//
// Animaciones: unos tiles del tileset que el juego cambia por los de cada fotograma. Al
// pintar, el arte igual a su fotograma 0 usa esos tiles y se anima solo (Animar).
//
// El comportamiento, el nivel y la colision NO salen del arte: se editan aparte, los
// dos primeros en los atributos del metatile y la colision en el bloque. Por eso cada
// compilacion parte de lo que habia antes y lo respeta:
//
//   - Una casilla cuyo arte no ha cambiado se queda con su metatile, sus atributos y su
//     colision. Si alguien pinto ahi un duplicado con otro comportamiento, se mantiene.
//   - Una casilla con arte nuevo hereda los atributos de otro metatile que ya tuviera
//     ese mismo arte, si lo hay, y si no se queda en comportamiento normal y nivel auto.
//     La colision siempre es la que tenia la casilla.
//   - Los metatiles y los tiles que siguen existiendo conservan su numero, para que el
//     blockdata y las referencias desde codigo no se muevan.
//   - Los metatiles fijados (con nombre, porque los usa el codigo aunque no esten en
//     ningun mapa) se conservan siempre en su numero, con su arte de antes.
#ifndef MAPEADO_H
#define MAPEADO_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mapeado {

// Color de GBA (BGR555), o TRANSPARENTE.
typedef uint16_t Color;
const Color TRANSPARENTE = 0x8000;

Color DeRgb(int r, int g, int b);
void ARgb(Color c, int *r, int *g, int *b);

enum Capa { CAPA_BAJA, CAPA_MEDIA, CAPA_ALTA, NUM_CAPAS };

typedef std::array<Color, 16> Paleta;        // la 0 es la transparente

struct Imagen {
    int ancho = 0, alto = 0;
    std::vector<Color> px;
    // Un PNG con paleta trae ademas el indice de cada pixel y los colores de su paleta,
    // de 16 en 16 (filas); si no, estan vacios. Ver PiezaConPaletas.
    std::vector<uint8_t> indices;
    std::vector<Paleta> filas;

    Imagen() {}
    Imagen(int w, int h) : ancho(w), alto(h), px(w * h, TRANSPARENTE) {}
    Color &en(int x, int y) { return px[y * ancho + x]; }
    Color en(int x, int y) const { return px[y * ancho + x]; }
};

// Si la pieza trae sus paletas: es un PNG con paleta, organizada en filas de 16 colores
// como las de la GBA, y cada trozo de 8x8 usa colores de una sola fila. Entonces cada
// fila es una paleta: al estampar va a una paleta del tileset con esos colores en esos
// indices (la que ya la tenga, o una nueva), los tiles guardan los indices de la imagen,
// y el color 0 de cada fila es transparente. Si no, las paletas se reparten solas, y
// `motivo` dice por que.
bool PiezaConPaletas(const Imagen &pieza, std::string *motivo);

// Lo que el juego sabe cargar (include/fieldmap.h y include/global.fieldmap.h).
struct Formato {
    int maxTiles = 1008;
    int maxMetatiles = 0x7FFF;
    int maxPaletas = 15;                  // las que carga un mapa a la vez
    int maxPaletasTileset = 256;          // las que guarda el tileset para todos sus mapas
    uint16_t mascaraId = 0x7FFF;
    uint16_t mascaraColision = 0x8000;
};

typedef std::array<uint8_t, 64> Tile;        // indices de color 0-15, por filas
// 4 entradas por capa: baja, media, alta. Cada entrada lleva el tile (bits 0-9), los
// volteos (10 horizontal, 11 vertical) y, desde el bit 12, su paleta del tileset.
typedef std::array<uint32_t, 12> Metatile;

inline int TileDeEntrada(uint32_t e) { return e & 0x3FF; }
inline int PaletaDeEntrada(uint32_t e) { return e >> 12; }

// Una animacion: unos tiles seguidos del tileset que el juego va cambiando por los de
// cada fotograma. Todos sus colores van en una paleta del tileset. En los tiles del
// tileset esta el fotograma 0, que es el que se ve al pintar: un trozo de 8x8 de una
// pieza igual a uno del fotograma 0 (tal cual o volteado) pasa a usar el tile animado.
struct Animacion {
    std::string nombre;                   // como mucho 17 letras
    int tile = 0;                         // el primero
    int ancho = 0, alto = 0;              // en tiles
    int paleta = 0;                       // del tileset
    int cada = 16;                        // fotogramas del juego que dura cada uno
    std::vector<std::vector<Tile>> fotogramas; // [fotograma][y * ancho + x]
};

struct Tileset {
    std::vector<Tile> tiles;
    std::vector<Paleta> paletas;          // todas las de sus mapas
    std::vector<Metatile> metatiles;
    std::vector<uint16_t> atributos;
    std::vector<Animacion> animaciones;
};

struct Layout {
    std::string nombre;
    int ancho = 0, alto = 0;              // en casillas
    Imagen capas[NUM_CAPAS];              // ancho*16 x alto*16
    bool tieneArteBorde = false;
    Imagen borde[NUM_CAPAS];              // 32x32, si tieneArteBorde
    std::vector<uint16_t> bloques;        // los de antes; vacio si no habia
    std::vector<uint16_t> bloquesBorde;   // los 4 de antes
};

struct Fijado {
    std::string nombre;
    int metatile;
};

struct Entrada {
    Formato formato;
    Tileset anterior;
    std::vector<Layout> layouts;          // todos los que usan el tileset
    std::vector<Fijado> fijados;
    std::vector<int> tilesFijos;          // tiles que no se tocan (animaciones)
    bool compactar = false;               // renumerar sin respetar los numeros de antes
};

struct Estadisticas {
    int tiles = 0;                        // incluido el 0, transparente
    int metatiles = 0;                    // los que existen, sin contar huecos
    int metatilesHuecos = 0;              // numeros libres por debajo del ultimo
    int paletas = 0;                      // las del tileset con algun color en uso
    std::vector<int> coloresPorPaleta;
    int metatilesNuevos = 0;
    int metatilesQuitados = 0;
    std::vector<int> metatilesPorLayout;  // metatiles distintos en cada layout
    std::vector<int> paletasPorLayout;    // paletas que carga cada layout
};

struct Salida {
    Tileset tileset;
    std::vector<std::vector<uint16_t>> bloques;
    std::vector<std::vector<uint16_t>> bloquesBorde;
    Estadisticas est;
    std::vector<std::string> avisos;
};

// Devuelve false y deja el motivo en `error` si no cabe o el arte no es valido.
bool Compilar(const Entrada &entrada, Salida &salida, std::string &error);

// ---------------------------------------------------------------------------------
// Pintar en el editor
//
// Lo que necesita porymap para pintar arte libre directamente en el mapa: una pieza
// (cualquier imagen de lado multiplo de 8, sin paleta fijada) se estampa en una capa,
// y lo que haga falta (colores, tiles, un metatile) se mete en el tileset en ese
// momento. Si no hay sitio, no se pinta y se dice que falta.
// ---------------------------------------------------------------------------------

// Un mapa que usa el tileset: hace falta conocerlos todos para saber que esta en uso.
struct MapaDelTileset {
    std::string nombre;
    int ancho = 0, alto = 0;              // en casillas
    std::vector<uint16_t> bloques;
    std::vector<uint16_t> borde;          // 4
};

enum Resultado {
    ESTAMPADO,
    SIN_HUECO_TILES,
    SIN_HUECO_PALETAS,
    SIN_HUECO_METATILES,
    DEMASIADOS_COLORES,                   // mas de 15 en un trozo de 8x8
    PIEZA_NO_VALIDA,
};

struct Estampado {
    Resultado resultado = ESTAMPADO;
    std::string mensaje;
    int casillas = 0;                     // casillas del mapa que cambian
    int metatilesNuevos = 0, tilesNuevos = 0, coloresNuevos = 0, paletasNuevas = 0;
    int paletasMapa = 0;                  // las que carga el mapa despues de estampar
    bool paletasDeLaPieza = false;        // con las paletas de la imagen (PiezaConPaletas)
};

// Estampa `pieza` en la capa `capa` de mapas[objetivo], con su esquina en (x, y) en
// pixeles, multiplos de 8. Lo que caiga fuera del mapa se ignora. Con `reemplazar`, lo
// transparente de la pieza borra la capa; sin el, deja lo que hubiera debajo.
//
// La casilla cuyo arte cambia pasa a un metatile con ese arte si ya hay uno y el mapa
// puede cargar sus paletas (el que tenga sus mismos atributos, si puede ser). Si no,
// se crea en un hueco del tileset con los atributos que tenia la casilla. La colision
// de la casilla no cambia.
//
// Cada trozo de 8x8 nuevo va, por este orden, a una paleta que el mapa ya carga y
// tiene todos sus colores; a una del tileset que los tenga, si el mapa puede cargar
// una mas; a una que el mapa ya carga y tenga sitio para los que faltan; y si el mapa
// aun puede cargar otra, a una del tileset que tenga parte de los colores o a una
// nueva.
//
// Si algo no cabe no toca nada: ni el tileset ni el mapa.
Estampado Estampar(const Formato &f, Tileset &ts, std::vector<MapaDelTileset> &mapas, int objetivo,
                   const Imagen &pieza, int x, int y, Capa capa, bool reemplazar,
                   const std::vector<int> &tilesFijos);

// Reempaqueta el tileset desde lo que hay pintado en sus mapas: junta metatiles
// duplicados, quita los que no usa ningun mapa (salvo los fijados) y deja libres los
// tiles y colores que sobran, y reparte los colores para que cada mapa cargue las
// menos paletas posibles. Respeta atributos y colision, y sin `compactar` tambien los
// numeros de lo que sigue. Es lo que libera hueco cuando estampar dice que no cabe.
bool Optimizar(const Formato &f, const Tileset &ts, const std::vector<MapaDelTileset> &mapas,
               const std::vector<Fijado> &fijados, const std::vector<int> &tilesFijos, bool compactar,
               Salida &salida, std::string &error);

// Mete una animacion en el tileset, o la cambia si ya hay una con ese nombre. Todos los
// fotogramas miden lo mismo, multiplo de 8, y entre todos tienen como mucho 15 colores
// (lo transparente es el color 0). Una animacion nueva va a tiles libres seguidos y a
// una paleta que ya tenga sus colores o a una para ella sola; cambiarla deja sus tiles y,
// si caben los colores, su paleta, y tiene que medir lo mismo. Lo pintado antes con ese
// arte se anima al optimizar. Si no cabe, no toca nada.
Estampado Animar(const Formato &f, Tileset &ts, const std::vector<MapaDelTileset> &mapas, const std::string &nombre,
                 const std::vector<Imagen> &fotogramas, int cada, const std::vector<int> &tilesFijos);

// Quita la animacion: sus tiles se quedan con el fotograma 0, ya sin animar.
bool QuitarAnimacion(Tileset &ts, const std::string &nombre);

// Las animaciones como van en animations.bin, que lee el juego (src/tileset_anims.c):
// una ficha de 32 bytes por animacion, una a cero al final, y detras los fotogramas. La
// ficha: tile (u16), tiles (u16), fotogramas (u16), cada (u16), donde empiezan sus
// fotogramas en el archivo (u32), paleta (u8), ancho en tiles (u8) y el nombre (18
// bytes, acabado en 0). Los fotogramas van seguidos, en 4bpp como en la VRAM.
std::vector<uint8_t> BytesDeAnimaciones(const std::vector<Animacion> &animaciones);
bool AnimacionesDeBytes(const std::vector<uint8_t> &bytes, std::vector<Animacion> &animaciones);

// Las paletas del tileset que carga un mapa, de menor a mayor: las de los metatiles de
// sus casillas y de su borde. Una entrada usa su paleta si su tile no es el 0.
std::vector<int> PaletasDelMapa(const Tileset &ts, const std::vector<uint16_t> &bloques,
                                const std::vector<uint16_t> &borde, uint16_t mascaraId);

// Lo contrario: las tres capas de un metatile, o de un mapa entero, desde el tileset.
void PintarMetatile(const Tileset &ts, int metatile, Imagen capas[NUM_CAPAS], int x0, int y0);
void PintarLayout(const Tileset &ts, const std::vector<uint16_t> &bloques, int ancho, int alto,
                  uint16_t mascaraId, Imagen capas[NUM_CAPAS]);

} // namespace mapeado

#endif // MAPEADO_H
