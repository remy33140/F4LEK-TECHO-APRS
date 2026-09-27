/*
  LilyGO T-Echo / T-Echo Plus (nRF52840 + SX1262) variant — Kacho System.
  Pin sources (three, all agreeing): cfr34k/t-echo-lora-aprs `config/pinout.h`
  (firmware that runs on this board), the official LilyGO T-Echo README, and the
  Meshtastic variant `variants/nrf52840/t-echo`. See docs/HARDWARE_TECHO.md.
  License: GPL-3.0 (project) / LGPL-2.1 boilerplate from Adafruit core lineage.
*/

#ifndef _VARIANT_TECHO_
#define _VARIANT_TECHO_

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

// El T-Echo LLEVA cristal de 32 kHz (lo dice la variante de Meshtastic, que corre
// en esta placa: `#define USE_LFXO`). No es como la Faketec, que va con el RC.
// Importa: el sueno temporizado usa el RTC2 a 32768 Hz.
#define USE_LFXO

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*
  Tabla de pines (los numeros de Arduino son DIRECTOS: pin N = P0.N, 32+N = P1.N,
  porque LilyGO usa el board generico del core). El detalle de cada pin, con su
  fuente, esta en src/pins_techo.h y en docs/HARDWARE_TECHO.md.

  | GPIO  | Funcion              | GPIO  | Funcion                     |
  |-------|----------------------|-------|-----------------------------|
  | P0.19 | LoRa SCK             | P0.22 | LoRa MOSI                   |
  | P0.23 | LoRa MISO            | P0.24 | LoRa CS                     |
  | P0.25 | LoRa RST             | P0.17 | LoRa BUSY (NO es RXEN)      |
  | P0.20 | LoRa DIO1            | P0.21 | LoRa DIO3 (TCXO)            |
  | P0.31 | Pantalla SCK         | P0.29 | Pantalla MOSI (SDI)         |
  | P0.30 | Pantalla CS          | P0.28 | Pantalla DC                 |
  | P0.02 | Pantalla RST         | P0.03 | Pantalla BUSY               |
  | P1.11 | Pantalla alimentacion| P0.12 | MOSFET de periferia (PWR)   |
  | P0.13 | Regulador 3,3 V      | P0.26 | I2C SDA                     |
  | P0.27 | I2C SCL              | P0.04 | Bateria (AIN2)              |
  | P1.08 | GPS TX               | P1.09 | GPS RX                      |
  | P1.04 | GPS PPS              | P1.02 | GPS wake up                 |
  | P1.05 | GPS reset            | P1.10 | Boton de usuario            |
  | P0.11 | Boton tactil         | P0.14 | LED azul                    |
  | P1.03 | LED rojo             | P1.01 | LED verde                   |
  | P0.18 | RESET del chip (no se usa para otra cosa)                  |
*/

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (1)
#define NUM_ANALOG_OUTPUTS (0)

// --------------------------------------------------------------- alimentacion
// P0.12: MOSFET que da corriente a pantalla, GPS, flash, LEDs y BME280.
// P0.13: regulador de 3,3 V (de el cuelga el modulo LoRa).
// Los dos se ponen en alto en initVariant().
#define PIN_PWR_EN (0 + 12) // P0.12
#define PIN_3V3_EN (0 + 13) // P0.13 (nombre heredado: aqui es el enable del regulador)

// ---------------------------------------------------------------- bateria
// Divisor 1/2 en P0.04 (AIN2). OJO: en las Faketec es P0.31 (AIN7).
#define PIN_A0 (4) // P0.04 Battery ADC
#define BATTERY_PIN PIN_A0
static const uint8_t A0 = PIN_A0;
#define ADC_RESOLUTION 14
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER (2.0F)

// ------------------------------------------------------------------- I2C
// Un solo bus: BME280 0x77, reloj PCF8563 0x51 y (solo en el Plus) IMU 0x28
// y motor DRV2605 0x5A.
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (26) // P0.26
#define PIN_WIRE_SCL (27) // P0.27

// ------------------------------------------------------------------- LEDs
#define PIN_LED1 (0 + 14) // P0.14 LED azul (el que uso la Faketec para el latido)
#define LED_BUILTIN PIN_LED1
#define LED_BLUE PIN_LED1
#define LED_RED (32 + 3)   // P1.03
#define LED_GREEN (32 + 1) // P1.01
#define LED_STATE_ON 0     // en esta placa los LEDs se encienden a nivel bajo

// ------------------------------------------------------------------ botones
// Boton de usuario: P1.10, activo a nivel bajo. Es el equivalente al boton de la
// Faketec, asi que es el que usa nuestra maquina de estados.
#define BUTTON_PIN (32 + 10) // P1.10
#define BUTTON_ACTIVE_LOW true
#define BUTTON_ACTIVE_PULLUP true
// Boton tactil capacitivo (P0.11, activo a nivel alto). Queda declarado porque
// existe, pero nuestra interfaz no lo usa de momento.
#define PIN_BUTTON_TOUCH (0 + 11)
// P0.18 es el RESET del chip. NO se puede usar para otra cosa.

// ------------------------------------------------------------------- UART
// GPS Quectel L76K.
//
// ★ ESTOS DOS PINES VAN AL REVES DE LO QUE DICEN LA TABLA DE LILYGO Y EL FIRMWARE DE
//   REFERENCIA. COMPROBADO EN UNA PLACA REAL (T-Echo Plus, 2026-09-14):
//     - RX=P1.08 / TX=P1.09  -> el GPS no dice NADA (0 satelites a la vista)
//     - RX=P1.09 / TX=P1.08  -> funcionaba del tiron (9 a la vista, 11 usados, hora ok)
//   Los dos firmwares de referencia etiquetan esos pines DESDE EL PUNTO DE VISTA DEL
//   MODULO GPS (su TX es P1.08); el core de Arduino los quiere desde el punto de vista
//   DEL MICROCONTROLADOR, o sea al reves. De ahi la confusion.
//   SI ALGUN DIA EL GPS SE QUEDA MUDO, ESTE ES EL PRIMER SITIO DONDE MIRAR.
#define PIN_SERIAL1_RX (32 + 9) // P1.09  (por donde ENTRAN los datos del GPS)
#define PIN_SERIAL1_TX (32 + 8) // P1.08  (por donde SALEN los datos al GPS)

// -------------------------------------------------------------------- SPI
// Radio en SPI0 (el `SPI` del core). La PANTALLA DE TINTA ELECTRONICA va por OTRO bus
// fisico (SCK P0.31 / MOSI P0.29) y necesita su PROPIO periferico SPI por hardware.
//
// ★ POR QUE SE USA EL SPI POR HARDWARE (leccion del 2026-09-14): al principio la pantalla
//   se gobernaba "a mano", moviendo los pines bit a bit. El panel no respondia NUNCA
//   (se quedaba con la imagen del firmware anterior y el pin BUSY no se movia). El
//   firmware que funciona usa el periferico SPI del chip a 8 MHz. Aqui se hace igual.
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (0 + 23) // P0.23  (radio)
#define PIN_SPI_MOSI (0 + 22) // P0.22  (radio)
#define PIN_SPI_SCK (0 + 19)  // P0.19  (radio)

// NOTA (2026-09-14): la pantalla NO usa un SPI por hardware de este core. Se intento
// (`SPI1`, que existe en la libreria) y el nodo se colgaba en el arranque: se reiniciaba
// en bucle y se quedaba sin puerto serie. La pantalla se gobierna moviendo los pines a
// mano desde src/epaper_techo.cpp.

#ifdef __cplusplus
}
#endif

#endif // _VARIANT_TECHO_
