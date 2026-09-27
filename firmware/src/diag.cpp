// diag.cpp — real-time diagnostics stream. See diag.h.
// Output is line-based JSON, one object per line (JSONL), so it can be logged
// and post-analysed without breaking the WebSerial protocol/CLI replies.
// License: GPL-3.0

#include "diag.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "aprs.h"
#include "display.h"
#include "gps.h"
#include "power.h"
#include "protocol.h"   // protocolHostOut(): el diagnostico sale por donde entro la orden
#include "radio.h"
#include "sensors.h"
#include "tnc.h"
#include "tracker.h"

namespace {

DigiConfig *gCfg = nullptr;
bool gActive = false;
bool gNmea = false;
uint32_t gLastSnapMs = 0;

void jsonEscapePrint(const char *s) {
  for (const char *p = s; p && *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') Serial.print('\\');
    if ((uint8_t)c >= 0x20) Serial.print(c);
  }
}

// La misma limpieza, pero a un buzon: hace falta porque ahora la nota sale ENTERA por una
// sola puerta (protocolHostOut), y esa puerta no puede ir imprimiendo trozos.
// Limpieza de una cadena para meterla en una linea JSON. Antes se imprimia trozo a trozo
// directamente al puerto; ahora las lineas de diagnostico salen ENTERAS por una sola puerta
// (protocolHostOut) para que puedan ir por el cable o por el aire, y esa puerta no puede ir
// imprimiendo trozos: por eso se compone en un buzon.
void jsonEscape(const char *s, char *out, size_t n) {
  size_t i = 0;
  for (const char *p = s; p && *p && i + 2 < n; p++) {
    const char c = *p;
    if (c == '"' || c == '\\') out[i++] = '\\';
    if ((uint8_t)c >= 0x20) out[i++] = c;
  }
  out[i] = '\0';
}

}  // namespace

void diagBindConfig(DigiConfig *cfg) { gCfg = cfg; }

void diagSetActive(bool on) { gActive = on; }
bool diagActive() { return gActive; }
void diagSetNmea(bool on) { gNmea = on; }
bool diagNmea() { return gNmea; }

bool diagStreaming() { return gActive && !tncActive(); }

// ★ Las dos preguntas de las trazas de taller (el porque, largo y tendido, en diag.h):
//   - `diagTrazaTaller()`   = la MISMA condicion que el flujo de diagnostico: encendido
//     por el usuario y fuera de modo TNC.
//   - `diagTrazaArranque()` = solo mira el TNC: las lineas del arranque tienen que verse
//     siempre (el diagnostico esta apagado al arrancar), pero nunca si el puerto es de un
//     programa host.
bool diagTrazaTaller() { return diagStreaming(); }
bool diagTrazaArranque() { return !tncActive(); }

void diagRxFrame(const char *from, const char *info, float rssi, float snr) {
  if (!diagStreaming()) return;
  // Las tres lineas de diagnostico salen por la MISMA puerta que las respuestas del protocolo
  // (ver diagNote): por el cable si la orden vino del cable, por el aire si vino del Bluetooth.
  char eFrom[64], eInfo[160];
  jsonEscape(from, eFrom, sizeof(eFrom));
  jsonEscape(info, eInfo, sizeof(eInfo));
  char b[288];
  snprintf(b, sizeof(b),
           "{\"diag\":\"rx\",\"ms\":%lu,\"from\":\"%s\",\"rssi\":%.0f,\"snr\":%.1f,"
           "\"info\":\"%s\"}",
           (unsigned long)millis(), eFrom, (double)rssi, (double)snr, eInfo);
  protocolHostOut(b);
}

void diagTxFrame(const char *frame, size_t len, int code) {
  if (!diagStreaming()) return;
  char eFrame[200];
  jsonEscape(frame, eFrame, sizeof(eFrame));
  char b[256];
  snprintf(b, sizeof(b),
           "{\"diag\":\"tx\",\"ms\":%lu,\"code\":%d,\"len\":%u,\"frame\":\"%s\"}",
           (unsigned long)millis(), code, (unsigned)len, eFrame);
  protocolHostOut(b);
}

void diagNote(const char *what) {
  if (!diagStreaming()) return;
  // ★ Sale por la MISMA puerta que las respuestas del protocolo (protocolHostOut), que manda
  //   por donde vino la ultima orden: si el diagnostico se encendio desde la app por
  //   Bluetooth, las notas van por el aire; si se encendio por el cable, por el cable. Nunca
  //   por los dos a la vez, que romperia la regla de "una respuesta por linea".
  char esc[128];
  jsonEscape(what, esc, sizeof(esc));
  char b[176];
  snprintf(b, sizeof(b), "{\"diag\":\"note\",\"what\":\"%s\"}", esc);
  protocolHostOut(b);
}

namespace {

void emitSnapshot() {
  const GpsData &g = gpsGet();
  const TrackerDiag td = trackerDiag(*gCfg);

  JsonDocument d;
  d["diag"] = "snap";
  d["ms"] = millis();
  d["mode"] = gCfg->mode;
  d["tnc"] = gCfg->tncProtocol;  // 0 off / 1 TNC2 / 2 KISS
  // KISS health: frames replayed to the host, bytes that really left through USB
  // and the error code of the last drop. A listener that prints nothing while
  // kiss.out stays at 0 points at the node; if out/bytes grow, the node did its
  // part and the missing frames are a host-side problem.
  JsonObject kiss = d["kiss"].to<JsonObject>();
  kiss["out"] = tncKissFramesOut();
  kiss["bytes"] = tncKissUsbTxBytes();
  kiss["lastErr"] = tncKissLastDrop();
  // 2026-09-13 RESCATE: the "ble" object of the snapshot (on/state/adv/conn/
  // ready/name/in/out/bytes/drop/pairReq/pairOk/pairFail/pairStatus) is gone
  // with the Bluetooth code: there is no stack to report on any more. The
  // config fields bleEnabled/blePin stay in the JSON, inert and unread.
  d["call"] = gCfg->callsign;
  d["bcnMin"] = gCfg->beaconIntervalMin;

  JsonObject gps = d["gps"].to<JsonObject>();
  gps["on"] = gpsPowered();
  gps["fix"] = g.fix;
  gps["sats"] = g.sats;
  gps["hdop"] = g.hdop;
  gps["lat"] = g.lat;
  gps["lon"] = g.lon;
  gps["spd"] = g.speedKmh;
  gps["crs"] = g.courseDeg;
  gps["alt"] = g.altM;
  gps["ageMs"] = g.fix ? (millis() - g.lastFixMs) : 0;
  gps["inView"] = g.satsInView;  // satellites seen (works with no fix)
  gps["snr"] = g.bestSnr;
  if (g.timeValid) {
    char t[12];
    snprintf(t, sizeof(t), "%02u:%02u:%02u", g.utcH, g.utcM, g.utcS);
    gps["utc"] = t;
  }

  JsonObject trk = d["trk"].to<JsonObject>();
  trk["first"] = td.firstFix;
  trk["mv"] = td.moving;
  trk["lastBcnMs"] = td.lastBeaconMs;
  trk["nextInMs"] = td.nextBeaconMs;
  trk["cornerMs"] = td.lastCornerMs;

  JsonObject pw = d["pwr"].to<JsonObject>();
  pw["mv"] = powerReadMv();
  pw["usb"] = powerUsbPresent();
  pw["low"] = powerLowCount();
  pw["cut"] = gCfg->sleepCutMv;
  pw["wake"] = gCfg->sleepWakeMv;

  JsonObject rd = d["rdo"].to<JsonObject>();
  rd["rssi"] = radioLastRssi();
  rd["snr"] = radioLastSnr();
  rd["fErr"] = radioLastFreqErr();
  rd["crc"] = radioCrcErrCount();
  rd["rx"] = radioRxCount();
  rd["tx"] = radioTxCount();
  rd["dg"] = aprsDigiCount();
  rd["from"] = aprsLastFrom();

  JsonObject se = d["sens"].to<JsonObject>();
  se["wx"] = gSensorCache.wxOk;
  se["temp"] = gSensorCache.tempC;
  se["ctemp"] = gSensorCache.chipTempC;  // raw die temperature (offset aside)
  se["hum"] = gSensorCache.hum;
  se["hpa"] = gSensorCache.pressHpa;
  se["vbat"] = gSensorCache.vbatDivV;
  se["ima"] = gSensorCache.inaCurrentMa;

  JsonObject ui = d["ui"].to<JsonObject>();
  ui["on"] = displayIsOn();
  ui["menu"] = menuIsOpen();

  // ★ Sale por la misma puerta que las respuestas: si el diagnostico se encendio desde la app
  //   por Bluetooth, las muestras van por el aire; si se encendio por el cable, por el cable.
  String out;
  serializeJson(d, out);
  protocolHostOut(out.c_str());
}

}  // namespace

void diagLoop() {
  // Keep the NMEA echo in sync with the effective stream state.
  gpsSetRawEcho(gNmea && diagStreaming());
  if (!diagStreaming()) return;

  uint32_t now = millis();
  if (now - gLastSnapMs < 1000) return;
  gLastSnapMs = now;
  emitSnapshot();
}
