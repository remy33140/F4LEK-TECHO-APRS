// src\diag_sd_main.cpp — RELEVO del banco de pruebas del SoftDevice. NO ES FIRMWARE DEL NODO.
//
// ★★★ POR QUE ESTE FICHERO EXISTE, SIENDO SOLO DOS LINEAS ★★★
// PlatformIO **solo compila lo que esta bajo `src\`**: su comprobacion es literal
// ("Nothing to build. Please put your source code files to the 'src' folder",
// `platformio\builder\tools\piobuild.py:184`) y ocurre ANTES de que se miren las librerias,
// asi que no hay ninguna opcion de `platformio.ini` que le diga "compila esta otra carpeta".
// Comprobado a la mala el 2026-09-17: con el codigo en `diag_sd\` el entorno contesta
// "Nothing to build" y no compila nada.
//
// LA SOLUCION, CON LA PROTECCION PUESTA: el codigo de verdad vive FUERA de `src\`
// (`..\diag_sd\diag_sd_main.h`, con `..\diag_sd\ble_rastro.h`), y este fichero es solo el
// relevo que PlatformIO necesita. Para que no pueda colarse en el firmware del nodo:
//   1) los CUATRO entornos de release lo EXCLUYEN a proposito (ver `src_filter` /
//      `build_src_filter` en `platformio.ini`), y
//   2) si alguien anadiera un entorno y se le olvidara excluirlo, el enlazador se quejaria en
//      el acto (este fichero define `setup()` y `loop()`, que ya define `src\main.cpp`).
//      Se rompe la compilacion, no el nodo.
//
// License: GPL-3.0

#include "../diag_sd/diag_sd_main.h"
