/*
  LilyGO T-Echo / T-Echo Plus (nRF52840) variant - Kacho System.
  Behaviour source: the Meshtastic t-echo variant (runs on this board) and
  cfr34k/t-echo-lora-aprs `periph_pwr.c`. See docs/HARDWARE_TECHO.md.
  License: GPL-3.0 (project) / LGPL-2.1 boilerplate from Adafruit core lineage.
*/

#include "variant.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

// Mapa identidad: pin de Arduino N = P0.N y 32+N = P1.N (LilyGO usa el board
// generico del core, que ya trae este mismo mapa).
const uint32_t g_ADigitalPinMap[] = {
    // P0
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    // P1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

void initVariant()
{
    // Regulador de 3,3 V: de el cuelga el modulo LoRa. Va PRIMERO, o la radio no
    // responde.
    pinMode(PIN_3V3_EN, OUTPUT);
    digitalWrite(PIN_3V3_EN, HIGH);

    // MOSFET de periferia: pantalla, GPS, flash, LEDs y BME280. Sin esto en alto,
    // ninguno de esos cinco existe para el firmware.
    pinMode(PIN_PWR_EN, OUTPUT);
    digitalWrite(PIN_PWR_EN, HIGH);

    // El boton de usuario es activo a nivel bajo: pull-up para que no flote.
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

// Se llama justo antes de apagar (System OFF): deja la placa quieta y sin consumo.
//
// ★ EL BOTON DE USUARIO YA NO DESPIERTA (2026-09-15, peticion del operador).
//   Antes este pin quedaba con SENSE_Low, o sea que el boton sacaba al chip del sueno.
//   Y eso traia un efecto feo: al despertar, el chip arranca DE CERO y el primero que
//   manda es el BOOTLOADER; si el boton sigue pulsado (lo normal: acabas de pulsarlo
//   para despertar), el bootloader entra en modo DFU, parpadea el LED rojo y NO arranca
//   la aplicacion hasta que se sale solo. El operador prefiere que despierte el boton
//   de RESET, que en esta placa esta a mano y arranca limpio.
//   Se deja el pin como entrada con pull-up (para que no flote), pero SIN SENSE.
//   Despertares que quedan: RESET (que siempre funciona) y la bateria por LPCOMP
//   (recuperacion solar, la arma power.cpp).
//
// ADEMAS, y esto es propio de esta placa: hay que SOLTAR los pines de la pantalla
// de tinta electronica. Si se quedan como salida, la pantalla sigue chupando
// corriente con el nodo apagado. Es una leccion que Meshtastic tiene escrita en su
// variant.cpp ("otherwise, there will be leakage current").
void variant_shutdown()
{
    // Entrada, con pull-up, y SENSE DESACTIVADO (no se pone el campo SENSE: queda a 0).
    const uint32_t cnf = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                         (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
                         (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
    if (BUTTON_PIN < 32) {
        NRF_P0->PIN_CNF[BUTTON_PIN] = cnf;
    } else {
        NRF_P1->PIN_CNF[BUTTON_PIN - 32] = cnf;
    }

    // Pines de la pantalla a entrada, sin pull, para que no haya fuga.
    // (Numeros directos de GPIO: ver src/pins_techo.h)
    pinMode(30, INPUT); // CS
    pinMode(28, INPUT); // DC
    pinMode(2, INPUT);  // RST
    pinMode(3, INPUT);  // BUSY

    // ★★ APAGADO DE VERDAD (2026-09-15) ★★
    // En System OFF los pines CONSERVAN su estado: lo que quede en alto sigue dando
    // corriente. Y en esta placa eso es mucho:
    //   - P0.12 (PWR_EN): MOSFET que alimenta eInk, GPS, flash, LEDs y BME280.
    //   - P1.11 (PIN_EPD_PWR): alimentacion / luz de fondo del panel.
    // Sin bajarlos, el nodo "apagado" se queda con la luz encendida, la placa
    // alimentada y gastando bateria. Medido por el operador: dormia y seguia la luz.
    // (Mapa de alimentacion: docs/HARDWARE_TECHO.md §4)
    // Va AL FINAL a proposito: primero se sueltan los pines de la pantalla (arriba) y
    // despues se corta la corriente. La imagen de tinta es bistable y se queda.
    pinMode(43, OUTPUT);          // P1.11 = alimentacion / luz de fondo
    digitalWrite(43, LOW);
    pinMode(PIN_PWR_EN, OUTPUT);  // P0.12 = MOSFET de periferia
    digitalWrite(PIN_PWR_EN, LOW);
}
