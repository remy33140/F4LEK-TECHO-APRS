// pins_techo.h — LilyGO T-Echo y T-Echo Plus (nRF52840 + SX1262) pin map.
//
// FUENTE DE ESTOS PINES (no inventados): el `pinout.h` de cfr34k/t-echo-lora-aprs,
// firmware que FUNCIONA en este hardware, contrastado con la tabla oficial de
// LilyGO y con las variantes de Meshtastic. Los tres coinciden. El detalle, con
// las fuentes, esta en `docs/HARDWARE_TECHO.md`.
//
// OJO, DOS COSAS QUE ENGAÑAN SI VIENEN DE OTRA PLACA:
//   1. LilyGO usa el board generico del core (pca10056), asi que el mapeo de pines
//      del core es IDENTIDAD: pin Arduino N = P0.N, y 32+N = P1.N. Por eso los
//      defines de abajo son los numeros de GPIO directos.
//   2. La pantalla NO va por el mismo bus SPI que la radio: radio en SPI0
//      (0.19/0.22/0.23) y pantalla en SPI1 (0.31/0.29). Y aqui NO hay pin de RXEN.

#pragma once

#include <Arduino.h>

// Valor para decir "aqui no hay pin" (equivale al RADIOLIB_NC de RadioLib, pero
// sin obligar a incluir RadioLib en los ficheros que solo quieren los pines).
#ifndef NO_PIN
#define NO_PIN (-1)
#endif

// ---------------------------------------------------------------- radio (SPI0)
// Los nombres son LOS MISMOS que usa la Faketec (SX126X_*), porque es lo que
// espera `radio.cpp`. Aqui solo cambian los numeros de pin.
#define SX126X_CS 24     // P0.24
#define SX126X_BUSY 17   // P0.17  <-- en las Faketec este pin es RXEN/alimentacion
#define SX126X_DIO1 20   // P0.20
#define SX126X_RESET 25  // P0.25
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
// El T-Echo NO tiene pin de conmutador de RF ni RXEN: lo hace el DIO2 interno del
// SX1262 (SX126X_DIO2_AS_RF_SWITCH).
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_TXEN RADIOLIB_NC
// Sin pin de alimentacion del modulo: aqui P0.17 es BUSY, no un enable.
#define RADIO_POWER_ENABLE_PIN NO_PIN

// Pines del bus SPI de la radio (los usa el core al hacer SPI.begin()).
#define PIN_LORA_SCK 19   // P0.19
#define PIN_LORA_MOSI 22  // P0.22
#define PIN_LORA_MISO 23  // P0.23
#define PIN_LORA_DIO3 21  // P0.21  tension del TCXO


// ------------------------------------------------------------- pantalla (SPI1)
// Tinta electronica Good Display GDEH0154D67, controlador SSD1681, 200x200.
// Toda la alimentacion de la pantalla (y del GPS, la flash, los LEDs y el BME280)
// cuelga del MOSFET de PIN_PWR_EN.
#define PIN_EPD_SCK 31    // P0.31
#define PIN_EPD_MOSI 29   // P0.29 (SDI)
#define PIN_EPD_CS 30     // P0.30
#define PIN_EPD_DC 28     // P0.28
#define PIN_EPD_RST 2     // P0.02
#define PIN_EPD_BUSY 3    // P0.03
#define PIN_EPD_PWR 43    // P1.11 alimentacion / luz de fondo

// --------------------------------------------------------------- alimentacion
// P0.12 enciende el MOSFET que da corriente a pantalla, GPS, flash, LEDs y BME280.
// SIN ESTE PIN EN ALTO, NINGUNO DE ESOS CINCO RESPONDE.
// (La variante de placa ya lo declara: por eso va con guarda, para no repetirlo.)
#ifndef PIN_PWR_EN
#define PIN_PWR_EN 12     // P0.12
#endif
// P0.13 enciende el regulador de 3,3 V (de el cuelga el modulo LoRa).
#ifndef PIN_REG_EN
#define PIN_REG_EN 13     // P0.13
#endif

// ------------------------------------------------------------------------ GPS
// Quectel L76K, NMEA por UART. No es un u-blox y NO tiene un pin de "encendido"
// como el MOSFET de las Faketec: se alimenta del MOSFET general y tiene ademas
// linea de wake up y de reset.
// ★ OJO: estos dos pines van AL REVES de como los etiquetan LilyGO y el firmware de
//   referencia, y esta COMPROBADO en una placa real (T-Echo Plus, 2026-09-14):
//   los dos firmwares los nombran desde el punto de vista DEL MODULO GPS, y el core de
//   Arduino los quiere desde el del MICROCONTROLADOR. La variante de placa
//   (variants/techo/variant.h) es la que manda para Serial1.#define PIN_GPS_PPS 36     // P1.04
#define PIN_GPS_WAKEUP 34  // P1.02
#define PIN_GPS_RESET 37   // P1.05

// ------------------------------------------------------------------------ I2C
#define PIN_I2C_SDA 26     // P0.26
#define PIN_I2C_SCL 27     // P0.27
// En este bus: BME280 0x77 (en las dos placas), PCF8563 (reloj) 0x51,
// y solo en el Plus: BHI260AP (IMU) 0x28 y DRV2605 (motor) 0x5A.

// ------------------------------------------------------- bateria (ADC y LPCOMP)
// Divisor 1/2 en P0.04 (AIN2). En las Faketec es P0.31 (AIN7): NO se puede
// reutilizar el mismo numero de AIN.
#define PIN_BATTERY_ADC 4        // P0.04 = AIN2
#define BATTERY_DIVIDER 2.0f     // el ADC ve la mitad de la tension de la bateria
#define BATTERY_LPCOMP_AIN 2     // AIN2 = P0.04 (en la Faketec es AIN7)

// -------------------------------------------------------- botones y LEDs
#define PIN_BUTTON 42        // P1.10  boton de usuario (activo a nivel bajo)
#define PIN_BTN_TOUCH 11     // P0.11  boton tactil capacitivo (activo a nivel alto)
// P0.18 es el RESET del chip: no se puede usar para nada mas.
#define PIN_LED_BLUE 14      // P0.14
#define PIN_LED_RED 35       // P1.03
#define PIN_LED_GREEN 33     // P1.01

// --------------------------------------------------------------- GPS (extra)
// El GPS NO tiene MOSFET propio como en la Faketec: se alimenta del MOSFET
// general (PIN_PWR_EN), asi que aqui no hay pin de encendido. El core de Arduino
// ignora sin mas un pinMode() con un numero fuera de rango, asi que poner -1 es
// seguro y deja el codigo de gps.cpp tal cual, sin ramas nuevas.
#define PIN_GPS_EN NO_PIN

// ------------------------------------------ solo en el T-Echo Plus (TEchoPlus)
#ifdef TEchoPlus
#define PIN_BUZZER 6      // P0.06  zumbador
#define PIN_DRV_EN 8      // P0.08  habilita el motor haptico DRV2605 (I2C 0x5A)
#endif

// --------------------------------------------------------- valores de bateria
// Celda de litio de 1S (la placa es de 3,7 V). Los umbrales de las Faketec
// (3400/3710 mV) NO valen aqui: en una LiPo 3400 mV es casi el final util, y
// 3710 mV es mas de media carga.
//   - umbral de apagado: 3200 mV (la curva de cfr34k da 3 % a 3200 mV)
//   - umbral de despertar: 3400 mV
// Son valores DE ARRANQUE para probar; se ajustan desde el configurador.
#define TECHO_SLEEP_CUT_MV 3200
#define TECHO_SLEEP_WAKE_MV 3400
