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

void PintarEntrada(const Tileset &ts, uint32_t entrada, Imagen &img, int x0, int y0)
{
    int tile = TileDeEntrada(entrada);
    bool h = entrada & 0x400, v = entrada & 0x800;
    int pal = PaletaDeEntrada(entrada);
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
// la transparente). El tileset guarda las paletas de todos sus mapas, y cada mapa
// carga solo las de sus tiles, como mucho maxPaletas a la vez.
//
// Compilar reparte los trozos de 8x8 entre las paletas del tileset haciendo que cada
// mapa cargue las menos posibles. Sin compactar, primero intenta dejar cada trozo en
// una paleta de antes que ya tenga sus colores, para que no se muevan ni los colores
// ni los tiles. Si asi no sale, se rehace desde cero, y luego mapa por mapa.
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

    bool Tiene(const Colores &s, bool soloUsados) const
    {
        for (Color c : s)
            if (Busca(c, soloUsados) < 0)
                return false;
        return true;
    }

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

bool PorTamano(const Colores &a, const Colores &b)
{
    if (a.size() != b.size())
        return a.size() > b.size();
    return a < b;
}

// El reparto de los conjuntos de colores de los trozos de 8x8 entre las paletas del
// tileset, con las que carga cada mapa.
struct Reparto {
    const Formato &f;
    const std::vector<Colores> &conjuntos;
    const std::vector<std::vector<int>> &mapas; // conjunto -> mapas donde sale
    std::vector<EstadoPaleta> pals;
    std::vector<std::vector<bool>> carga;       // paleta -> mapa -> la carga
    std::vector<int> cargadas;                  // mapa -> cuantas carga
    std::vector<int> paletaDe;                  // conjunto -> paleta
    int mapaLleno = -1;                         // el que no dejo colocar un conjunto

    Reparto(const Formato &formato, const std::vector<Colores> &c, const std::vector<std::vector<int>> &m, int numMapas)
        : f(formato), conjuntos(c), mapas(m), cargadas(numMapas, 0), paletaDe(c.size(), -1)
    {
    }

    int NuevaPaleta()
    {
        pals.push_back(EstadoPaleta());
        carga.push_back(std::vector<bool>(cargadas.size(), false));
        return pals.size() - 1;
    }

    // Cuantos mapas del conjunto tendrian que cargar la paleta p; -1 si alguno ya no puede.
    int Cargas(int p, int i)
    {
        int n = 0;
        for (int m : mapas[i]) {
            if (carga[p][m])
                continue;
            if (cargadas[m] >= f.maxPaletas) {
                mapaLleno = m;
                return -1;
            }
            n++;
        }
        return n;
    }

    void Asigna(int i, int p)
    {
        pals[p].Anade(conjuntos[i]);
        for (int m : mapas[i])
            if (!carga[p][m]) {
                carga[p][m] = true;
                cargadas[m]++;
            }
        paletaDe[i] = p;
    }

    // Donde haga falta cargar menos paletas y, luego, meter menos colores. Entre una
    // paleta vacia y otra igual de buena, la vacia solo si hay que cargarla de todas
    // formas: asi no se mezclan colores de mapas que no tienen nada que ver.
    bool Coloca(int i)
    {
        const Colores &s = conjuntos[i];
        int mejor = -1;
        std::array<int, 3> mejorCoste = {{0, 0, 0}};
        bool vaciaVista = false;
        for (int p = 0; p <= (int)pals.size(); p++) {
            bool nueva = p == (int)pals.size();
            if (nueva && (vaciaVista || (int)pals.size() >= f.maxPaletasTileset))
                break;
            bool vacia = nueva || pals[p].Vacia();
            if (vacia && !nueva) {
                if (vaciaVista)
                    continue; // las vacias son todas iguales
                vaciaVista = true;
            }
            int colores = nueva ? s.size() : pals[p].Coste(s);
            if (!nueva && colores > pals[p].Libres())
                continue;
            int cargas = nueva ? (int)mapas[i].size() : Cargas(p, i);
            if (cargas < 0)
                continue;
            if (nueva && cargas > 0)
                for (int m : mapas[i])
                    if (cargadas[m] >= f.maxPaletas) {
                        mapaLleno = m;
                        cargas = -1;
                        break;
                    }
            if (cargas < 0)
                continue;
            std::array<int, 3> coste = {{cargas, colores, (cargas > 0) == vacia ? 0 : 1}};
            if (mejor < 0 || coste < mejorCoste) {
                mejor = p;
                mejorCoste = coste;
            }
        }
        if (mejor < 0)
            return false;
        if (mejor == (int)pals.size())
            NuevaPaleta();
        Asigna(i, mejor);
        return true;
    }

    // Con las paletas de antes: cada conjunto que ya este entero en una, se queda alli.
    // Entre varias, la que menos cargas haga y, mejor, una que se usara antes, que una
    // que no usaba nadie solo tiene colores viejos que coinciden.
    void Siembra(const std::vector<Paleta> &anteriores, const std::vector<bool> &usadas, std::vector<int> &resto,
                 const std::vector<int> &orden)
    {
        for (size_t p = 0; p < anteriores.size() && (int)p < f.maxPaletasTileset; p++)
            pals[NuevaPaleta()].color = anteriores[p];
        for (int i : orden) {
            int mejor = -1;
            std::pair<int, bool> mejorCoste;
            for (int p = 0; p < (int)pals.size(); p++) {
                if (!pals[p].Tiene(conjuntos[i], false))
                    continue;
                int cargas = Cargas(p, i);
                std::pair<int, bool> coste(cargas, !(p < (int)usadas.size() && usadas[p]));
                if (cargas >= 0 && (mejor < 0 || coste < mejorCoste)) {
                    mejor = p;
                    mejorCoste = coste;
                }
            }
            if (mejor < 0)
                resto.push_back(i);
            else
                Asigna(i, mejor);
        }
    }
};

// Reparte los conjuntos entre las paletas del tileset sin que ningun mapa pase de
// f.maxPaletas a la vez. `mapas[i]` son los mapas donde sale conjuntos[i], ordenados.
// Con `anteriores`, primero intenta respetarlas; `usadas` dice cuales usaba algun metatile.
bool RepartePaletas(const Formato &f, const std::vector<Colores> &conjuntos, const std::vector<std::vector<int>> &mapas,
                    int numMapas, const std::vector<Paleta> *anteriores, const std::vector<bool> &usadas,
                    Reparto &salida)
{
    std::vector<int> porTamano(conjuntos.size());
    for (size_t i = 0; i < conjuntos.size(); i++)
        porTamano[i] = i;
    std::sort(porTamano.begin(), porTamano.end(),
              [&](int a, int b) { return PorTamano(conjuntos[a], conjuntos[b]); });

    // Mapa por mapa, empezando por el que mas colores tiene.
    std::vector<std::vector<int>> deMapa(numMapas);
    std::vector<int> sinMapa;
    for (int i : porTamano) {
        for (int m : mapas[i])
            deMapa[m].push_back(i);
        if (mapas[i].empty())
            sinMapa.push_back(i);
    }
    std::vector<int> ordenMapas(numMapas);
    for (int m = 0; m < numMapas; m++)
        ordenMapas[m] = m;
    std::stable_sort(ordenMapas.begin(), ordenMapas.end(),
                     [&](int a, int b) { return deMapa[a].size() > deMapa[b].size(); });
    std::vector<int> porMapa;
    std::vector<bool> puesto(conjuntos.size(), false);
    for (int m : ordenMapas)
        for (int i : deMapa[m])
            if (!puesto[i]) {
                puesto[i] = true;
                porMapa.push_back(i);
            }
    porMapa.insert(porMapa.end(), sinMapa.begin(), sinMapa.end());

    int mapaLleno = -1;
    auto intenta = [&](const std::vector<int> &orden, bool sembrar) {
        Reparto r(f, conjuntos, mapas, numMapas);
        std::vector<int> resto;
        if (sembrar)
            r.Siembra(*anteriores, usadas, resto, orden);
        else
            resto = orden;
        for (int i : resto)
            if (!r.Coloca(i)) {
                mapaLleno = r.mapaLleno;
                return false;
            }
        salida.pals = r.pals;
        salida.carga = r.carga;
        salida.cargadas = r.cargadas;
        salida.paletaDe = r.paletaDe;
        return true;
    };
    if (anteriores && intenta(porTamano, true))
        return true;
    if (intenta(porTamano, false) || intenta(porMapa, false))
        return true;
    salida.mapaLleno = mapaLleno;
    return false;
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
    // Cada combinacion de colores, en que mapas sale.
    std::vector<Colores> conjuntos(coloresTile);
    std::sort(conjuntos.begin(), conjuntos.end());
    conjuntos.erase(std::unique(conjuntos.begin(), conjuntos.end()), conjuntos.end());
    std::vector<int> conjuntoTile(tilesColor.size());
    for (size_t i = 0; i < tilesColor.size(); i++)
        conjuntoTile[i] = std::lower_bound(conjuntos.begin(), conjuntos.end(), coloresTile[i]) - conjuntos.begin();
    std::vector<std::vector<int>> mapasConjunto(conjuntos.size());
    for (size_t li = 0; li < e.layouts.size(); li++) {
        std::set<int> enMapa;
        for (auto *lista : {&casillas[li], &bordes[li]})
            for (const Casilla &c : *lista) {
                int pref;
                int n = numero[clave(c, &pref)];
                for (int q = 0; q < 12; q++)
                    if (usoTiles[n][q] >= 0)
                        enMapa.insert(conjuntoTile[usoTiles[n][q]]);
            }
        for (int k : enMapa)
            mapasConjunto[k].push_back(li);
    }
    std::vector<bool> usadasAntes(ant.paletas.size(), false);
    for (const Metatile &m : ant.metatiles)
        for (uint32_t en : m)
            if (TileDeEntrada(en) != 0 && PaletaDeEntrada(en) < (int)usadasAntes.size())
                usadasAntes[PaletaDeEntrada(en)] = true;
    Reparto reparto(f, conjuntos, mapasConjunto, e.layouts.size());
    if (!RepartePaletas(f, conjuntos, mapasConjunto, e.layouts.size(), e.compactar ? nullptr : &ant.paletas, usadasAntes,
                        reparto)) {
        if (reparto.mapaLleno < 0) {
            std::set<Color> todos;
            for (auto &c : conjuntos)
                todos.insert(c.begin(), c.end());
            error = "Los colores no caben en las " + std::to_string(f.maxPaletasTileset) + " paletas del tileset: hay " +
                    std::to_string(conjuntos.size()) + " combinaciones distintas de colores en trozos de 8x8 y " +
                    std::to_string(todos.size()) + " colores en total";
        } else {
            int combinaciones = 0;
            std::set<Color> todos;
            for (size_t k = 0; k < conjuntos.size(); k++)
                if (std::binary_search(mapasConjunto[k].begin(), mapasConjunto[k].end(), reparto.mapaLleno)) {
                    combinaciones++;
                    todos.insert(conjuntos[k].begin(), conjuntos[k].end());
                }
            error = e.layouts[reparto.mapaLleno].nombre + ": sus colores no caben en las " + std::to_string(f.maxPaletas) +
                    " paletas que carga un mapa: hay " + std::to_string(combinaciones) +
                    " combinaciones distintas de colores en trozos de 8x8 y " + std::to_string(todos.size()) +
                    " colores en total";
        }
        return false;
    }
    std::vector<EstadoPaleta> &pals = reparto.pals;

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
        int pal = reparto.paletaDe[conjuntoTile[i]];
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
    std::vector<uint32_t> entradaTile(tilesColor.size());
    for (size_t i = 0; i < tilesColor.size(); i++) {
        if (!colocados.count(datosTile[i])) {
            while (siguienteTile < (int)tileOcupado.size() && tileOcupado[siguienteTile])
                siguienteTile++;
            coloca(siguienteTile, datosTile[i]);
        }
        std::pair<int, int> sitio = colocados[datosTile[i]];
        entradaTile[i] = sitio.first | ((sitio.second & 1) << 10) | ((sitio.second >> 1) << 11) | ((uint32_t)paletaTile[i] << 12);
    }
    if ((int)tiles.size() > f.maxTiles) {
        error = "Hacen falta " + std::to_string(tiles.size()) + " tiles y caben " + std::to_string(f.maxTiles);
        return false;
    }

    // --- Tileset ----------------------------------------------------------------------
    Tileset &ts = s.tileset;
    ts.tiles = tiles;
    for (size_t p = 0; p < pals.size(); p++) {
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
        s.est.paletasPorLayout.push_back(reparto.cargadas[li]);
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

// Las paletas que usa un metatile: las de sus entradas cuyo tile no es el 0.
std::vector<int> PaletasDeMetatile(const Metatile &m)
{
    std::vector<int> r;
    for (uint32_t e : m)
        if (TileDeEntrada(e) != 0)
            r.push_back(PaletaDeEntrada(e));
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    return r;
}

// El tileset mientras se estampa: lo que hay, que esta en uso, lo que se va metiendo y
// que paletas carga el mapa.
struct Relleno {
    const Formato &f;
    Tileset ts;
    std::vector<EstadoPaleta> pals;
    std::vector<bool> cargada;                 // paleta -> la carga el mapa
    int numCargadas = 0;
    int cargadasAntes = 0;                     // sin lo que mete la pieza
    std::vector<bool> tileUsado;
    std::set<int> tilesFijos;
    std::map<Tile, std::pair<int, int>> tiles; // datos (y volteos) -> tile y volteo
    Estampado &r;

    Relleno(const Formato &formato, const Tileset &t, const std::vector<int> &fijos, Estampado &res)
        : f(formato), ts(t), tilesFijos(fijos.begin(), fijos.end()), r(res)
    {
        int numPaletas = std::min((int)ts.paletas.size(), f.maxPaletasTileset);
        pals.assign(numPaletas, EstadoPaleta());
        for (int p = 0; p < numPaletas; p++)
            pals[p].color = ts.paletas[p];
        cargada.assign(numPaletas, false);
        tileUsado.assign(ts.tiles.size(), false);
        if (!tileUsado.empty())
            tileUsado[0] = true;
        // Un color esta en uso si algun metatile lo pinta, este o no en un mapa.
        for (const Metatile &m : ts.metatiles) {
            for (uint32_t e : m) {
                int t = TileDeEntrada(e), p = PaletaDeEntrada(e);
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

    void Carga(int p)
    {
        if (p < (int)cargada.size() && !cargada[p]) {
            cargada[p] = true;
            numCargadas++;
        }
    }

    // Cuantas de estas paletas tendria que cargar el mapa.
    int SinCargar(const std::vector<int> &paletas) const
    {
        int n = 0;
        for (int p : paletas)
            n += p < (int)cargada.size() && !cargada[p];
        return n;
    }

    // La paleta con sitio para lo que le falta de `s` que menos colores nuevos necesita,
    // entre las que carga el mapa o las que no. Con `parte`, solo si ya tiene alguno.
    int MasParecida(const Colores &s, bool cargadas, bool parte) const
    {
        int mejor = -1, mejorCoste = 99;
        for (int p = 0; p < (int)pals.size(); p++) {
            if (cargada[p] != cargadas)
                continue;
            int coste = pals[p].Coste(s);
            if (coste <= pals[p].Libres() && coste < mejorCoste && (!parte || coste < (int)s.size())) {
                mejor = p;
                mejorCoste = coste;
            }
        }
        return mejor;
    }

    // La paleta para un trozo de 8x8 con estos colores, cargandola si hace falta.
    bool ElegirPaleta(const Colores &s, int *paleta)
    {
        int n = pals.size(), pal = -1;
        bool puedeCargar = numCargadas < f.maxPaletas;
        // 1. Una que el mapa ya carga y tiene todos los colores.
        for (int p = 0; p < n && pal < 0; p++)
            if (cargada[p] && pals[p].Tiene(s, true))
                pal = p;
        // 2. Una del tileset que los tenga todos, si el mapa puede cargar una mas.
        for (int p = 0; p < n && pal < 0 && puedeCargar; p++)
            if (!cargada[p] && pals[p].Tiene(s, true))
                pal = p;
        // 3. Una que el mapa ya carga y tiene sitio para los que le faltan.
        if (pal < 0)
            pal = MasParecida(s, true, false);
        // 4. Si el mapa puede cargar otra: una del tileset que ya tenga parte de los
        //    colores, una que no use nadie, o una nueva.
        if (pal < 0 && puedeCargar) {
            pal = MasParecida(s, false, true);
            for (int p = 0; p < n && pal < 0; p++)
                if (!cargada[p] && pals[p].Vacia())
                    pal = p;
            if (pal < 0 && n < f.maxPaletasTileset) {
                pals.push_back(EstadoPaleta());
                Paleta vacia;
                vacia.fill(0);
                vacia[0] = TRANSPARENTE;
                ts.paletas.push_back(vacia);
                cargada.push_back(false);
                pal = n;
            }
            if (pal < 0)
                pal = MasParecida(s, false, false);
            if (pal >= 0 && pals[pal].Vacia())
                r.paletasNuevas++;
        }
        if (pal < 0) {
            if (!puedeCargar && cargadasAntes < f.maxPaletas)
                return Falla(SIN_HUECO_PALETAS, "el mapa carga " + std::to_string(cargadasAntes) + " paletas de las " +
                                                    std::to_string(f.maxPaletas) + " que caben y la pieza necesita mas de " +
                                                    std::to_string(f.maxPaletas - cargadasAntes) +
                                                    " nuevas: sus colores no estan en las que ya carga");
            if (!puedeCargar) {
                int mejor = -1;
                for (int p = 0; p < n; p++)
                    if (cargada[p] && (mejor < 0 || pals[p].Coste(s) - pals[p].Libres() <
                                                        pals[mejor].Coste(s) - pals[mejor].Libres()))
                        mejor = p;
                std::string detalle;
                if (mejor >= 0)
                    detalle = ": a la que mas se acerca le faltan " + std::to_string(pals[mejor].Coste(s)) +
                              " y le quedan " + std::to_string(pals[mejor].Libres()) + " huecos";
                return Falla(SIN_HUECO_PALETAS, "el mapa ya carga las " + std::to_string(f.maxPaletas) +
                                                    " paletas que caben y a ninguna le caben los " +
                                                    std::to_string(s.size()) + " colores de un trozo de 8x8 de la pieza" +
                                                    detalle);
            }
            return Falla(SIN_HUECO_PALETAS, "el tileset ya tiene sus " + std::to_string(f.maxPaletasTileset) +
                                                " paletas y ninguna tiene sitio para los " + std::to_string(s.size()) +
                                                " colores de un trozo de 8x8 de la pieza");
        }
        r.coloresNuevos += pals[pal].Coste(s);
        pals[pal].Anade(s);
        Carga(pal);
        *paleta = pal;
        return true;
    }

    // La entrada de metatile para un trozo de 8x8, metiendo lo que falte.
    bool Entrada(const TileColor &t, uint32_t *entrada)
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
        int pal;
        if (!ElegirPaleta(Colores(cs.begin(), cs.end()), &pal))
            return false;

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
        *entrada = it->second.first | ((it->second.second & 1) << 10) | ((it->second.second >> 1) << 11) |
                   ((uint32_t)pal << 12);
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

std::vector<int> PaletasDelMapa(const Tileset &ts, const std::vector<uint16_t> &bloques,
                                const std::vector<uint16_t> &borde, uint16_t mascaraId)
{
    std::vector<bool> visto(ts.metatiles.size(), false);
    std::set<int> r;
    for (auto *lista : {&bloques, &borde})
        for (uint16_t b : *lista) {
            int m = b & mascaraId;
            if (m >= (int)ts.metatiles.size() || visto[m])
                continue;
            visto[m] = true;
            for (int p : PaletasDeMetatile(ts.metatiles[m]))
                r.insert(p);
        }
    return std::vector<int>(r.begin(), r.end());
}

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

    // Metatiles por arte, sus paletas, y que numeros estan en uso en algun mapa.
    int numMetatiles = std::min(rel.ts.metatiles.size(), rel.ts.atributos.size());
    std::map<Arte, std::vector<int>> porArte;
    std::vector<std::vector<int>> paletasMetatile(numMetatiles);
    for (int m = 0; m < numMetatiles; m++) {
        porArte[ArteDeMetatile(rel.ts, m)].push_back(m);
        paletasMetatile[m] = PaletasDeMetatile(rel.ts.metatiles[m]);
    }
    std::vector<bool> enUso(numMetatiles, false);
    for (const MapaDelTileset &md : mapas)
        for (auto *lista : {&md.bloques, &md.borde})
            for (uint16_t b : *lista)
                if ((b & f.mascaraId) < numMetatiles)
                    enUso[b & f.mascaraId] = true;
    auto esHueco = [&](int m) {
        return !enUso[m] && rel.ts.atributos[m] == 0 &&
               std::all_of(rel.ts.metatiles[m].begin(), rel.ts.metatiles[m].end(), [](uint32_t e) { return e == 0; });
    };

    // El arte nuevo de las casillas que cambian.
    std::vector<std::pair<int, Arte>> cambios;
    std::vector<bool> cambia(bloques.size(), false);
    for (int cy = y0 / LADO; cy <= (y1 - 1) / LADO; cy++) {
        for (int cx = x0 / LADO; cx <= (x1 - 1) / LADO; cx++) {
            int i = cy * mapa.ancho + cx;
            int antes = bloques[i] & f.mascaraId;
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
            if (nuevo != arte) {
                cambios.push_back(std::make_pair(i, nuevo));
                cambia[i] = true;
            }
        }
    }

    // Las paletas que carga el mapa sin contar lo que se pinta encima.
    auto cargaMetatile = [&](int m) {
        if (m < numMetatiles)
            for (int p : paletasMetatile[m])
                rel.Carga(p);
    };
    for (size_t i = 0; i < bloques.size(); i++)
        if (!cambia[i])
            cargaMetatile(bloques[i] & f.mascaraId);
    for (uint16_t b : mapa.borde)
        cargaMetatile(b & f.mascaraId);
    rel.cargadasAntes = rel.numCargadas;

    for (const auto &cambio : cambios) {
        int i = cambio.first;
        const Arte &nuevo = cambio.second;
        int antes = bloques[i] & f.mascaraId;
        uint16_t colision = bloques[i] & f.mascaraColision;
        uint16_t atributos = antes < numMetatiles ? rel.ts.atributos[antes] : 0;

        // Uno que ya tenga ese arte y cuyas paletas pueda cargar el mapa: el que menos
        // haga cargar, y si puede ser con los atributos de la casilla.
        int destino = -1, mejorCargas = 0;
        auto it = porArte.find(nuevo);
        if (it != porArte.end()) {
            for (int m : it->second) {
                int cargas = rel.SinCargar(paletasMetatile[m]);
                if (rel.numCargadas + cargas > f.maxPaletas)
                    continue;
                bool mejor = destino < 0 || cargas < mejorCargas ||
                             (cargas == mejorCargas && rel.ts.atributos[m] == atributos &&
                              rel.ts.atributos[destino] != atributos);
                if (mejor) {
                    destino = m;
                    mejorCargas = cargas;
                }
            }
        }
        if (destino < 0) {
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
                paletasMetatile.resize(numMetatiles);
                enUso.push_back(false);
            }
            rel.ts.metatiles[destino] = mt;
            rel.ts.atributos[destino] = atributos;
            paletasMetatile[destino] = PaletasDeMetatile(mt);
            porArte[nuevo].push_back(destino);
            r.metatilesNuevos++;
        }
        cargaMetatile(destino);
        enUso[destino] = true;
        bloques[i] = destino | colision;
        r.casillas++;
    }

    r.paletasMapa = rel.numCargadas;
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
