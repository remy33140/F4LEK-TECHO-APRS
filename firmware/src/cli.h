// cli.h — NavaCLI-style token commands, shared by USB, Bluetooth and RF-remote (N-10).
// A line NOT starting with '{' on USB-CDC (or on the Bluetooth link) is token mode (text
// replies); the RF remote-control path uses the same engine (CliOrigen::Rf).
//
// ★★ DE DONDE VIENE LA ORDEN, Y QUE CAMBIA ESO (2026-09-17) ★★
// El motor es el mismo para los tres caminos, pero NO todos pueden hacer lo mismo:
//   Usb -> el cable: lo puede todo.
//   Ble -> el Bluetooth: lo mismo que el cable, MENOS el modo grabacion. `dfu confirm`
//          reinicia el nodo en el cargador UF2, y el cargador solo se maneja con el cable
//          puesto: por el aire el nodo se quedaria fuera de juego sin forma de terminar. Se
//          RECHAZA con un motivo claro. `wipe` (borrado total + reinicio) se rechaza igual.
//   Rf  -> un mensaje APRS por radio: lo minimo, para no inundar el canal. Sigue como estaba
//          (aqui estaba el antiguo `viaRemote=true`).
//
// Destructive policy (operator-approved):
//   reboot / reset  -> allowed (remote too) but require "confirm"; both do a
//                      SOFT reboot, config intact ("reset" used to wipe: it no
//                      longer does, see factory_reset below)
//   factory_reset   -> factory defaults PRESERVING callsign+managers+remoteEnabled
//   wipe            -> USB only + confirm (full erase; node unreachable after)
// License: GPL-3.0

#pragma once

#include <Arduino.h>

#include "config.h"
#include "usb_lector.h"  // Origen (Usb / Ble), compartido con el receptor de lineas

// Por donde ha entrado la orden.
enum class CliOrigen : uint8_t {
  Usb = 0,  // el cable
  Ble = 1,  // el enlace Bluetooth
  Rf = 2,   // un mensaje APRS por radio (control remoto)
};

// Traduce el origen del transporte al del CLI. Son dos enums distintos a proposito: el
// transporte no tiene por que saber que existe el control remoto por radio, ni el CLI que
// existe un segundo puerto serie.
inline CliOrigen cliOrigenDe(Origen o) {
  return (o == Origen::Ble) ? CliOrigen::Ble : CliOrigen::Usb;
}

// Execute a token command line. Returns the text reply (may be multi-line).
String cliExecute(DigiConfig &cfg, const char *line, CliOrigen origen);

// Atajo para el camino de radio (era `viaRemote=true`).
inline String cliExecuteRemoto(DigiConfig &cfg, const char *line) {
  return cliExecute(cfg, line, CliOrigen::Rf);
}

// Envoltorio pubico del motor de tipeado/validacion de valor por clave (typedSet es
// interno a cli.cpp). Lo usa el menu en pantalla de la tinta electronica: valida el
// valor con el MISMO criterio que `set <clave> <valor>` y lo deja en cfg. Devuelve
// true si entro bien. La persistencia a flash (storeSave) la hace quien llama.
bool cliTypedSet(DigiConfig &cfg, const String &key, const String &val, String &errOut);
