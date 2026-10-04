# Porymap

[![Actions Status](https://github.com/huderlem/porymap/workflows/Build%20Porymap/badge.svg)](https://github.com/huderlem/porymap/actions)

A map editor for the Pokémon generation 3 decompilation projects ([pokeruby][pokeruby], [pokeemerald][pokeemerald], and [pokefirered][pokefirered]).

To get started, view the full online guide here: https://huderlem.github.io/porymap/

View the [Changelog][changelog] to see what's new.

## Este fork: pintar con piezas

Este fork añade la pestaña **Piezas**, junto a Metatiles, Collision y Prefabs, para pintar los mapas con arte libre: como porytiles, pero dentro del editor y pintando directamente en el mapa.

- **Importar:** sirve cualquier PNG cuyo ancho y alto sean múltiplos de 8, sin tamaño ni paleta fijados. Puede ser una pieza suelta o una hoja entera; en la hoja se elige con el ratón qué trozo se estampa.
- **Con sus paletas:** si el PNG es indexado, con la paleta en filas de 16 colores (el 0 de cada fila, transparente) y cada trozo de 8×8 con colores de una sola fila, como una hoja hecha en Aseprite, cada fila va a una paleta del tileset con esos colores en esos índices, y los tiles guardan los índices de la imagen. Debajo de la pieza dice si se estampa «con las paletas de la imagen» o con «paletas automáticas» (y por qué).
- **Estampar:** con la pestaña abierta y el lápiz, un clic estampa la pieza en la capa elegida (baja, media o alta), en la rejilla de 8 o de 16 píxeles. Arrastrando se repite. Lo transparente de la pieza deja ver lo que había, o lo borra si se marca *Lo transparente borra*.
- **El tileset se rellena solo:** los colores, los tiles (con volteos) y los metatiles que hagan falta se añaden al tileset primario del mapa en ese momento. Si algo no cabe no se pinta nada, y se avisa de qué falta:
  - el mapa ya carga todas las paletas que caben y a ninguna le caben los colores, o la pieza necesita más paletas nuevas de las que le quedan al mapa;
  - no hay hueco para tiles;
  - no hay hueco para metatiles;
  - un trozo de 8×8 tiene más de 15 colores.
- **Comportamiento, nivel y colisión** no salen del arte. El metatile nuevo hereda el comportamiento y el nivel de la casilla, la colisión de la casilla no cambia, y todo se edita aparte, como siempre.
- **Contadores y Optimizar:** el panel muestra los tiles y metatiles en uso, las paletas que carga el mapa y las que usa el tileset. *Optimizar tileset* lo reempaqueta desde lo pintado en todos los mapas que lo usan: junta metatiles repetidos, quita los que no usa nadie, libera tiles y colores y reparte los colores para que cada mapa cargue las menos paletas posibles. Los metatiles con nombre en `metatile_labels.h` y los tiles de `tiles_fijos.txt` (junto al `tiles.png`) se respetan.
- **Capas que se ven:** oculta capas mientras la pestaña está abierta.
- **Guardar y deshacer:** el tileset se guarda con el mapa. Estampar se deshace con Ctrl+Z; optimizar no, porque guarda el tileset y sus mapas y vacía su historial.

### Camino inteligente con piezas

Una pieza de 48×48 (3×3 casillas, en el mismo orden que una selección de camino inteligente de metatiles) también es un camino inteligente. Con *Smart Paths* marcado en la barra, o con Mayús, el lápiz pinta como el camino inteligente de porymap: un bloque de 2×2 con la casilla del centro, y las casillas de alrededor que son camino cogen la pieza que les toca según sus vecinas. La pieza sustituye lo que hubiera en la capa elegida.

Una casilla es camino si su arte en esa capa es uno de los 9 trozos, así que no depende de los números de metatile: da igual lo que haya debajo, y optimizar no lo rompe.

El camino inteligente de siempre, con metatiles, sigue yendo por números. Para que optimizar no lo estropee, los metatiles de los prefabs (y los que tienen nombre) se quedan siempre en su sitio: guarda cada juego de camino como un prefab de 3×3.

### Un solo tileset por layout

Si el `fieldmap.h` del proyecto define `NUM_TILESETS_PER_LAYOUT` como 1, porymap trabaja con un solo tileset por layout:

- el primario es el único: tiene todos los tiles, metatiles y paletas (`NUM_TILES_IN_PRIMARY`, `NUM_METATILES_IN_PRIMARY` y `NUM_PALS_IN_PRIMARY`, que pueden llegar a 1024, a lo que dejen las máscaras del bloque y a 16; con paletas por mapa, `NUM_PALS_IN_PRIMARY` son las que carga cada mapa);
- no se carga ningún secundario, ni sus paletas; los `NUM_*_TOTAL` no se usan;
- desaparece el secundario de la interfaz: panel del layout, nuevo layout, nuevo tileset, configuración del proyecto y, en el editor de tilesets, los menús de importar y exportar, *Change Number of Metatiles* y *Export Metatiles Image*;
- `layouts.json` se guarda sin `secondary_tileset`.

### Paletas por mapa

Con un solo tileset por layout, el tileset puede guardar las paletas de todos sus mapas, hasta `MAX_PALS_IN_TILESET` (256 en albor), y cada mapa carga solo las que usan sus metatiles, como mucho `NUM_PALS_IN_PRIMARY` (15). Para eso, en el proyecto:

- `graphics.h` incluye todas las paletas del tileset juntas, con un solo `INCBIN_U16(".../palettes.gbapal")` que el Makefile hace con `palettes/00.pal`, `01.pal`…;
- el tileset tiene `.metatilePalettes` en `headers.h`: un byte por entrada de metatile con su paleta del tileset (`metatile_palettes.bin`). En `metatiles.bin` quedan sus 4 bits bajos.

Entonces porymap:

- carga todas las `palettes/NN.pal` de la carpeta y pinta cada tile con su paleta del tileset, pase o no de 15;
- en el editor de tilesets, y en el de paletas, se elige entre todas;
- al estampar, los colores nuevos van primero a las paletas que el mapa ya carga; luego a una del tileset que ya los tenga (el mapa la carga y aprovecha sus tiles); luego a una cargada con sitio; y si el mapa aún puede cargar otra, a una del tileset con parte de los colores, a una que no use nadie o a una nueva;
- qué paletas carga cada mapa no se guarda: sale de sus metatiles cada vez, así que no se arrastran de un mapa a otro. El contador lo dice, y se pone en rojo si un mapa pasa del máximo pintando metatiles a mano;
- guarda las paletas nuevas como `NN.pal`, quita las que sobren al optimizar y escribe `metatile_palettes.bin`. Un tileset nuevo se crea ya así.

### Animaciones

Si el tileset tiene `.animations` en `headers.h` (un `INCBIN_U32` de su `animations.bin`), la pestaña Piezas tiene **Animaciones del tileset**:

- **Importar…** pide una carpeta con los fotogramas (`00.png`, `01.png`…, todos del mismo tamaño y con 15 colores como mucho entre todos), un nombre y cuántos fotogramas del juego dura cada uno. Reserva tiles libres seguidos y una paleta para ella. Con el nombre de una que ya existe, la cambia.
- **Elegir una** la pone como pieza: su fotograma 0, mientras la vista de la pieza reproduce la animación. Al estamparlo, cada trozo igual a uno del fotograma 0 (también volteado) usa el tile animado, y en el juego se anima solo. Lo pintado antes de importarla con ese arte se anima al optimizar.
- **Quitar** deja sus tiles con el fotograma 0, sin animar.

Se guarda con el tileset. En el mapa, porymap enseña siempre el fotograma 0.

El motor es la biblioteca de `tools/mapeado` de albor, copiada en `src/lib/mapeado`: los cambios van primero allí. Necesita metatiles de triple capa. Se compila como porymap ([INSTALL.md](INSTALL.md)); las descargas de abajo son del porymap original, sin las piezas.

## Download

Windows and macOS users can download Porymap below to start using it immediately. Older versions of Porymap may be downloaded from the [Releases][releases] page.

 - [Download Porymap for Windows](https://github.com/huderlem/porymap/releases/latest/download/porymap-windows.zip).
 - [Download Porymap for macOS latest (arm)](https://github.com/huderlem/porymap/releases/latest/download/porymap-macos-latest.zip).
 - [Download Porymap for macOS 15 (intel)](https://github.com/huderlem/porymap/releases/latest/download/porymap-macos-15-intel.zip).

Linux users must compile Porymap from source.

<details>
    <summary><i>Pre-compiled builds for Linux...</i></summary>

>   If you are a Linux user and you do not want to compile Porymap from source, you may find Porymap on an external package repository like Flathub or AUR.
>   Builds installed through an external package manager are not explicitly maintained by Porymap and may be out of date.
</details>

Read [INSTALL.md](INSTALL.md) for instructions on how to compile Porymap from source.

![Porymap Preview](docsrc/manual/images/introduction/porymap-loaded-project.png)

[pokeruby]: https://github.com/pret/pokeruby
[pokeemerald]: https://github.com/pret/pokeemerald
[pokefirered]: https://github.com/pret/pokefirered
[changelog]: https://github.com/huderlem/porymap/blob/master/CHANGELOG.md
[releases]: https://github.com/huderlem/porymap/releases
