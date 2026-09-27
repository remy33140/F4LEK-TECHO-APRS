// tnc.h — USB TNC bridge (TNC2 text and KISS binary)
//
// When cfg.tncProtocol is 1 (TNC2) the node acts as a modem made of text lines:
// a "SRC>DST,PATH:info" line read from USB goes out over LoRa, and every valid
// frame heard on LoRa is echoed to USB as a TNC2 line (APRSdroid: TNC-2).
//
// When cfg.tncProtocol is 2 (KISS) both directions are binary AX.25 inside KISS
// framing (APRSdroid, APRSIS32, LoRa APRS App). The framing itself lives in
// kiss.h/kiss.cpp and knows nothing about USB; the byte sink for USB is here.
//
// Reading the config is the only thing this bridge does besides moving frames,
// so a copy of the config pointer is kept (tncBindConfig).
// License: GPL-3.0

#pragma once

#include <Arduino.h>

#include "ax25.h"
#include "config.h"

// Bind the live config (call once at boot, after the config is loaded).
void tncBindConfig(DigiConfig *cfg);

// True when a TNC bridge is enabled in any form (protocol != 0). Some call sites
// only need "is the USB stream ours?"; use the two below to know which protocol.
bool tncActive();

// True only for the TNC2 text protocol (protocol 1).
bool tncTnc2Active();

// True only for the KISS protocol (protocol 2).
bool tncKissActive();

// The configured protocol (CFG_TNC_OFF / CFG_TNC_TNC2 / CFG_TNC_KISS).
uint8_t tncProtocol();

// ---- OVERRIDE DEL OPERADOR EN MODO KISS (2026-09-15) ----
//
// Con el selector en KISS, el firmware trataba "todo el trafico como ordenes del
// programa host" (`autoBeaconAllowed()` y `tncHostDriven()`), asi que los comandos
// normales por USB (baliza manual, telemetria, mensajes...) se negaban aunque NO
// hubiera ninguna app KISS hablando. Ese era un bloqueo lateral del modo KISS.
//
// Estos tres metodos abren/cierran una PAUSA (en memoria, no persistente) que
// de OTRA pequeña herramienta: el operador manda `kissoff` por USB -> el nodo
// vuelve a aceptar todas sus ordenes normales aunque KISS este puesto; con
// `kisson` (o reiniciando) el selector vuelve a mandar. La convivencia por bytes
// (0xC0 = trama KISS, el resto lineas) NO se toca: el puerto sigue sirviendo a
// una app KISS cuando la hay.
void tncKissPause();        // "kissoff": el host deja de mandar, el nodo obedece sus ordenes
void tncKissResume();       // "kisson": KISS vuelve a ser quien manda
bool tncKissPaused();

// Offer one line read from USB to the TNC2 bridge. Returns true when the line
// was a TNC frame and has been handed to the radio (the CLI/JSON parser must
// not process it). Always false in KISS mode: text lines are never frames then.
// ★ A TNC2 line is only transmitted when its SOURCE field is our own callsign
// (callsign comparison is case-insensitive, as APRS does) and that callsign is
// representable in AX.25. Anything else is dropped and logged: without this the
// host could put frames on the air under somebody else's callsign.
bool tncHandleLine(const String &line);

// Offer ONE byte read from USB, before the line protocol sees it.
// Returns true when the byte belongs to a KISS frame (the caller must not add it
// to its text line buffer). A 0xC0 byte is the tell: from then on everything is
// fed to the KISS state machine until the closing FEND, so JSON and CLI lines
// keep working while KISS is enabled (the operator must never be locked out).
bool tncHandleUsbByte(uint8_t b);

// Call once per main loop while the KISS protocol is on: abandons a frame the
// host left half-sent (see kissPoll) so it can never swallow the JSON/CLI lines
// that follow it. No-op in any other mode.
void tncUsbPoll();

// Echo a frame received over the air to the host: AX.25 binary inside KISS
// output when the KISS protocol is on, TNC2 text when the TNC2 protocol is on,
// nothing when the bridge is off. Single call site for "RF -> host".
void tncOutputFrame(const char *frame);

// Addresses of the frame the builder just encoded, so the caller can leave its
// own audit line in the trip log ("TNC TX EA2OY-7>APZFKT,WIDE1-1 45b usb47").
struct TncFrameInfo {
  char src[16];
  char dst[16];
  char path[AX25_MAX_PATH_LEN];
  int axLen;  // bytes of the AX.25 frame (without the KISS framing)
};

// Build the KISS encoding (FEND, command byte, escaped AX.25, FEND) of one
// "SRC>DST,PATH:info" text frame into out. Returns the number of bytes, or 0
// when the frame cannot be represented in AX.25 (callsign too long, SSID out of
// range, does not fit outMax): the reason is logged and counted (tncKissLastDrop).
// info is optional (nullptr when the caller does not need the addresses).
// Shared by the USB and the Bluetooth sink: the text -> AX.25 -> KISS conversion
// exists once, in tnc.cpp.
size_t tncBuildKissFrame(const char *textFrame, uint8_t *out, size_t outMax,
                         TncFrameInfo *info = nullptr);

// One complete AX.25 frame arriving from a HOST link (USB KISS or Bluetooth):
// convert it to the "SRC>DST,PATH:info" text the radio speaks and transmit it
// with the same aprsSendTextFrame() path as every other frame. Returns the radio
// code, or -110 when the frame cannot be represented or the radio refuses it.
// The USB path only calls this while the TNC selector is on KISS; Bluetooth is a
// second host link, so it may call it with the selector in any position.
int16_t tncSendHostFrame(const uint8_t *frame, size_t len);

// Send one frame coming from the host over the air, using the same
// aprsSendTextFrame() path as every other frame. textLine is the
// "SRC>DST,PATH:info" form (converted by ax25ToTextLine). Returns the radio
// code, or -110 when KISS is off or the frame cannot be represented in AX.25.
int16_t tncKissSendFrame(const char *textLine);

// Why the last REPLAYED frame was dropped (AX25_ERR_*, or 0 when the last one
// went out). The RF -> KISS direction used to fail silently: a frame that could
// not be represented simply disappeared. Now every drop is logged
// ("TNC drop ..." / diag note) and counted, so "the node never produced output"
// can be told apart from "the host did not see it".
int tncKissLastDrop();

// Bytes actually written to the USB port as KISS frames since boot. When frames
// keep arriving and this stays at 0, nothing was replayed (config, parse or a
// refused write), which is the difference between a node-side and a host-side
// problem.
uint32_t tncKissUsbTxBytes();

// KISS frames replayed to the host (USB) since boot.
uint32_t tncKissFramesOut();
