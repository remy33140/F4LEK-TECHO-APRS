// ble_kiss.h — Bluetooth LE del T-Echo: TNC KISS para APRSdroid.
//
// ★★ QUE OFRECE (2026-10-17) ★★
//   Servicio "BLE KISS TNC" (00000001-ba2a-46c9-ae49-01b0961f68bb): el mismo que usan
//      APRSdroid, el tracker de CA2RXU y los TNC Mobilinkd. El movil escribe tramas KISS en
//      00000002-... (salen por la radio) y recibe por aviso en 00000003-... las tramas que
//      oye la radio. Es SIEMPRE KISS, mire lo que mire el selector del TNC del cable.
//   Servicio de CONFIGURACION (f4e40001-7e5c-4b8a-9c1d-2a4f4c454b00, 2026-10-18, app Android):
//      lineas de texto con el MISMO protocolo que el cable (JSON y CLI). El movil escribe en
//      f4e40002-... y recibe las respuestas por aviso en f4e40003-... Mismos permisos que el
//      KISS (cifrado con MITM si hay PIN).
//   ★ El Nordic UART NO se ofrece: BLEUart de Adafruit fija sus permisos en abierto, y con el
//     PIN puesto cualquiera podria reconfigurar el nodo. Por eso el servicio de configuracion
//     es propio, hecho con BLECharacteristic (permisos a mano).
//
// ★★ POR QUE ESTUVO APAGADO, Y POR QUE YA NO HACE FALTA ★★
// Con el SoftDevice arriba, la aplicacion NO puede escribir en los registros de NVMC (la
// flash) ni de POWER: hacerlo dispara NRF_FAULT_ID_APP_MEMACC (0x1001) y el SoftDevice
// reinicia el chip. El firmware escribia el registro de viaje (flog), la configuracion y la
// ultima posicion tocando NVMC a mano, y el arranque del Bluetooth anotaba sus pasos en ese
// registro y en GPREGRET2 justo despues de `Bluefruit.begin()`: de ahi el "bucle de reinicios".
// Arreglo: la flash pasa por nvm.cpp (API del SoftDevice cuando esta arriba), y este modulo no
// toca ningun registro protegido.
//
// Se arranca al final del setup si `cfg.bleEnabled` esta encendido (menu "Bluetooth" o el
// configurador). Encenderlo despues tambien funciona; apagarlo deja de anunciar y echa al movil.
// License: GPL-3.0

#pragma once

#include <Arduino.h>

#include "config.h"

void bleBindConfig(DigiConfig *cfg);

// Arranca el stack si `cfg.bleEnabled`. Una vez, al final del setup (despues del registro).
void bleLinkInit();

// Una vez por vuelta del bucle: cobra lo que escribio el movil y obedece al ajuste on/off.
void bleLoop();

// Una trama oida por la radio ("SRC>DST,PATH:info") va al movil por el servicio KISS.
void bleOutputFrameText(const char *textFrame);

// Para dormir: deja de anunciar, echa al movil y APAGA el SoftDevice. Siempre se puede llamar.
void bleShutdown();

// El SoftDevice esta arriba (sensors.cpp lee la temperatura del chip por su API).
bool bleSoftDeviceIsp();

// Resumen de una linea para el comando `ble` del CLI.
String bleResumen();
