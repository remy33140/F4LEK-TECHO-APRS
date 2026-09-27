// pins_board.h — punto UNICO donde se elige el fichero de pines de la placa.
//
// Cada placa declara en su propio fichero lo que el resto del firmware necesita
// saber de ella (pines de radio, bateria, GPS...):
//   - `pins_techo.h`    -> LilyGO T-Echo y T-Echo Plus (SX1262)
//
// ★ 2026-09-27: firmware dedicado al T-Echo (SOTA). Las Faketec / ProMicro
//   (pins_faketec.h, OLED, E22P) se han quitado del arbol: el T-Echo es la
//   UNICA placa. El define se sigue exigiendo para que un entorno mal
//   configurado no compile con los pines equivocados.
//
// License: GPL-3.0

#pragma once

#if defined(FAKETEC_BOARD_TECHO)
#include "pins_techo.h"
#else
#error "No hay placa elegida: falta -DFAKETEC_BOARD_TECHO en platformio.ini"
#endif
