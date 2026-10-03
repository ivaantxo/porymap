#include "mapeado.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace mapeado {

Color DeRgb(int r, int g, int b)
{
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
}

void ARgb(Color c, int *r, int *g, int *b)
{
    *r = (c & 31) * 8;
    *g = ((c >> 5) & 31) * 8;
    *b = ((c >> 10) & 31) * 8;
}

namespace {

const int LADO = 16;
const int PX_CAPA = LADO * LADO;

// Las tres capas de una casilla, una detras de otra.
typedef std::array<Color, NUM_CAPAS * PX_CAPA> Arte;

// Un tile de 8x8 en colores, antes de tener paleta.
typedef std::array<Color, 64> TileColor;

std::string Hex(int n)
{
    char buf[16];
    snprintf(buf, sizeof buf, "0x%X", n);
    return buf;
}

Tile Voltear(const Tile &t, bool h, bool v)
{
    Tile r;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            r[y * 8 + x] = t[(v ? 7 - y : y) * 8 + (h ? 7 - x : x)];
    return r;
}

void PintarEntrada(const Tileset &ts, uint16_t entrada, Imagen &img, int x0, int y0)
{
    int tile = entrada & 0x3FF;
    bool h = entrada & 0x400, v = entrada & 0x800;
    int pal = entrada >> 12;
    if (tile >= (int)ts.tiles.size())
        return;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int i = ts.tiles[tile][(v ? 7 - y : y) * 8 + (h ? 7 - x : x)];
            if (i != 0 && pal < (int)ts.paletas.size())
                img.en(x0 + x, y0 + y) = ts.paletas[pal][i] & 0x7FFF;
        }
    }
}

Arte ArteDeImagenes(const Imagen capas[NUM_CAPAS], int x0, int y0)
{
    Arte a;
    for (int c = 0; c < NUM_CAPAS; c++)
        for (int y = 0; y < LADO; y++)
            for (int x = 0; x < LADO; x++)
                a[c * PX_CAPA + y * LADO + x] = capas[c].en(x0 + x, y0 + y);
    return a;
}

Arte ArteDeMetatile(const Tileset &ts, int metatile)
{
    Imagen capas[NUM_CAPAS];
    for (int c = 0; c < NUM_CAPAS; c++)
        capas[c] = Imagen(LADO, LADO);
    PintarMetatile(ts, metatile, capas, 0, 0);
    return ArteDeImagenes(capas, 0, 0);
}

TileColor TileDeArte(const Arte &a, int capa, int cuarto)
{
    TileColor t;
    int x0 = (cuarto & 1) * 8, y0 = (cuarto >> 1) * 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            t[y * 8 + x] = a[capa * PX_CAPA + (y0 + y) * LADO + x0 + x];
    return t;
}

// ---------------------------------------------------------------------------------
// Paletas
//
// Cada tile necesita que todos sus colores esten en una misma paleta de 15 (la 0 es
// la transparente). Primero se intenta meter cada tile en una de las paletas de
// antes, tal cual, para que no se muevan los colores ni los tiles. Lo que no cabe
// se reparte por los huecos. Si asi no sale, se rehace todo desde cero, y si aun no
// sale, se busca con vuelta atras.
// ---------------------------------------------------------------------------------

typedef std::vector<Color> Colores; // ordenados, sin repetir

struct EstadoPaleta {
    Paleta color;
    std::array<bool, 16> usado;

    EstadoPaleta() { color.fill(0); usado.fill(false); }

    int Libres() const
    {
        int n = 0;
        for (int i = 1; i < 16; i++)
            n += !usado[i];
        return n;
    }

    int Busca(Color c, bool soloUsados) const
    {
        for (int i = 1; i < 16; i++)
            if (color[i] == c && (usado[i] || !soloUsados))
                return i;
        return -1;
    }

    int Coste(const Colores &s) const
    {
        int n = 0;
        for (Color c : s)
            n += Busca(c, true) < 0;
        return n;
    }

    bool Cabe(const Colores &s) const { return Coste(s) <= Libres(); }

    void Anade(const Colores &s)
    {
        for (Color c : s) {
            if (Busca(c, true) >= 0)
                continue;
            int i = Busca(c, false); // un hueco que ya tenia ese color
            if (i < 0)
                for (i = 1; usado[i]; i++)
                    ;
            color[i] = c;
            usado[i] = true;
        }
    }

    bool Vacia() const
    {
        for (int i = 1; i < 16; i++)
            if (usado[i])
                return false;
        return true;
    }
};

bool Subconjunto(const Colores &a, const Colores &b)
{
    return std::includes(b.begin(), b.end(), a.begin(), a.end());
}

bool PorTamano(const Colores &a, const Colores &b)
{
    if (a.size() != b.size())
        return a.size() > b.size();
    return a < b;
}

// Mete cada conjunto en la paleta donde menos colores nuevos haga falta.
bool Voraz(std::vector<EstadoPaleta> &pals, std::vector<Colores> conjuntos)
{
    std::sort(conjuntos.begin(), conjuntos.end(), PorTamano);
    for (const Colores &s : conjuntos) {
        int mejor = -1, mejorCoste = 99;
        for (int p = 0; p < (int)pals.size(); p++) {
            int coste = pals[p].Coste(s);
            if (coste <= pals[p].Libres() && coste < mejorCoste) {
                mejor = p;
                mejorCoste = coste;
            }
        }
        if (mejor < 0)
            return false;
        pals[mejor].Anade(s);
    }
    return true;
}

bool VueltaAtras(std::vector<EstadoPaleta> &pals, const std::vector<Colores> &conjuntos, size_t i, long &nodos)
{
    if (i == conjuntos.size())
        return true;
    if (--nodos < 0)
        return false;
    std::vector<std::pair<int, int>> opciones; // coste, paleta
    bool vaciaProbada = false;
    for (int p = 0; p < (int)pals.size(); p++) {
        if (pals[p].Vacia()) {
            if (vaciaProbada)
                continue; // las vacias son todas iguales
            vaciaProbada = true;
        }
        int coste = pals[p].Coste(conjuntos[i]);
        if (coste <= pals[p].Libres())
            opciones.push_back(std::make_pair(coste, p));
    }
    std::sort(opciones.begin(), opciones.end());
    for (auto &o : opciones) {
        EstadoPaleta copia = pals[o.second];
        pals[o.second].Anade(conjuntos[i]);
        if (VueltaAtras(pals, conjuntos, i + 1, nodos))
            return true;
        pals[o.second] = copia;
    }
    return false;
}

std::vector<Colores> Maximales(std::vector<Colores> conjuntos)
{
    std::sort(conjuntos.begin(), conjuntos.end(), PorTamano);
    std::vector<Colores> r;
    for (const Colores &s : conjuntos) {
        bool dentro = false;
        for (const Colores &m : r)
            if (Subconjunto(s, m)) {
                dentro = true;
                break;
            }
        if (!dentro)
            r.push_back(s);
    }
    return r;
}

bool Paletas(const std::vector<Colores> &conjuntos, const std::vector<Paleta> &anteriores, int maxPaletas,
             bool respetarAnteriores, std::vector<EstadoPaleta> &pals)
{
    // 1. Con las paletas de antes.
    if (respetarAnteriores) {
        pals.assign(maxPaletas, EstadoPaleta());
        for (int p = 0; p < maxPaletas && p < (int)anteriores.size(); p++)
            pals[p].color = anteriores[p];
        std::vector<Colores> resto;
        for (const Colores &s : conjuntos) {
            int dentro = -1;
            for (int p = 0; p < maxPaletas && dentro < 0; p++) {
                bool todos = true;
                for (Color c : s)
                    if (pals[p].Busca(c, false) < 0) {
                        todos = false;
                        break;
                    }
                if (todos)
                    dentro = p;
            }
            if (dentro < 0) {
                resto.push_back(s);
                continue;
            }
            for (Color c : s)
                pals[dentro].usado[pals[dentro].Busca(c, false)] = true;
        }
        if (Voraz(pals, resto))
            return true;
    }

    // 2. Desde cero.
    std::vector<Colores> maximales = Maximales(conjuntos);
    pals.assign(maxPaletas, EstadoPaleta());
    if (Voraz(pals, maximales))
        return true;

    // 3. Con vuelta atras.
    pals.assign(maxPaletas, EstadoPaleta());
    long nodos = 2000000;
    return VueltaAtras(pals, maximales, 0, nodos);
}

int PaletaDe(const std::vector<EstadoPaleta> &pals, const Colores &s)
{
    for (int p = 0; p < (int)pals.size(); p++) {
        bool todos = true;
        for (Color c : s)
            if (pals[p].Busca(c, true) < 0) {
                todos = false;
                break;
            }
        if (todos)
            return p;
    }
    return -1;
}

// ---------------------------------------------------------------------------------
// Metatiles
// ---------------------------------------------------------------------------------

// Donde aparece una casilla, para los avisos.
struct Sitio {
    int layout;   // -1 para los fijados
    int casilla;  // indice en el blockdata, o -(1 + n) para la casilla n del borde
};

struct Casilla {
    int arte;              // indice en la tabla de artes
    int anterior;          // metatile que tenia la casilla, o -1
    uint16_t colision;
};

struct Clave {
    int arte;
    uint16_t atributos;
    bool operator<(const Clave &o) const
    {
        return arte != o.arte ? arte < o.arte : atributos < o.atributos;
    }
};

} // namespace

void PintarMetatile(const Tileset &ts, int metatile, Imagen capas[NUM_CAPAS], int x0, int y0)
{
    if (metatile < 0 || metatile >= (int)ts.metatiles.size())
        return;
    const Metatile &m = ts.metatiles[metatile];
    for (int c = 0; c < NUM_CAPAS; c++)
        for (int q = 0; q < 4; q++)
            PintarEntrada(ts, m[c * 4 + q], capas[c], x0 + (q & 1) * 8, y0 + (q >> 1) * 8);
}

void PintarLayout(const Tileset &ts, const std::vector<uint16_t> &bloques, int ancho, int alto,
                  uint16_t mascaraId, Imagen capas[NUM_CAPAS])
{
    for (int c = 0; c < NUM_CAPAS; c++)
        capas[c] = Imagen(ancho * LADO, alto * LADO);
    for (int y = 0; y < alto; y++)
        for (int x = 0; x < ancho; x++)
            PintarMetatile(ts, bloques[y * ancho + x] & mascaraId, capas, x * LADO, y * LADO);
}

bool Compilar(const Entrada &e, Salida &s, std::string &error)
{
    const Formato &f = e.formato;
    const Tileset &ant = e.anterior;
    s = Salida();

    auto describe = [&](const Sitio &sitio) -> std::string {
        if (sitio.layout < 0)
            return "el metatile fijado " + e.fijados[sitio.casilla].nombre;
        const Layout &l = e.layouts[sitio.layout];
        if (sitio.casilla < 0)
            return l.nombre + ", casilla " + std::to_string(-1 - sitio.casilla) + " del borde";
        return l.nombre + " (" + std::to_string(sitio.casilla % l.ancho) + "," + std::to_string(sitio.casilla / l.ancho) + ")";
    };

    // --- El arte de antes, para reconocerlo -----------------------------------------
    std::vector<Arte> artes;
    std::map<Arte, int> indiceArte;
    auto registra = [&](const Arte &a) {
        auto it = indiceArte.find(a);
        if (it != indiceArte.end())
            return it->second;
        artes.push_back(a);
        indiceArte[a] = artes.size() - 1;
        return (int)artes.size() - 1;
    };

    int numAnteriores = std::min(ant.metatiles.size(), ant.atributos.size());
    std::vector<int> arteAnterior(numAnteriores);
    for (int m = 0; m < numAnteriores; m++)
        arteAnterior[m] = registra(ArteDeMetatile(ant, m));

    // Cuantas veces se usaba cada metatile, para elegir entre duplicados.
    std::vector<int> usos(numAnteriores, 0);
    for (const Layout &l : e.layouts) {
        for (uint16_t b : l.bloques)
            if ((b & f.mascaraId) < numAnteriores)
                usos[b & f.mascaraId]++;
    }
    // Arte -> el metatile de antes con ese arte que mas se usaba.
    std::map<int, int> anteriorPorArte;
    for (int m = 0; m < numAnteriores; m++) {
        auto it = anteriorPorArte.find(arteAnterior[m]);
        if (it == anteriorPorArte.end() || usos[m] > usos[it->second])
            anteriorPorArte[arteAnterior[m]] = m;
    }

    // --- Las casillas de ahora --------------------------------------------------------
    std::vector<std::vector<Casilla>> casillas(e.layouts.size()), bordes(e.layouts.size());
    std::map<int, Sitio> primerSitio; // arte -> donde aparece primero
    for (size_t li = 0; li < e.layouts.size(); li++) {
        const Layout &l = e.layouts[li];
        bool hayBloques = (int)l.bloques.size() == l.ancho * l.alto;
        for (int c = 0; c < NUM_CAPAS; c++) {
            if (l.capas[c].ancho != l.ancho * LADO || l.capas[c].alto != l.alto * LADO) {
                error = l.nombre + ": las capas tienen que medir " + std::to_string(l.ancho * LADO) + "x" +
                        std::to_string(l.alto * LADO) + " (el layout es de " + std::to_string(l.ancho) + "x" +
                        std::to_string(l.alto) + " casillas)";
                return false;
            }
        }
        for (int y = 0; y < l.alto; y++) {
            for (int x = 0; x < l.ancho; x++) {
                int i = y * l.ancho + x;
                Casilla c;
                c.arte = registra(ArteDeImagenes(l.capas, x * LADO, y * LADO));
                c.anterior = hayBloques ? l.bloques[i] & f.mascaraId : -1;
                if (c.anterior >= numAnteriores)
                    c.anterior = -1;
                c.colision = hayBloques ? l.bloques[i] & f.mascaraColision : 0;
                if (!primerSitio.count(c.arte))
                    primerSitio[c.arte] = Sitio{(int)li, i};
                casillas[li].push_back(c);
            }
        }
        for (int i = 0; i < 4; i++) {
            Casilla c;
            uint16_t b = i < (int)l.bloquesBorde.size() ? l.bloquesBorde[i] : 0;
            c.anterior = (b & f.mascaraId) < numAnteriores ? b & f.mascaraId : -1;
            c.colision = b & f.mascaraColision;
            if (l.tieneArteBorde)
                c.arte = registra(ArteDeImagenes(l.borde, (i & 1) * LADO, (i >> 1) * LADO));
            else if (c.anterior >= 0)
                c.arte = arteAnterior[c.anterior];
            else {
                Arte vacio;
                vacio.fill(TRANSPARENTE);
                c.arte = registra(vacio);
            }
            if (!primerSitio.count(c.arte))
                primerSitio[c.arte] = Sitio{(int)li, -1 - i};
            bordes[li].push_back(c);
        }
    }

    // --- Atributos de cada casilla ----------------------------------------------------
    // El arte nuevo, que no tenia ningun metatile de antes, hereda por mayoria los
    // atributos de los metatiles que habia en las casillas donde ahora aparece.
    std::map<int, std::map<uint16_t, int>> votos;
    auto vota = [&](const Casilla &c) {
        if (!anteriorPorArte.count(c.arte) && c.anterior >= 0)
            votos[c.arte][ant.atributos[c.anterior]]++;
    };
    for (size_t li = 0; li < e.layouts.size(); li++) {
        for (const Casilla &c : casillas[li])
            vota(c);
        for (const Casilla &c : bordes[li])
            vota(c);
    }
    std::map<int, uint16_t> heredado;
    for (auto &v : votos) {
        int mejor = -1;
        for (auto &a : v.second)
            if (a.second > mejor) {
                mejor = a.second;
                heredado[v.first] = a.first;
            }
    }

    // Clave de la casilla y el numero que le gustaria conservar.
    auto clave = [&](const Casilla &c, int *preferido) -> Clave {
        if (c.anterior >= 0 && arteAnterior[c.anterior] == c.arte) {
            *preferido = c.anterior;
            return Clave{c.arte, ant.atributos[c.anterior]};
        }
        auto it = anteriorPorArte.find(c.arte);
        if (it != anteriorPorArte.end()) {
            *preferido = it->second;
            return Clave{c.arte, ant.atributos[it->second]};
        }
        *preferido = -1;
        auto h = heredado.find(c.arte);
        return Clave{c.arte, h != heredado.end() ? h->second : (uint16_t)0};
    };

    // --- Numeros de metatile ----------------------------------------------------------
    std::map<Clave, int> numero;
    std::vector<int> ocupante;           // numero -> indice de clave, o -1
    std::vector<Clave> claves;
    auto ocupa = [&](int n, const Clave &k) {
        if (n >= (int)ocupante.size())
            ocupante.resize(n + 1, -1);
        int ki = claves.size();
        claves.push_back(k);
        ocupante[n] = ki;
        if (!numero.count(k))
            numero[k] = n;
    };
    auto libre = [&](int n) { return n >= (int)ocupante.size() || ocupante[n] < 0; };

    // Los fijados, en su numero, con su arte de antes.
    for (size_t i = 0; i < e.fijados.size(); i++) {
        int n = e.fijados[i].metatile;
        if (n >= numAnteriores) {
            s.avisos.push_back("El metatile fijado " + e.fijados[i].nombre + " (" + Hex(n) +
                               ") no existe en el tileset y se ignora");
            continue;
        }
        Clave k{arteAnterior[n], ant.atributos[n]};
        if (!primerSitio.count(k.arte))
            primerSitio[k.arte] = Sitio{-1, (int)i};
        if (libre(n))
            ocupa(n, k);
    }

    // Las casillas, en orden: primero las que conservan su numero de antes.
    std::vector<std::pair<Clave, int>> pendientes; // clave, preferido
    for (size_t li = 0; li < e.layouts.size(); li++) {
        for (auto *lista : {&casillas[li], &bordes[li]})
            for (const Casilla &c : *lista) {
                int pref;
                Clave k = clave(c, &pref);
                pendientes.push_back(std::make_pair(k, e.compactar ? -1 : pref));
            }
    }
    for (auto &p : pendientes)
        if (!numero.count(p.first) && p.second >= 0 && libre(p.second))
            ocupa(p.second, p.first);
    int siguiente = 0;
    for (auto &p : pendientes) {
        if (numero.count(p.first))
            continue;
        while (!libre(siguiente))
            siguiente++;
        ocupa(siguiente, p.first);
    }
    int totalMetatiles = ocupante.size();
    if (totalMetatiles > f.maxMetatiles) {
        error = "Hacen falta " + std::to_string(totalMetatiles) + " metatiles y caben " + std::to_string(f.maxMetatiles);
        return false;
    }

    // --- Tiles de 8x8 y sus colores ---------------------------------------------------
    std::vector<TileColor> tilesColor;
    std::map<TileColor, int> indiceTile;
    std::vector<Colores> coloresTile;
    // metatile -> 12 indices en tilesColor (-1 = transparente)
    std::vector<std::array<int, 12>> usoTiles(totalMetatiles);
    for (int n = 0; n < totalMetatiles; n++) {
        usoTiles[n].fill(-1);
        if (ocupante[n] < 0)
            continue;
        const Clave &k = claves[ocupante[n]];
        for (int c = 0; c < NUM_CAPAS; c++) {
            for (int q = 0; q < 4; q++) {
                TileColor t = TileDeArte(artes[k.arte], c, q);
                std::set<Color> cs;
                for (Color px : t)
                    if (px != TRANSPARENTE)
                        cs.insert(px);
                if (cs.empty())
                    continue;
                if (cs.size() > 15) {
                    static const char *nombres[] = {"baja", "media", "alta"};
                    error = describe(primerSitio[k.arte]) + ", capa " + nombres[c] + ": un trozo de 8x8 tiene " +
                            std::to_string(cs.size()) + " colores y una paleta admite 15";
                    return false;
                }
                auto it = indiceTile.find(t);
                if (it == indiceTile.end()) {
                    tilesColor.push_back(t);
                    coloresTile.push_back(Colores(cs.begin(), cs.end()));
                    it = indiceTile.insert(std::make_pair(t, (int)tilesColor.size() - 1)).first;
                }
                usoTiles[n][c * 4 + q] = it->second;
            }
        }
    }

    // --- Paletas ----------------------------------------------------------------------
    std::vector<Colores> conjuntos(coloresTile);
    std::sort(conjuntos.begin(), conjuntos.end());
    conjuntos.erase(std::unique(conjuntos.begin(), conjuntos.end()), conjuntos.end());
    std::vector<EstadoPaleta> pals;
    if (!Paletas(conjuntos, ant.paletas, f.maxPaletas, !e.compactar, pals)) {
        std::set<Color> todos;
        for (auto &c : conjuntos)
            todos.insert(c.begin(), c.end());
        error = "Los colores no caben en " + std::to_string(f.maxPaletas) + " paletas de 15: hay " +
                std::to_string(conjuntos.size()) + " combinaciones distintas de colores en trozos de 8x8 y " +
                std::to_string(todos.size()) + " colores en total";
        return false;
    }

    // --- Tiles en indices, con volteos ------------------------------------------------
    // Un tile que ya estaba (tal cual o volteado) vuelve a su sitio de antes. Los demas
    // van a los huecos cuando ya se sabe cuales quedan libres.
    std::set<int> fijos;
    for (int t : e.tilesFijos)
        if (t > 0 && t < (int)ant.tiles.size())
            fijos.insert(t);
    // Datos de antes (y sus volteos) -> tile y volteo; gana el numero mas bajo.
    std::map<Tile, std::pair<int, int>> anteriores;
    for (int t = (int)ant.tiles.size() - 1; t >= 1; t--) {
        if (e.compactar && !fijos.count(t))
            continue;
        for (int v = 3; v >= 0; v--)
            anteriores[Voltear(ant.tiles[t], v & 1, v & 2)] = std::make_pair(t, v);
    }

    std::vector<Tile> tiles(1); // el 0, transparente
    tiles[0].fill(0);
    std::vector<bool> tileOcupado(1, true);
    std::map<Tile, std::pair<int, int>> colocados; // datos (y volteos) -> tile y volteo
    auto coloca = [&](int t, const Tile &datos) {
        if (t >= (int)tiles.size()) {
            Tile vacio;
            vacio.fill(0);
            tiles.resize(t + 1, vacio);
            tileOcupado.resize(t + 1, false);
        }
        tiles[t] = datos;
        tileOcupado[t] = true;
        for (int v = 0; v < 4; v++) {
            Tile w = Voltear(datos, v & 1, v & 2);
            if (!colocados.count(w))
                colocados[w] = std::make_pair(t, v);
        }
    };
    for (int t : fijos)
        coloca(t, ant.tiles[t]);

    std::vector<int> paletaTile(tilesColor.size());
    std::vector<Tile> datosTile(tilesColor.size());
    for (size_t i = 0; i < tilesColor.size(); i++) {
        int pal = PaletaDe(pals, coloresTile[i]);
        paletaTile[i] = pal;
        for (int p = 0; p < 64; p++)
            datosTile[i][p] = tilesColor[i][p] == TRANSPARENTE ? 0 : pals[pal].Busca(tilesColor[i][p], true);
    }
    // Como en colocados no estaba, su sitio de antes esta libre: si lo ocupara otro
    // tile, seria uno con estos mismos datos y ya estaria en colocados.
    for (size_t i = 0; i < tilesColor.size(); i++) {
        auto ia = anteriores.find(datosTile[i]);
        if (!colocados.count(datosTile[i]) && ia != anteriores.end())
            coloca(ia->second.first, ant.tiles[ia->second.first]);
    }
    int siguienteTile = 1;
    std::vector<uint16_t> entradaTile(tilesColor.size());
    for (size_t i = 0; i < tilesColor.size(); i++) {
        if (!colocados.count(datosTile[i])) {
            while (siguienteTile < (int)tileOcupado.size() && tileOcupado[siguienteTile])
                siguienteTile++;
            coloca(siguienteTile, datosTile[i]);
        }
        std::pair<int, int> sitio = colocados[datosTile[i]];
        entradaTile[i] = sitio.first | ((sitio.second & 1) << 10) | ((sitio.second >> 1) << 11) | (paletaTile[i] << 12);
    }
    if ((int)tiles.size() > f.maxTiles) {
        error = "Hacen falta " + std::to_string(tiles.size()) + " tiles y caben " + std::to_string(f.maxTiles);
        return false;
    }

    // --- Tileset ----------------------------------------------------------------------
    Tileset &ts = s.tileset;
    ts.tiles = tiles;
    for (int p = 0; p < f.maxPaletas; p++) {
        Paleta pal = pals[p].color;
        pal[0] = TRANSPARENTE;
        ts.paletas.push_back(pal);
    }
    ts.metatiles.assign(totalMetatiles, Metatile());
    ts.atributos.assign(totalMetatiles, 0);
    for (int n = 0; n < totalMetatiles; n++) {
        ts.metatiles[n].fill(0);
        if (ocupante[n] < 0)
            continue;
        ts.atributos[n] = claves[ocupante[n]].atributos;
        for (int q = 0; q < 12; q++)
            if (usoTiles[n][q] >= 0)
                ts.metatiles[n][q] = entradaTile[usoTiles[n][q]];
    }

    // --- Blockdata --------------------------------------------------------------------
    for (size_t li = 0; li < e.layouts.size(); li++) {
        std::set<int> distintos;
        std::vector<uint16_t> bloques, borde;
        for (const Casilla &c : casillas[li]) {
            int pref;
            int n = numero[clave(c, &pref)];
            distintos.insert(n);
            bloques.push_back(n | c.colision);
        }
        for (const Casilla &c : bordes[li]) {
            int pref;
            borde.push_back(numero[clave(c, &pref)] | c.colision);
        }
        s.bloques.push_back(bloques);
        s.bloquesBorde.push_back(borde);
        s.est.metatilesPorLayout.push_back(distintos.size());
    }

    // --- Cuentas ----------------------------------------------------------------------
    Estadisticas &est = s.est;
    est.tiles = tiles.size();
    for (int n = 0; n < totalMetatiles; n++) {
        if (ocupante[n] < 0) {
            est.metatilesHuecos++;
            continue;
        }
        est.metatiles++;
        const Clave &k = claves[ocupante[n]];
        if (n >= numAnteriores || arteAnterior[n] != k.arte || ant.atributos[n] != k.atributos)
            est.metatilesNuevos++;
    }
    // Un hueco de antes (transparente y sin atributos) que sigue libre no se ha quitado.
    Arte vacio;
    vacio.fill(TRANSPARENTE);
    auto itVacio = indiceArte.find(vacio);
    int arteVacio = itVacio != indiceArte.end() ? itVacio->second : -1;
    for (int n = 0; n < numAnteriores; n++) {
        bool eraHueco = arteAnterior[n] == arteVacio && ant.atributos[n] == 0;
        bool sigue = n < totalMetatiles && ocupante[n] >= 0 && arteAnterior[n] == claves[ocupante[n]].arte &&
                     ant.atributos[n] == claves[ocupante[n]].atributos;
        if (!sigue && !(eraHueco && (n >= totalMetatiles || ocupante[n] < 0)))
            est.metatilesQuitados++;
    }
    for (auto &p : pals) {
        int n = 15 - p.Libres();
        if (n > 0)
            est.paletas++;
        est.coloresPorPaleta.push_back(n);
    }
    return true;
}

// ---------------------------------------------------------------------------------
// Estampar
// ---------------------------------------------------------------------------------

namespace {

// El tileset mientras se estampa: lo que hay, que esta en uso y lo que se va metiendo.
struct Relleno {
    const Formato &f;
    Tileset ts;
    std::vector<EstadoPaleta> pals;
    std::vector<bool> tileUsado;
    std::set<int> tilesFijos;
    std::map<Tile, std::pair<int, int>> tiles; // datos (y volteos) -> tile y volteo
    Estampado &r;

    Relleno(const Formato &formato, const Tileset &t, const std::vector<int> &fijos, Estampado &res)
        : f(formato), ts(t), tilesFijos(fijos.begin(), fijos.end()), r(res)
    {
        int numPaletas = std::min((int)ts.paletas.size(), f.maxPaletas);
        pals.assign(numPaletas, EstadoPaleta());
        for (int p = 0; p < numPaletas; p++)
            pals[p].color = ts.paletas[p];
        tileUsado.assign(ts.tiles.size(), false);
        if (!tileUsado.empty())
            tileUsado[0] = true;
        // Un color esta en uso si algun metatile lo pinta, este o no en un mapa.
        for (const Metatile &m : ts.metatiles) {
            for (uint16_t e : m) {
                int t = e & 0x3FF, p = e >> 12;
                if (t >= (int)ts.tiles.size())
                    continue;
                tileUsado[t] = true;
                if (p < numPaletas)
                    for (uint8_t i : ts.tiles[t])
                        if (i)
                            pals[p].usado[i] = true;
            }
        }
        for (int t = (int)ts.tiles.size() - 1; t >= 0; t--)
            for (int v = 3; v >= 0; v--)
                tiles[Voltear(ts.tiles[t], v & 1, v & 2)] = std::make_pair(t, v);
    }

    bool Falla(Resultado res, const std::string &mensaje)
    {
        r.resultado = res;
        r.mensaje = mensaje;
        return false;
    }

    // La entrada de metatile para un trozo de 8x8, metiendo lo que falte.
    bool Entrada(const TileColor &t, uint16_t *entrada)
    {
        std::set<Color> cs;
        for (Color px : t)
            if (px != TRANSPARENTE)
                cs.insert(px);
        if (cs.empty()) {
            *entrada = 0;
            return true;
        }
        if (cs.size() > 15)
            return Falla(DEMASIADOS_COLORES, "un trozo de 8x8 de la pieza tiene " + std::to_string(cs.size()) +
                                                 " colores y una paleta admite 15");
        Colores s(cs.begin(), cs.end());

        int pal = PaletaDe(pals, s);
        if (pal < 0) {
            int mejorCoste = 99;
            for (int p = 0; p < (int)pals.size(); p++) {
                int coste = pals[p].Coste(s);
                if (coste <= pals[p].Libres() && coste < mejorCoste) {
                    pal = p;
                    mejorCoste = coste;
                }
            }
            if (pal < 0)
                return Falla(SIN_HUECO_PALETAS, "no hay hueco en las paletas para " + std::to_string(s.size()) +
                                                    " colores de un trozo de 8x8 (ninguna tiene sitio para los que le faltan)");
            r.coloresNuevos += mejorCoste;
            pals[pal].Anade(s);
        }

        Tile datos;
        for (int p = 0; p < 64; p++)
            datos[p] = t[p] == TRANSPARENTE ? 0 : pals[pal].Busca(t[p], true);
        auto it = tiles.find(datos);
        if (it == tiles.end()) {
            int n = -1;
            for (int i = 1; i < (int)ts.tiles.size() && n < 0; i++) {
                bool vacio = std::all_of(ts.tiles[i].begin(), ts.tiles[i].end(), [](uint8_t v) { return v == 0; });
                if (!tileUsado[i] && vacio && !tilesFijos.count(i))
                    n = i;
            }
            if (n < 0) {
                if ((int)ts.tiles.size() >= f.maxTiles)
                    return Falla(SIN_HUECO_TILES, "no hay hueco para tiles: el tileset ya tiene los " +
                                                      std::to_string(f.maxTiles) + " que caben");
                n = ts.tiles.size();
                ts.tiles.push_back(Tile());
                tileUsado.push_back(false);
            }
            ts.tiles[n] = datos;
            tileUsado[n] = true;
            r.tilesNuevos++;
            for (int v = 3; v >= 0; v--)
                tiles[Voltear(datos, v & 1, v & 2)] = std::make_pair(n, v);
            it = tiles.find(datos);
        }
        *entrada = it->second.first | ((it->second.second & 1) << 10) | ((it->second.second >> 1) << 11) | (pal << 12);
        return true;
    }

    void Guarda(Tileset &destino)
    {
        for (int p = 0; p < (int)pals.size(); p++)
            for (int i = 1; i < 16; i++)
                ts.paletas[p][i] = pals[p].color[i];
        destino = ts;
    }
};

} // namespace

Estampado Estampar(const Formato &f, Tileset &ts, std::vector<MapaDelTileset> &mapas, int objetivo,
                   const Imagen &pieza, int x, int y, Capa capa, bool reemplazar,
                   const std::vector<int> &tilesFijos)
{
    Estampado r;
    auto falla = [&](Resultado res, const std::string &mensaje) {
        r.resultado = res;
        r.mensaje = mensaje;
        return r;
    };
    if (objetivo < 0 || objetivo >= (int)mapas.size())
        return falla(PIEZA_NO_VALIDA, "no hay ese mapa");
    if (pieza.ancho <= 0 || pieza.alto <= 0 || pieza.ancho % 8 || pieza.alto % 8)
        return falla(PIEZA_NO_VALIDA, "la pieza mide " + std::to_string(pieza.ancho) + "x" + std::to_string(pieza.alto) +
                                          " y tiene que ser multiplo de 8 en los dos lados");
    if (x % 8 || y % 8)
        return falla(PIEZA_NO_VALIDA, "la pieza va en la rejilla de 8 pixeles");
    MapaDelTileset &mapa = mapas[objetivo];
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(mapa.ancho * LADO, x + pieza.ancho), y1 = std::min(mapa.alto * LADO, y + pieza.alto);
    if (x0 >= x1 || y0 >= y1)
        return falla(PIEZA_NO_VALIDA, "la pieza cae fuera del mapa");

    Relleno rel(f, ts, tilesFijos, r);
    std::vector<uint16_t> bloques = mapa.bloques;

    // Metatiles por arte, y que numeros estan en uso en algun mapa.
    int numMetatiles = std::min(rel.ts.metatiles.size(), rel.ts.atributos.size());
    std::map<Arte, std::vector<int>> porArte;
    for (int m = 0; m < numMetatiles; m++)
        porArte[ArteDeMetatile(rel.ts, m)].push_back(m);
    std::vector<bool> enUso(numMetatiles, false);
    for (const MapaDelTileset &md : mapas)
        for (auto *lista : {&md.bloques, &md.borde})
            for (uint16_t b : *lista)
                if ((b & f.mascaraId) < numMetatiles)
                    enUso[b & f.mascaraId] = true;
    auto esHueco = [&](int m) {
        return !enUso[m] && rel.ts.atributos[m] == 0 &&
               std::all_of(rel.ts.metatiles[m].begin(), rel.ts.metatiles[m].end(), [](uint16_t e) { return e == 0; });
    };

    for (int cy = y0 / LADO; cy <= (y1 - 1) / LADO; cy++) {
        for (int cx = x0 / LADO; cx <= (x1 - 1) / LADO; cx++) {
            int i = cy * mapa.ancho + cx;
            int antes = bloques[i] & f.mascaraId;
            uint16_t colision = bloques[i] & f.mascaraColision;
            Arte arte;
            arte.fill(TRANSPARENTE);
            if (antes < numMetatiles)
                arte = ArteDeMetatile(rel.ts, antes);
            Arte nuevo = arte;
            for (int py = 0; py < LADO; py++) {
                for (int px = 0; px < LADO; px++) {
                    int mx = cx * LADO + px, my = cy * LADO + py;
                    if (mx < x0 || mx >= x1 || my < y0 || my >= y1)
                        continue;
                    Color c = pieza.en(mx - x, my - y);
                    if (c != TRANSPARENTE || reemplazar)
                        nuevo[capa * PX_CAPA + py * LADO + px] = c;
                }
            }
            if (nuevo == arte)
                continue;

            uint16_t atributos = antes < numMetatiles ? rel.ts.atributos[antes] : 0;
            int destino = -1;
            auto it = porArte.find(nuevo);
            if (it != porArte.end()) {
                destino = it->second[0];
                for (int m : it->second)
                    if (rel.ts.atributos[m] == atributos) {
                        destino = m;
                        break;
                    }
            } else {
                Metatile mt;
                for (int c = 0; c < NUM_CAPAS; c++)
                    for (int q = 0; q < 4; q++)
                        if (!rel.Entrada(TileDeArte(nuevo, c, q), &mt[c * 4 + q]))
                            return r;
                for (int m = 0; m < numMetatiles && destino < 0; m++)
                    if (esHueco(m))
                        destino = m;
                if (destino < 0) {
                    if (numMetatiles >= f.maxMetatiles)
                        return falla(SIN_HUECO_METATILES, "no hay hueco para metatiles: el tileset ya tiene los " +
                                                              std::to_string(f.maxMetatiles) + " que caben");
                    destino = numMetatiles++;
                    rel.ts.metatiles.resize(numMetatiles);
                    rel.ts.atributos.resize(numMetatiles);
                    enUso.push_back(false);
                }
                rel.ts.metatiles[destino] = mt;
                rel.ts.atributos[destino] = atributos;
                porArte[nuevo].push_back(destino);
                r.metatilesNuevos++;
            }
            enUso[destino] = true;
            bloques[i] = destino | colision;
            r.casillas++;
        }
    }

    rel.Guarda(ts);
    mapa.bloques = bloques;
    return r;
}

bool Optimizar(const Formato &f, const Tileset &ts, const std::vector<MapaDelTileset> &mapas,
               const std::vector<Fijado> &fijados, const std::vector<int> &tilesFijos, bool compactar,
               Salida &salida, std::string &error)
{
    Entrada e;
    e.formato = f;
    e.anterior = ts;
    e.fijados = fijados;
    e.tilesFijos = tilesFijos;
    e.compactar = compactar;
    for (const MapaDelTileset &m : mapas) {
        Layout l;
        l.nombre = m.nombre;
        l.ancho = m.ancho;
        l.alto = m.alto;
        l.bloques = m.bloques;
        l.bloquesBorde = m.borde;
        l.bloquesBorde.resize(4, 0);
        PintarLayout(ts, l.bloques, l.ancho, l.alto, f.mascaraId, l.capas);
        PintarLayout(ts, l.bloquesBorde, 2, 2, f.mascaraId, l.borde);
        l.tieneArteBorde = true;
        e.layouts.push_back(l);
    }
    return Compilar(e, salida, error);
}

} // namespace mapeado
