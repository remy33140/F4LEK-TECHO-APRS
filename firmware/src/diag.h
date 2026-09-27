// diag.h — real-time diagnostics stream over USB (runtime only, never
// persisted). When enabled, the node emits one JSON snapshot per second with
// everything it is doing (GPS, tracker, power, radio, sensors, UI) plus
// event lines for each received/transmitted frame. Optional raw NMEA echo.
// It never changes the node behaviour and it is automatically muted while the
// USB TNC bridge is active (the TNC stream must stay clean).
// License: GPL-3.0
#pragma once

#include <Arduino.h>

#include "config.h"

// Bind the live config (call once at boot).
void diagBindConfig(DigiConfig *cfg);

// Runtime toggles (OFF at every boot).
void diagSetActive(bool on);
bool diagActive();
void diagSetNmea(bool on);
bool diagNmea();

// Effective stream state: enabled by the user and not in TNC mode.
bool diagStreaming();

// ---------------------------------------------------------------------------
//  ★★ LAS TRAZAS DE TALLER, AGRUPADAS EN EL MODO DIAGNOSTICO (2026-09-16) ★★
//
//  QUE PROBLEMA RESUELVE: el nodo soltaba trazas de texto por el USB cuando le daba la
//  gana (una por cada repintado de la tinta, otra por cada cambio de diapositiva, las del
//  menu, las de "fijar coords"...). En el configurador eso llena la consola, y en modo TNC
//  (KISS o TNC2) el puerto es del programa host y NO se le puede meter ni una linea.
//  Peticion del operador: que se puedan encender y apagar, que se sepa que estan agrupadas
//  ahi, que se pueda desde el configurador, y que no rompa el TNC.
//
//  SON DOS PREGUNTAS DISTINTAS, y por eso hay dos funciones:
//
//   1) `diagTrazaTaller()`  -> trazas que se REPITEN mientras el nodo trabaja (repintados,
//      cambios de escena, menu, fijar coords). Piden el MODO DIAGNOSTICO ENCENDIDO.
//      Es la MISMA condicion que `diagStreaming()`: un solo interruptor para todo lo que
//      es diagnostico, que es justo lo que se pidio.
//
//   2) `diagTrazaArranque()` -> el saludo del arranque (version, config, radio, sensores,
//      traza de la pantalla). NO puede depender del modo diagnostico: al arrancar esta
//      APAGADO siempre, asi que esas lineas no se verian nunca. Solo se callan si el
//      puerto lo va a usar un programa host (TNC), que es cuando de verdad molestan.
//
//  En los dos casos, en modo TNC no sale nada: el puerto es del host.
// ---------------------------------------------------------------------------
bool diagTrazaTaller();
bool diagTrazaArranque();

// Call from loop(): emits the 1 Hz snapshot when streaming.
void diagLoop();

// Event hooks (no-op unless streaming).
void diagRxFrame(const char *from, const char *info, float rssi, float snr);
void diagTxFrame(const char *frame, size_t len, int code);
void diagNote(const char *what);
