// aprs.cpp - APRS-LoRa: position beacon + digipeater RX logic
// Beacon position encoding mirrors cfr34k (MIT, t-echo) aprs.c
// encode_position_readable so CA2RXU iGates decode it identically.
// Digi logic ported from CA2RXU LoRa_APRS_iGate digi_utils.cpp (GPL-3.0),
// trimmed to same-frequency operation, modes 1/2 (no third-party/queries).
// Wire format (both TX paths): LoRa payload 0x3C 0xFF 0x01 + ASCII AX25-UI text.
// License: GPL-3.0

#include "aprs.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <ctype.h>
#include <string.h>

#include "cli.h"
#include "diag.h"
#include "display.h"
#include "flog.h"
#include "haptic.h"
#include "lastpos.h"
#include "power.h"
#include "protocol.h"   // usbDiagResumen() para la consulta ?USB? por radio
#include "radio.h"
#include "ax25.h"       // ax25ValidAddress(): la vara del indicativo

// Cuantas posiciones BUENAS hacen falta desde el encendido antes de dar por
// empezada la sesion en el registro de viaje. El operador lo pidio asi (2026-09-13)
// para que el track no arranque con una posicion de las primeras, que traen mucho
// error mientras el receptor se asienta. 5-6 es lo que se probo en un paseo real.
#define kSesionFixes 5
#include "sensors.h"
#include "store.h"
#include "tnc.h"

namespace {

// (aqui vivia `kDestFallback = "APLRG1"`: se quito el 2026-09-21, cuando el tocall dejo
//  de ser configurable. El valor unico esta en DigiConfig::kKachoSystemTocall, config.h.)

// Automatic beacons stay quiet while the KISS host drives the node (operator
// decision: the app commands, see tncHostDriven() in main.cpp). This second gate
// lives at the transmit choke point on purpose: the first one (the block in the
// main loop) missed trackerLoop() and the node still sent its first-fix beacon
// on the bench (2026-09-13, "TX TRK ... ok F" with tncProtocol = 2). Everything a
// human asks for explicitly (CLI `beacon`/`trkbeacon`, the web panel button, the
// OLED menu) goes through aprsSendManualBeacon()/trackerBeaconNow() instead, so
// it keeps working.
bool autoBeaconAllowed() { return !tncKissActive() || tncKissPaused(); }

// Identificador de dispositivo que llevan TODAS nuestras tramas (el "tocall",
// campo de destino AX.25). Es lo que aprs.fi enseña en "Dispositivo".
//
// ★★ ESTE VALOR NO VIENE DE LA CONFIGURACION (2026-09-21) ★★
//   Es una declaracion del firmware: se manda SIEMPRE la matricula de Kacho System,
//   aunque el nodo tenga otra guardada de una version anterior, y aunque alguien
//   intente cambiarla por el cable o por los menus. El porque y el detalle, en el
//   comentario de DigiConfig::kKachoSystemTocall (config.h).
//   La firma conserva el parametro `cfg` porque quien llama ya lo tiene a mano y asi
//   no hay que tocar los seis sitios que la usan; no se lee nada de el.
const char *destOf(const DigiConfig &cfg) {
  (void)cfg;
  return DigiConfig::kKachoSystemTocall;
}
constexpr uint8_t kHeardSize = 5;         // heard-stations ring (Phase E)

DigiConfig *gCfg = nullptr;
uint32_t gDigiCount = 0;
char gLastFrom[16] = "";
float gLastRssi = 0.0f;
float gLastSnr = 0.0f;
uint16_t gMsgId = 0;

// Ruta efectiva de TODO lo que transmitimos (balizas, mensajes, objetos,
// meteorología, estado, telemetría y respuestas). Hay una ruta por modo de
// trabajo (decisión del operador 2026-09-12): así se puede pedir 1 salto cuando
// el nodo solo repite (estación fija) y 2 saltos cuando rastrea o hace las dos
// cosas. Si la ruta del modo está vacía se usa "path" como respaldo.
String effectivePath(const DigiConfig &cfg) {
  const char *src = cfg.path;
  if (cfg.mode == 1 && cfg.pathTracker[0] != '\0') {
    src = cfg.pathTracker;
  } else if (cfg.mode == 2 && cfg.pathBoth[0] != '\0') {
    src = cfg.pathBoth;
  } else if (cfg.mode == 0 && cfg.pathDigi[0] != '\0') {
    src = cfg.pathDigi;
  }
  if (src == nullptr || src[0] == '\0' || strcmp(src, "0") == 0) return String();
  String p = ",";
  p += src;
  return p;
}

void put2(char *&p, int v) {
  *p++ = (char)('0' + (v / 10) % 10);
  *p++ = (char)('0' + v % 10);
}

void put3(char *&p, int v) {
  *p++ = (char)('0' + (v / 100) % 10);
  *p++ = (char)('0' + (v / 10) % 10);
  *p++ = (char)('0' + v % 10);
}

// APRS uncompressed position "DDMM.mmN/DDDMM.mmE" (truncation math identical
// to cfr34k so parsers round the same way). Manual formatting: no printf
// width warnings and zero risk of buffer overrun.
// tableId is the symbol table identifier / overlay character ("/" primary,
// "\" alternate, 0-9A-Z = alternate table with that overlay).
// amb (0..4) hides the least significant digits for privacy: the receiver sees
// the position as blurred and reports the ambiguity, exactly like a station
// that does not want to publish its exact spot.
void encodePosition(double lat, double lon, char *out, size_t maxLen,
                    char tableId = '/', int amb = 0) {
  char ns = 'N', ew = 'E';
  if (lat < 0) { lat = -lat; ns = 'S'; }
  if (lon < 0) { lon = -lon; ew = 'W'; }

  int latDeg = (int)lat;
  int lonDeg = (int)lon;
  long latFull = (long)((lat - latDeg) * 600000.0);
  long lonFull = (long)((lon - lonDeg) * 600000.0);
  int latMin = (int)(latFull / 10000);
  int lonMin = (int)(lonFull / 10000);
  int latFr = (int)((latFull / 100) % 100);
  int lonFr = (int)((lonFull / 100) % 100);

  char tmp[24];
  char *p = tmp;
  put2(p, latDeg);
  put2(p, latMin);
  *p++ = '.';
  put2(p, latFr);
  *p++ = ns;
  *p++ = tableId;
  put3(p, lonDeg);
  put2(p, lonMin);
  *p++ = '.';
  put2(p, lonFr);
  *p++ = ew;
  *p = '\0';

  // Ambiguity: blank digits from the right of each "MM.mm" group.
  // ★ EL PUNTO DECIMAL NO SE BORRA NUNCA (arreglado 2026-09-15). APRS101 cap. 6
  //   define la posicion como un campo de LONGITUD FIJA ("ddmm.hhN" = 8 y
  //   "dddmm.hhW" = 9, punto incluido) y la ambiguedad se expresa dejando en
  //   blanco los digitos MENOS significativos. Borrar tambien el punto (lo que se
  //   hacia con amb >= 4) producia una trama como "42  .  N/001  .  W" ampliada
  //   con espacios donde estaba el punto: no la parsea nadie. El operador creia
  //   difuminar su posicion y emitia basura.
  //   El maximo util es 4 (los dos digitos de los minutos y los dos decimales) y
  //   el campo sigue midiendo 8/9 caracteres con su punto en su sitio; config.cpp
  //   acota el ajuste a 0..4 y RECHAZA lo que no se puede representar.
  if (amb > 0) {
    if (amb > 4) amb = 4;  // red de seguridad: encodePosition es publica dentro del modulo
    // Latitude group: [2][3] = minutes, [5][6] = decimals.
    // Longitude group: [9][10][11] = degrees, [12][13] minutes, [15][16] dec.
    const int latIdx[4] = {6, 5, 3, 2};
    const int lonIdx[4] = {16, 15, 13, 12};
    for (int i = 0; i < amb; i++) {
      tmp[latIdx[i]] = ' ';
      tmp[lonIdx[i]] = ' ';
    }
  }

  strncpy(out, tmp, maxLen - 1);
  out[maxLen - 1] = '\0';
}

// --- dedupe: heard (sender + info tail) within 25 s (CA2RXU hash buffer) ---
constexpr uint32_t kDedupeMs = 25000;
constexpr uint8_t kDedupeSlots = 16;
struct DedupeEntry {
  uint32_t tsMs;
  uint32_t hash;
};
DedupeEntry gDedupe[kDedupeSlots];
uint8_t gDedupeNext = 0;

uint32_t fnv1a(const char *s, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

bool dedupeCheckAndInsert(const String &sender, const String &infoTail) {
  String key = sender;
  key += '|';
  key += infoTail;
  uint32_t hash = fnv1a(key.c_str(), key.length());
  uint32_t now = millis();

  for (int i = 0; i < kDedupeSlots; i++) {
    DedupeEntry &e = gDedupe[i];
    if (e.tsMs != 0 && (int32_t)(now - e.tsMs) < (int32_t)kDedupeMs) {
      if (e.hash == hash) return true;  // heard recently: drop duplicate
    } else {
      e.tsMs = 0;  // free slot
    }
  }
  gDedupe[gDedupeNext].tsMs = now;
  gDedupe[gDedupeNext].hash = hash;
  gDedupeNext = (gDedupeNext + 1) % kDedupeSlots;
  return false;
}

bool callsignValid(const char *s) {
  size_t n = strlen(s);
  if (n < 3 || n > 9) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!(isalnum(c) || c == '-' || c == '/' || c == ' ')) return false;
  }
  return true;
}

// Blacklist: space-separated callsigns, '*' wildcard (N-12)
bool isBlacklisted(const DigiConfig &cfg, const String &sender) {
  const char *p = cfg.blacklist;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') p++;
    char tmp[24];
    size_t tokLen = (size_t)(p - start);
    if (tokLen >= sizeof(tmp)) tokLen = sizeof(tmp) - 1;
    memcpy(tmp, start, tokLen);
    tmp[tokLen] = '\0';
    String tok = tmp;
    if (tok == "*") return true;
    if (tok.indexOf('*') != -1) {
      if (tok.startsWith("*") && sender.endsWith(tok.substring(1))) return true;
      if (tok.endsWith("*") &&
          sender.startsWith(tok.substring(0, tok.length() - 1))) return true;
    } else if (sender == tok) {
      return true;
    }
  }
  return false;
}

// --- path helpers (ported from CA2RXU digi_utils.cpp) ---

String cleanPath(String path) {
  static const char *terms[] = {"WIDE1*,", "WIDE2*,", "*"};
  for (const char *term : terms) {
    int index = path.indexOf(term);
    if (index != -1) path.remove(index, strlen(term));
  }
  return path;
}

// Mode 1 (WIDE1-1) / mode 2 (WIDE1-1 and WIDE2-n) same-frequency path rewrite.
// Returns "" when this station must NOT repeat.
// ★ ¿Lleva el path NUESTRO indicativo? Se compara TOKEN A TOKEN (el path son estaciones
// separadas por comas) y se ignora el asterisco final, que es justo lo que marca "ya
// repetido". Comparar la cadena entera daria falsos positivos: N0CALL-9 esta contenido en
// N0CALL-99.
bool pathHasOwn(const String &path, const String &own) {
  int i = 0;
  while (i <= (int)path.length()) {
    int comma = path.indexOf(',', i);
    String tok = (comma < 0) ? path.substring(i) : path.substring(i, comma);
    tok.trim();
    if (tok.endsWith("*")) tok.remove(tok.length() - 1);
    if (tok == own) return true;
    if (comma < 0) break;
    i = comma + 1;
  }
  return false;
}

String buildDigiPath(const DigiConfig &cfg, const String &path) {
  const String own = cfg.callsign;
  uint8_t mode = cfg.digiMode;

  // ★★ SI ESTE PAQUETE YA PASO POR NOSOTROS, NO SE REPITE (arreglado 2026-09-15) ★★
  // Un indicativo NUESTRO en el path (con asterisco o sin el) significa que este paquete ya
  // salio de este nodo: repetirlo otra vez es ensuciar el aire con un duplicado.
  // EL FALLO QUE ESTO ARREGLA: solo lo miraba la rama de WIDE1-1, y ademas miraba otra cosa
  // ("¿hay cualquier asterisco?"). La rama de WIDE2 **no miraba NADA**, asi que un paquete
  // ya repetido por nosotros entraba por ahi y se repetia OTRA VEZ, metiendo el indicativo
  // dos veces en el path. Visto en el aire el 2026-09-15:
  //     N0CALL-3>APL2OY,N0CALL-9,N0CALL-9*:@151549z...
  // Pasa IGUAL en modo repetidor solo (mode 0) y en digi+tracker (mode 2): el fallo no
  // depende del modo de trabajo, solo de digiMode, que es quien elige las ramas. Poniendolo
  // AQUI, al principio, quedan cubiertas las dos ramas y cualquier otra que se anada.
  // El duplicador (dedupeCheckAndInsert, 25 s) es otra red distinta y sigue en su sitio.
  if (pathHasOwn(path, own)) return "";

  // CA2RXU digi_utils.cpp: a mis-ordered "WIDE2-n,WIDE1-1" must not be
  // repeated (WIDE1 must always precede WIDE2 in the path).
  int w1 = path.indexOf("WIDE1-1");
  int w2 = path.indexOf("WIDE2-");
  if (w1 != -1 && w2 != -1 && w2 < w1) return "";

  if (path.indexOf("WIDE1-1") != -1 && (mode == 1 || mode == 2)) {
    if (path.indexOf("*") != -1) return "";  // already digipeated (WIDE1-1)
    String out = path;
    out.replace("WIDE1-1", own + "*");
    return out;
  }
  if (mode == 2 && path.indexOf("WIDE2-") != -1) {
    String out = cleanPath(path);
    if (out.indexOf("WIDE2-1") != -1) {
      out.replace("WIDE2-1", own + "*");
      return out;
    }
    if (out.indexOf("WIDE2-2") != -1) {
      out.replace("WIDE2-2", own + "*,WIDE2-1");
      return out;
    }
    return "";
  }
  return "";
}

// --- remote control over RF messages (shared NavaCLI engine, cli.cpp) ---

bool isManager(const DigiConfig &cfg, const String &sender) {
  const char *list = cfg.managers;
  String base = sender;
  int dash = base.indexOf('-');
  if (dash > 0) base = base.substring(0, dash);
  while (*list) {
    while (*list == ' ') list++;
    if (!*list) break;
    const char *start = list;
    while (*list && *list != ' ') list++;
    char tok[24];
    size_t tl = (size_t)(list - start);
    if (tl >= sizeof(tok)) tl = sizeof(tok) - 1;
    memcpy(tok, start, tl);
    tok[tl] = '\0';
    String tokBase = tok;
    int td = tokBase.indexOf('-');
    if (td > 0) tokBase = tokBase.substring(0, td);
    if (strcasecmp(tok, sender.c_str()) == 0 ||
        strcasecmp(tokBase.c_str(), base.c_str()) == 0) {
      return true;
    }
  }
  return false;
}

// APRS message text is "::ADDRESSEE :body"; body = after the second ':'.
String messageBody(const String &info) {
  int c2 = info.indexOf(':', 2);
  if (c2 < 0) return "";
  return info.substring(c2 + 1);
}

// Bench hook: when set, replies are captured here instead of going on the air
// (the CLI "q" verb uses it to test the query engine without a second radio).
String *gReplyCapture = nullptr;

// Reply as an APRS message addressed to `to`. bypassMute=true is used by the
// remote-control path (a manager can always unmute the node); queries use
// bypassMute=false so the mute gate silences them.
bool remoteReply(const DigiConfig &cfg, const String &to, const String &text,
                 bool bypassMute = true, bool withId = true) {
  if (gReplyCapture != nullptr) {
    *gReplyCapture = text;
    return true;
  }
  String body = text;
  if (body.length() > 214) body = body.substring(0, 214);
  if (withId) {
    // APRS message number: the peer (APRSdroid, tracker) can ack it.
    char id[8];
    snprintf(id, sizeof(id), "{%03u", (unsigned)(gMsgId++ % 1000));
    body += id;
  }
  char frame[256];
  String a = to;
  if (a.length() > 9) a = a.substring(0, 9);
  while (a.length() < 9) a += ' ';
  int n = snprintf(frame, sizeof(frame), "%s>%s%s::%s:%s", cfg.callsign, destOf(cfg),
                   effectivePath(cfg).c_str(), a.c_str(), body.c_str());
  if (n <= 0 || (size_t)n >= sizeof(frame)) return false;
  return aprsSendTextFrame(cfg, frame, (size_t)n, bypassMute) ==
         RADIOLIB_ERR_NONE;
}

// --- ?APRSH: stations heard in the last N hours -----------------------------
// Collected from the trip log lines ("2026-09-11 23:33:43 RX N0CALL-10 Bcn ..."
// or "23:33:43 RX ..." when the GPS has no date yet). The hour is compared with
// the current GPS time; with no clock the whole log window is taken as "recent",
// which is better than answering "none".
namespace {
constexpr uint8_t kHistMax = 12;
struct HeardHistory {
  char calls[kHistMax][12];
  uint8_t count = 0;
  uint32_t nowSec = 0;
  uint32_t windowSec = 8 * 3600u;
  bool haveClock = false;
};

void heardHistoryCb(const char *line, void *ctx) {
  HeardHistory *h = (HeardHistory *)ctx;
  const char *rx = strstr(line, " RX ");
  if (rx == nullptr) return;
  int hh = 0, mm = 0, ss = 0;
  if (strlen(line) >= 19 && line[4] == '-') {
    if (sscanf(line + 11, "%2d:%2d:%2d", &hh, &mm, &ss) != 3) return;
  } else if (strlen(line) >= 8 && line[2] == ':') {
    if (sscanf(line, "%2d:%2d:%2d", &hh, &mm, &ss) != 3) return;
  } else {
    return;
  }
  const uint32_t sec = (uint32_t)hh * 3600u + (uint32_t)mm * 60u + (uint32_t)ss;
  if (h->haveClock) {
    uint32_t age = (h->nowSec >= sec) ? (h->nowSec - sec)
                                      : (86400u - sec + h->nowSec);
    if (age > h->windowSec) return;
  }
  const char *p = rx + 4;
  char call[12];
  uint8_t i = 0;
  while (*p != '\0' && *p != ' ' && i < sizeof(call) - 1) call[i++] = *p++;
  call[i] = '\0';
  if (i == 0) return;
  for (uint8_t k = 0; k < h->count; k++) {
    if (strcasecmp(h->calls[k], call) == 0) return;  // already listed
  }
  if (h->count < kHistMax) {
    strncpy(h->calls[h->count], call, sizeof(h->calls[0]) - 1);
    h->calls[h->count][sizeof(h->calls[0]) - 1] = '\0';
    h->count++;
  }
}
}  // namespace

// APRS queries (N-08 + CA2RXU parity): ?APRS? ?APRSV ?APRSP ?APRSL ?APRSSR
// ?APRSD ?IGATE? plus the short aliases (H / HELP / ?) and the management
// questions ?TX=? / ?EM=? (the ON/OFF forms need the manager ACL).
void handleQuery(DigiConfig &cfg, const String &sender, const String &q,
                 bool isMgr) {
  String u = q;
  u.trim();
  u.toUpperCase();
  if (u == "?APRS?" || u == "?APRSS" || u == "H" || u == "HELP" || u == "?") {
    // La lista de consultas crecio (?USB?) y el mensaje se cortaba: 192 de sobra.
    char b[192];
    snprintf(b, sizeof(b), APP_NAME_STR " %s %s D%d | %s", APP_VERSION_STR,
             cfg.callsign, cfg.digiMode,
             "?APRSV ?APRSP ?APRSL ?APRSH ?APRSSR ?APRSD ?USB? ?TX=? ?EM=?");
    remoteReply(cfg, sender, b, false);
  } else if (u == "?USB?") {
    /* DIAGNOSTICO DEL USB POR RADIO: si el USB se ha muerto, esta es la unica forma
       de preguntarle al nodo que le pasa (pedido del operador, 2026-09-13: "tras un
       rato el nodo deja de escuchar el USB"). Devuelve los bytes recibidos, cuantos
       segundos seguidos lleva sin recibir nada y el PEOR hueco del bucle (un hueco
       de decenas de ms delata el atasco del NVMC al escribir el registro). */
    char b[96];
    usbDiagResumen(b, sizeof(b));
    remoteReply(cfg, sender, b, false);
  } else if (u == "?APRSV") {
    remoteReply(cfg, sender, APP_NAME_STR " " APP_VERSION_STR " " __DATE__, false);
  } else if (u == "?TX=?") {
    char b[48];
    snprintf(b, sizeof(b), "TX %s %udBm", cfg.txDisabled ? "OFF" : "ON",
             (unsigned)radioPowerDbm());
    remoteReply(cfg, sender, b, false);
  } else if (u == "?TX=ON" || u == "?TX=OFF") {
    if (!isMgr) {
      remoteReply(cfg, sender, "Not a manager", false);
    } else {
      DigiConfig next = cfg;
      next.txDisabled = (u == "?TX=OFF");
      JsonDocument doc;
      configToJson(next, doc.to<JsonObject>());
      String err;
      configFromJson(cfg, doc.as<JsonObjectConst>(), err);  // apply + persist
      storeSave(cfg);
      radioSetMuted(cfg.txDisabled);
      remoteReply(cfg, sender, cfg.txDisabled ? "TX OFF" : "TX ON");
    }
  } else if (u == "?EM=?") {
    remoteReply(cfg, sender, cfg.gpsEco ? "GPS eco ON" : "GPS eco OFF", false);
  } else if (u == "?EM=ON" || u == "?EM=OFF") {
    if (!isMgr) {
      remoteReply(cfg, sender, "Not a manager", false);
    } else {
      DigiConfig next = cfg;
      next.gpsEco = (u == "?EM=ON");
      JsonDocument doc;
      configToJson(next, doc.to<JsonObject>());
      String err;
      configFromJson(cfg, doc.as<JsonObjectConst>(), err);
      storeSave(cfg);
      remoteReply(cfg, sender, cfg.gpsEco ? "GPS eco ON" : "GPS eco OFF");
    }
  } else if (u == "?APRSL") {
    HeardStation hs[kHeardSize];
    uint8_t n = aprsHeardStations(hs, kHeardSize);
    if (n == 0) {
      remoteReply(cfg, sender, "No Station Listened", false);
    } else {
      String out;
      for (uint8_t i = 0; i < n; i++) {
        if (i) out += ' ';
        out += hs[i].call;
      }
      remoteReply(cfg, sender, out, false);
    }
  } else if (u.startsWith("?APRSH")) {
    // Stations heard in the last N hours (default 8), read from the trip log so
    // it survives reboots. On a plain digipeater (log off) it falls back to the
    // in-RAM ring of the last stations heard.
    int hours = 8;
    int sp = u.indexOf(' ');
    if (sp > 0) {
      int v = atoi(u.c_str() + sp + 1);
      if (v > 0) hours = v;
    }
    if (hours < 1) hours = 1;
    if (hours > 24) hours = 24;
    String out = "HEARD " + String(hours) + "h:";
    if (flogEnabled()) {
      HeardHistory h;
      h.windowSec = (uint32_t)hours * 3600u;
      const GpsData &g = gpsGet();
      h.nowSec = (uint32_t)g.utcH * 3600u + (uint32_t)g.utcM * 60u + g.utcS;
      h.haveClock = g.timeValid;
      h.count = 0;
      flogEachLine(heardHistoryCb, &h);
      for (uint8_t i = 0; i < h.count; i++) {
        out += ' ';
        out += h.calls[i];
      }
      if (h.count == 0) out += " none";
    } else {
      HeardStation hs[kHeardSize];
      uint8_t n = aprsHeardStations(hs, kHeardSize);
      if (n == 0) {
        out += " none";
      } else {
        for (uint8_t i = 0; i < n; i++) {
          out += ' ';
          out += hs[i].call;
        }
      }
    }
    // APRS messages are limited; never cut a callsign in half.
    if (out.length() > 120) out = out.substring(0, out.lastIndexOf(' '));
    remoteReply(cfg, sender, out, false);
  } else if (u == "?APRSSR") {
    char b[72];
    snprintf(b, sizeof(b), "%d dBm / %.2f dB / %.0f Hz / CRC %lu",
             (int)gLastRssi, (double)gLastSnr, (double)radioLastFreqErr(),
             (unsigned long)radioCrcErrCount());
    remoteReply(cfg, sender, b, false);
  } else if (u == "?APRSP") {
    // CA2RXU parity: reply with the QTH (the beacon is forced with "beacon").
    char b[48];
    snprintf(b, sizeof(b), "QTH %.2f %.2f", (double)cfg.latitude,
             (double)cfg.longitude);
    remoteReply(cfg, sender, b, false);
  } else if (u == "?APRSD") {
    HeardStation hs[kHeardSize];
    uint8_t n = aprsHeardStations(hs, kHeardSize);
    String out = "DIRECT:";
    for (uint8_t i = 0; i < n; i++) {
      out += " ";
      out += hs[i].call;
    }
    remoteReply(cfg, sender, out, false);
  } else if (u == "?IGATE?") {
    remoteReply(cfg, sender, "DIGI ONLY", false);
  } else {
    remoteReply(cfg, sender, "?APRS? ?APRSV ?APRSP ?APRSL ?APRSH ?APRSSR ?APRSD",
                false);
  }
}

// ---------------------------------------------------------------------------
// (aprsQueryText lives further down, outside the anonymous namespace.)

// Returns true when the message is consumed (reply sent or deliberately
// ignored); false lets the caller digipeat it (CA2RXU behaviour for messages
// addressed to us that we cannot handle).
bool handleRemoteMessage(DigiConfig &cfg, const String &sender,
                         const String &info) {
  int c2 = info.indexOf(':', 1);
  String addressee = (c2 < 0) ? info.substring(1) : info.substring(1, c2);
  addressee.trim();
  if (strcasecmp(addressee.c_str(), cfg.callsign) != 0) return false;  // not for us
  String body = (c2 < 0) ? "" : messageBody(info);

  // ★ AVISO POR VIBRACION de MENSAJE RECIBIDO (2026-09-15). Este mensaje YA es para
  //   nosotros: la direccion se ha comprobado justo arriba. Pero hay dos clases de mensaje
  //   que NO son "me han escrito" y tienen su propio aviso mas abajo:
  //     - "ack"/"rej": son ACUSES de un mensaje nuestro (aviso HAP_ACUSE);
  //     - las consultas ("?APRS?" y compania): son alguien preguntando por nosotros, y el
  //       operador las quiere distinguir porque sirven para ENCONTRAR el nodo (HAP_BUSCAR).
  {
    String qv = body;
    const int qb = qv.indexOf('{');
    if (qb >= 0) qv = qv.substring(0, qb);   // una consulta puede traer numero: "?APRS?{123"
    qv.trim();
    String qu = qv;
    qu.toUpperCase();
    const bool esAcuse = body.startsWith("ack") || body.startsWith("rej");
    const bool esConsulta = (qv.length() > 0 && qv[0] == '?') || qu == "H" || qu == "HELP";
    if (!esAcuse && !esConsulta) hapticAviso(HAP_MENSAJE);
  }

  // APRS delivery ACK: a message ending in "{id" expects ":SENDER:ack<id>".
  // Never ack an ack (loop protection).
  int brace = body.indexOf('{');
  if (brace >= 0 && !body.startsWith("ack") && !body.startsWith("rej")) {
    String id = body.substring(brace + 1);
    id.trim();
    if (id.length() > 0) {
      String ack = "ack";
      ack += id;
      remoteReply(cfg, sender, ack, true, false);  // acks carry no message id
    }
  }

  // Incoming ACK/REJ for our own messages: consume silently on the air (never
  // feed "ackNNN" to the CLI or we would ping-pong replies) but tell the
  // operator, who needs to know the message was delivered.
  if (body.startsWith("ack") || body.startsWith("rej")) {
    const bool rej = body.startsWith("rej");
    const uint16_t id = (uint16_t)atoi(body.c_str() + 3);
    if (!aprsMsgAcked(id, rej)) {
      // Not one of ours (another operator's message): just tell the operator.
      flogLine("RX %s %s%s", sender.c_str(), rej ? "REJ " : "ACK ",
               body.c_str() + 3);
      String p = rej ? "REJECTED " : "ACK ";
      p += sender;
      displayPopup(p.c_str());
    }
    return true;
  }

  // APRS queries first (no manager ACL; the ON/OFF forms check it inside).
  // Gate: "?...", or the CA2RXU-compatible aliases "H" / "HELP" / "?".
  // A query may arrive with a message number ("?APRS?{123"): the reference
  // firmware cuts at the brace before matching, and so do we now, or the exact
  // comparison below would fail and the query would be treated as a command.
  String q = body;
  const int qBrace = q.indexOf('{');
  if (qBrace >= 0) q = q.substring(0, qBrace);
  q.trim();
  String word = q;
  word.toUpperCase();
  const bool isQuery = (q.length() > 0 && q[0] == '?') || word == "H" ||
                       word == "HELP";
  if (isQuery) {
    // ★ Vibracion al recibir una CONSULTA (2026-09-15): es la forma de ENCONTRAR el nodo
    //   perdido -- se le pregunta por radio y el nodo avisa donde este. Se avisa tambien
    //   cuando las consultas estan desactivadas: entonces no contestara, pero el operador
    //   quiere enterarse igual de que alguien le esta llamando.
    hapticAviso(HAP_BUSCAR);
    if (cfg.queriesEnabled) handleQuery(cfg, sender, word, isManager(cfg, sender));
    return true;  // addressed to us: never digipeated
  }

  // Remote control (manager ACL + remoteEnabled): text from a manager IS a
  // command, and its reply is the answer.
  if (cfg.remoteEnabled && isManager(cfg, sender)) {
    String reply = cliExecuteRemoto(cfg, body.c_str());
    if (reply.length() > 0) {
      flogLine("RX %s CMD %s", sender.c_str(), body.c_str());
      remoteReply(cfg, sender, reply);
    }
    return true;
  }

  // Plain text from anybody else: show it to the operator and keep it in the
  // trip log. Returning false leaves the digipeat decision untouched (CA2RXU
  // repeats messages addressed to us that it cannot handle).
  flogLine("RX %s MSG <- %s", sender.c_str(), body.c_str());
  displayNoteMsg(sender.c_str(), body.c_str());  // guarda el mensaje para la escena "Mensajes"
  String p = "MSG ";
  p += sender;
  p += '\n';
  p += body.substring(0, 40);
  displayPopup(p.c_str());
  return false;
}
// --- heard stations ring (Phase E: ?APRSD + OLED stations scene) ---
HeardStation gHeard[kHeardSize];
uint8_t gHeardHead = 0;
uint8_t gHeardUsed = 0;

int digit(char c) { return (c >= '0' && c <= '9') ? c - '0' : -1; }

// Parse an uncompressed APRS position from an info field (types ! = / @).
// Falls back to the compressed Base91 format when the field is not
// "DDMM.mmN/DDDMM.mmE" (CA2RXU/APRS101 compressed positions).
bool parsePosition(const char *info, double &lat, double &lon) {
  size_t n = strlen(info);
  int idx = -1;
  switch (info[0]) {
    case '!':
    case '=': idx = 1; break;
    case '@':
    case '/': idx = 8; break;  // 7-char timestamp before the position
    default: return false;
  }
  if ((size_t)idx + 8 > n) return false;
  const char *p = info + idx;

  if ((p[0] >= '0' && p[0] <= '9') && (p[1] >= '0' && p[1] <= '9') &&
      p[4] == '.') {
    if ((size_t)idx + 17 > n) return false;
    if (p[8] != '/' && p[8] != '\\') return false;

    int d1 = digit(p[0]), d2 = digit(p[1]);
    int m1 = digit(p[2]), m2 = digit(p[3]), f1 = digit(p[5]), f2 = digit(p[6]);
    if (d1 < 0 || d2 < 0 || m1 < 0 || m2 < 0 || f1 < 0 || f2 < 0) return false;
    lat = d1 * 10 + d2 + (m1 * 10 + m2) / 60.0 + (f1 * 10 + f2) / 6000.0;
    if (p[7] == 'S') lat = -lat;

    int D1 = digit(p[9]), D2 = digit(p[10]), D3 = digit(p[11]);
    int M1 = digit(p[12]), M2 = digit(p[13]), F1 = digit(p[15]), F2 = digit(p[16]);
    if (D1 < 0 || D2 < 0 || D3 < 0 || M1 < 0 || M2 < 0 || F1 < 0 || F2 < 0) return false;
    lon = D1 * 100 + D2 * 10 + D3 + (M1 * 10 + M2) / 60.0 + (F1 * 10 + F2) / 6000.0;
    if (p[17] == 'W') lon = -lon;
    return true;
  }

  // Compressed Base91: symbol table char + 4 lat + 4 lon.
  if ((size_t)idx + 9 > n) return false;
  const char *q = p + 1;
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    int d = (uint8_t)q[i] - 33;
    if (d < 0 || d > 90) return false;
    v = v * 91u + (uint32_t)d;
  }
  lat = 90.0 - (double)v / 380926.0;
  v = 0;
  for (int i = 4; i < 8; i++) {
    int d = (uint8_t)q[i] - 33;
    if (d < 0 || d > 90) return false;
    v = v * 91u + (uint32_t)d;
  }
  lon = -180.0 + (double)v / 190463.0;
  return true;
}

void heardPush(const char *call, float rssi, float snr, const char *info) {
  double lat = 0.0, lon = 0.0;
  bool hasPos = parsePosition(info, lat, lon);

  for (uint8_t i = 0; i < gHeardUsed; i++) {
    if (strcmp(gHeard[i].call, call) == 0) {
      gHeard[i].rssi = rssi;
      gHeard[i].snr = snr;
      gHeard[i].ms = millis();
      if (hasPos) {
        gHeard[i].hasPos = true;
        gHeard[i].lat = lat;
        gHeard[i].lon = lon;
      }
      return;
    }
  }
  HeardStation &e = gHeard[gHeardHead];
  strncpy(e.call, call, sizeof(e.call) - 1);
  e.call[sizeof(e.call) - 1] = '\0';
  e.rssi = rssi;
  e.snr = snr;
  e.ms = millis();
  e.hasPos = hasPos;
  e.lat = lat;
  e.lon = lon;
  gHeardHead = (gHeardHead + 1) % kHeardSize;
  if (gHeardUsed < kHeardSize) gHeardUsed++;
}

void printDigiLog(const String &sender, const String &text) {
  Serial.print(F("{\"aprs\":\"digi\",\"from\":\""));
  Serial.print(sender);
  Serial.print(F("\",\"tx\":true,\"packet\":\""));
  for (size_t i = 0; i < text.length(); i++) {
    char c = text.charAt((unsigned int)i);
    if (c == '"' || c == '\\') Serial.print('\\');
    if (c >= 0x20) Serial.print(c);
  }
  Serial.println(F("\"}"));
}

}  // namespace

// Bench helper for the USB CLI ("q" verb): builds the answer to an APRS query
// without transmitting anything, so the query engine can be checked with a
// single board. Read-only: the manager is passed as false, so ?TX=ON/OFF and
// ?EM=ON/OFF answer "Not a manager" instead of changing the configuration.
String aprsQueryText(DigiConfig &cfg, const String &q) {
  String out;
  gReplyCapture = &out;
  handleQuery(cfg, "TEST", q, false);
  gReplyCapture = nullptr;
  return out;
}

// Bench helper for the USB CLI ("rxs"): build the APRS info field of a message
// ("::ADDRESSEE :body") and hand it to the same handler used for real frames.
// Bench helper for the USB CLI ("battest"): the low-battery comment exactly as
// it would go into the beacon for a given voltage running on the battery. Lets
// the warning be checked without draining a real battery (and without risking
// the low-voltage sleep). Defined further down, next to the real logic.

bool aprsInjectMessage(DigiConfig &cfg, const char *from, const char *to,
                       const char *body) {  if (from == nullptr || to == nullptr) return false;
  String info = ":";
  String a = to;
  if (a.length() > 9) a = a.substring(0, 9);
  while (a.length() < 9) a += ' ';
  info += a;
  info += ":";
  info += (body != nullptr) ? body : "";
  return handleRemoteMessage(cfg, String(from), info);
}

void aprsBindConfig(DigiConfig *cfg) { gCfg = cfg; }

uint32_t aprsDigiCount() { return gDigiCount; }

const char *aprsLastFrom() { return gLastFrom; }

namespace {

// ★ NORMA DEL FIRMWARE (2026-09-15): un indicativo INVALIDO nunca sale al aire.
// El NOCALL-11 de fabrica (y cualquier indicativo vacio, reservado o que no quepa en
// una direccion AX.25) no puede transmitir NADA: ni balizas, ni mensajes, ni
// repeticiones.
// Aqui vive el CRITERIO (un solo sitio), pero quien lo aplica de verdad es el embudo de
// envio, `aprsSendTextFrame()`: tenerlo solo en las funciones de baliza dejaba fuera al
// repetidor. Si algun dia se cambia que es un indicativo valido, se cambia AQUI.
// ★ SEGUNDA VARA (2026-09-15): ademas de no ser NOCALL, el indicativo tiene que ser
// REPRESENTABLE en AX.25 (ax25ValidAddress(): 1..6 caracteres, SSID 0..15, sin
// espacios). config.cpp ya lo exige al guardar, pero la comprobacion se repite aqui
// porque una configuracion guardada por una version anterior puede traer un
// indicativo con espacio o demasiado largo, y esas tramas salen ilegibles: mejor
// no transmitir que ensuciar el aire con algo que nadie puede leer.
// (El nombre se queda como estaba a proposito: ya se usaba en seis sitios.)
bool callsignBeaconOk(const DigiConfig &cfg) {
  if (strlen(cfg.callsign) < 3) return false;
  if (strncasecmp(cfg.callsign, "NOCALL", 6) == 0) return false;
  if (!ax25ValidAddress(cfg.callsign)) return false;
  return true;
}

// ★★ SOURCE DEL PERFIL DE USO (2026-09-15) ★★
// Si el perfil activo (1 peaton / 2 bici / 3 coche) tiene SSID propio, la baliza de
// RASTREADOR se emite con `<base>-<ssid_perfil>` (p.ej. N0CALL-7), creando track separado
// en los mapas. El perfil 0 (fijo/digipeater) usa el indicativo del nodo tal cual (su
// SSID se ignora: es la identidad del nodo). No toca el modo de trabajo.
const char *profileTxSource(const DigiConfig &cfg) {
  static char out[16];
  uint8_t p = cfg.smartBeaconPreset;
  if (p < 1 || p > 3 || cfg.profileSsid[p] < 1 || cfg.profileSsid[p] > 15) {
    snprintf(out, sizeof(out), "%s", cfg.callsign);   // sin override
  } else {
    // quitar el SSID del callsign ('-N') y poner el del perfil
    const char *dash = strchr(cfg.callsign, '-');
    int baseLen = dash ? (int)(dash - cfg.callsign) : (int)strlen(cfg.callsign);
    int n = baseLen < (int)sizeof(out) - 4 ? baseLen : (int)sizeof(out) - 4;
    memcpy(out, cfg.callsign, n);
    out[n] = '\0';
    snprintf(out + n, sizeof(out) - n, "-%u", (unsigned)cfg.profileSsid[p]);
  }
  return out;
}

// ★★ ICONO DEL MAPA POR PERFIL DE USO (2026-09-15) ★★
//
// QUE PROBLEMA RESUELVE: el icono (`symbol` + `overlay`) era UN SOLO ajuste del
// aparato, asi que cambiar de perfil cambiaba el SSID pero el icono del mapa no:
// un nodo en bici seguia saliendo con la estrella del digi. El operador pidio que
// el icono vaya CON EL PERFIL (cfg.profileSymbol[]/profileOverlay[]).
//
// PRECEDENCIA, EN UNA FRASE: **si el usuario ha tocado el icono a mano, manda el
// usuario**; si no lo ha tocado (sigue en el valor de fabrica), manda el perfil
// activo. La vara que decide es `symbol == '#'` y `overlay == '/'`, o sea
// EXACTAMENTE los valores de fabrica de config.h. Consecuencia buscada: quien
// tenga el icono configurado (web, CLI o el menu del T-Echo) no nota NINGUN
// cambio al actualizar el firmware -- su icono sigue saliendo-- y quien lo tenga
// de fabrica empieza a ver el icono de su perfil sin tocar nada.
//
// ★ POR QUE ESTA VARA Y NO UNA BANDERA NUEVA EN LA CONFIGURACION: una bandera
//   ("symbolManual") hay que mantenerla, migrarla y no se puede rellenar hacia
//   atras (una configuracion ya grabada no la trae), asi que quien tuviera un
//   icono propio lo perderia al actualizar. Con la comparacion contra el valor de
//   fabrica, TODA configuracion ya grabada se interpreta como "no lo he tocado"
//   solo si de verdad esta en el valor de fabrica, y si tiene un icono propio
//   manda el suyo. Cero migraciones y cero sorpresas.
//
// ★ Y SI EL USUARIO PONE A MANO UNO DE LOS CUATRO POR PERFIL (menu), tampoco
//   cambia nada para los demas: el ajuste manual sigue siendo el global.
//
// Los codigos por defecto y de donde salen estan documentados en config.h.
// --------------- (la funcion que decide, aqui abajo) ---------------

// Ver config.h: profileOverlay[]/profileSymbol[] son el PAR (tabla, codigo) del
// icono de cada perfil. Devuelve false si el perfil no trae icono propio (0..3
// son los cuatro perfiles, pero una configuracion manipulada podria traer otra
// cosa): en ese caso NO se inventa nada y se usa el ajuste del aparato.
bool profileIcon(const DigiConfig &cfg, uint8_t p, char &tableId, char &code) {
  if (p > 3) return false;
  const char t = cfg.profileOverlay[p][0];
  const char c = cfg.profileSymbol[p][0];
  if (t == '\0' || c == '\0') return false;
  tableId = t;
  code = c;
  return true;
}

// Resuelve el icono que va EN LA TRAMA: devuelve el par (tabla, codigo) ya
// decidido. `tableId` es el caracter que va delante del codigo en el campo de
// posicion (o el prefijo en la posicion comprimida): '/' primaria, '\' alterna.
//
// LA PRECEDENCIA, EN UNA LINEA: si el icono GLOBAL no esta en su valor de fabrica
// ('#' y '/'), manda el usuario; si esta de fabrica, manda el perfil activo.
void aprsProfileIcon(const DigiConfig &cfg, char &tableId, char &code) {
  const bool globalDeFabrica = (cfg.symbol == '#' && cfg.overlay[0] == '/');
  if (globalDeFabrica && profileIcon(cfg, cfg.smartBeaconPreset, tableId, code)) {
    return;   // nadie ha tocado el icono global: manda el del perfil activo
  }
  tableId = cfg.overlay[0] ? cfg.overlay[0] : '/';
  code = cfg.symbol;
}

}  // namespace

bool aprsCanBeacon(const DigiConfig &cfg) {
  if (!callsignBeaconOk(cfg)) return false;
  if (cfg.latitude == 0.0f && cfg.longitude == 0.0f) return false;
  return true;
}

namespace {

// Human-readable WX in the beacon comment (operator preference), e.g.
// " H37% 26.1C 969.0hPa": humidity goes first, then temperature and pressure.
// The leading space stays: it separates the comment from the symbol character in
// raw frames/text clients and keeps the one-shot banner from gluing onto it.
// Machine-readable values travel in the APRS telemetry T# channels (PARM/UNIT/
// EQNS), so aprs.fi still charts them.
// Human-readable weather text: this is what goes in the POSITION beacon comment
// (plain text, fully readable, next to the operator comment).
void appendWx(String &s, const DigiConfig &cfg, const SensorReadings &r) {
  if (!r.wxOk) return;  // no sensors: nothing to add

  float t = r.tempC + cfg.temperatureCorrectionC;
  float p = r.pressHpa + cfg.heightCorrectionM / 8.2296f;
  char buf[64];
  if (r.tempOk && r.humOk && r.pressOk) {
    snprintf(buf, sizeof(buf), " H%.0f%% %.1fC %.1fhPa", r.hum, t, p);
  } else if (r.tempOk && r.pressOk) {
    snprintf(buf, sizeof(buf), " %.1fC %.1fhPa", t, p);
  } else if (r.tempOk && r.humOk) {
    snprintf(buf, sizeof(buf), " H%.0f%% %.1fC", r.hum, t);
  } else if (r.pressOk) {
    snprintf(buf, sizeof(buf), " %.1fhPa", p);
  } else {
    snprintf(buf, sizeof(buf), " %.1fC", t);
  }
  s += buf;
}

// Standard APRS weather data block (".../...g...tTTThHHbPPPPP"). It belongs to a
// WEATHER packet, not to the comment of a position: that is why findu/aprs.fi
// ignored it before. Missing readings stay as dots (APRS101: unknown values are
// expressed as a series of dots).
size_t buildWxBlock(const DigiConfig &cfg, const SensorReadings &r, char *out,
                    size_t maxLen) {
  char tempStr[12] = "...";
  if (r.tempOk) {
    int tf = (int)lroundf((r.tempC + cfg.temperatureCorrectionC) * 9.0f / 5.0f +
                          32.0f);
    if (tf < -99) tf = -99;
    if (tf > 999) tf = 999;
    if (tf < 0) snprintf(tempStr, sizeof(tempStr), "-%02d", -tf);
    else snprintf(tempStr, sizeof(tempStr), "%03d", tf);
  }
  char humStr[12] = "..";
  if (r.humOk) {
    int hum = (int)lroundf(r.hum);
    if (hum < 0) hum = 0;
    if (hum >= 100) hum = 0;  // APRS: "00" means 100 %
    snprintf(humStr, sizeof(humStr), "%02d", hum);
  }
  char presStr[12] = ".....";
  if (r.pressOk) {
    float p = r.pressHpa + cfg.heightCorrectionM / 8.2296f;
    snprintf(presStr, sizeof(presStr), "%05d", (int)lroundf(p * 10.0f));
  }
  int n = snprintf(out, maxLen, ".../...g...t%sh%sb%s", tempStr, humStr, presStr);
  return (n > 0) ? (size_t)n : 0;
}

// Battery voltage in the beacon comment (operator request): shortened to
// " Bx.xxV". The INA219 current used to ride along as " I=+/-xxxmA" and was
// removed from the comment completely; the reading still travels in the APRS
// telemetry T# channels (PARM/UNIT/EQNS) and on the OLED/status.
void appendPower(String &s, const SensorReadings &r) {
  float bv = sensorsBatteryVolt(r);
  if (bv > 0.0f) {
    s += " B";
    s += String(bv, 2);
    s += "V";
  }
}

// Battery state text for the beacon comment (operator request 2026-09-12 (24),
// NavaTastic [Vivo] parity):
//   V <= 3500 mV       -> " BAT BAJA"          (low, still working normally)
//   V <  cut 3400/3500 -> "... DORMIR"         (the ~160 s before System OFF:
//                          the monitor needs 8 readings below the cut)
// Only without USB: with the charger connected the voltage says nothing about
// the real state (and the pack sits at ~4.2 V float anyway).
//
// Thresholds: the warning one is FIXED per module, NOT relative to sleepCutMv.
// Reason: with 3 NiMH cells (nominal 3.6 V, working range 3.5-3.6 V for most of
// the discharge) a rule like cut + 300 mV lit the warning almost always, and a
// warning that is always on warns about nothing. 3.5 V (1.17 V/cell) is
// genuinely low for NiMH and still safe for a 1S LiPo/Li-ion.
constexpr int kLowBatWarnMv = 3500;

// Returns "" when there is nothing to say, so the bench verb can reuse it.
String batteryText(const DigiConfig &cfg, float bv, bool usbPresent) {
  if (bv <= 0.0f) return "";
  if (usbPresent) return "";
  const int mv = (int)(bv * 1000.0f + 0.5f);
  String s;
  if (mv <= kLowBatWarnMv) {
    char b[28];
    snprintf(b, sizeof(b), " BAT BAJA");
    s += b;
  }
  if (mv < cfg.sleepCutMv) s += " DORMIR";
  return s;
}

void appendLowBat(String &s, const DigiConfig &cfg, const SensorReadings &r) {
  s += batteryText(cfg, sensorsBatteryVolt(r), powerUsbPresent());
}

// Chip temperature as plain text for aprs.fi. Kept ASCII on purpose: APRS
// carries 7-bit text, so the degree sign would risk breaking the frame at the
// iGate. Tint: = inside the enclosure (the die heats up on its own).
void appendChipTemp(String &s, const DigiConfig &cfg, const SensorReadings &r) {
  if (!r.chipTempOk) return;
  char buf[24];
  snprintf(buf, sizeof(buf), " Tint:%.1fC", sensorsChipTemp(r, cfg.chipTempOffsetC));
  s += buf;
}

}  // namespace

// Bench helper for the USB CLI ("battest"): the battery comment exactly as it
// would go into the beacon for a given voltage running on the battery. Lets the
// text be checked without draining a real battery (and without risking the
// low-voltage sleep).
String aprsLowBatText(const DigiConfig &cfg, int mv) {
  return batteryText(cfg, (float)mv / 1000.0f, false);
}

int aprsLowBatWarnMv() { return kLowBatWarnMv; }

size_t aprsBuildBeacon(const DigiConfig &cfg, char *out, size_t maxLen,
                       const SensorReadings &r, const char *banner) {
  if (!aprsCanBeacon(cfg)) return 0;

  // ★ ICONO DEL MAPA POR PERFIL (2026-09-15): mismo criterio que la baliza de
  //   rastreador (ver aprsProfileIcon). Aqui importa sobre todo el perfil 0
  //   (fijo/digi): su icono de fabrica es la estrella del digipeater, y quien
  //   quiera un repetidor con otro icono lo cambia en el menu sin tocar codigo.
  //   OJO: la tabla del icono va DENTRO del campo de posicion (encodePosition la
  //   escribe entre la latitud y la longitud), asi que hay que resolverla ANTES de
  //   montar la posicion: si no, la trama diria una tabla y el icono otra.
  char iconTable = cfg.overlay[0] ? cfg.overlay[0] : '/';
  char iconCode = cfg.symbol;
  aprsProfileIcon(cfg, iconTable, iconCode);

  char pos[24];
  encodePosition(cfg.latitude, cfg.longitude, pos, sizeof(pos), iconTable,
                 cfg.posAmbiguity);

  // "0" = empty path (N-12 convention); otherwise ",WIDE1-1[,WIDE2-1]".
  // effectivePath() decides it from the working mode (see its comment).
  String s;
  s.reserve(180);
  s += cfg.callsign;
  s += ">";
  s += destOf(cfg);
  s += effectivePath(cfg);
  s += ":!";
  s += pos;
  s += iconCode;

  // One-shot banner (e.g. "NODO ONLINE" / "DURMIENDO HASTA EL SOL").
  if (banner && banner[0] != '\0') {
    s += ' ';
    s += banner;
  }
  if (cfg.wxSensorActive) appendWx(s, cfg, r);
  appendChipTemp(s, cfg, r);  // inside-the-box temperature, always available
  appendLowBat(s, cfg, r);    // "BAT BAJA" when running on a low battery
  if (cfg.comment[0] != '\0') s += cfg.comment;
  if (cfg.sendBatteryTelemetry) appendPower(s, r);

  if (s.length() >= maxLen) return 0;
  strncpy(out, s.c_str(), maxLen - 1);
  out[maxLen - 1] = '\0';
  return s.length();
}

int16_t aprsSendBeacon(const DigiConfig &cfg, const char *banner, bool manual) {
  if (!manual && !autoBeaconAllowed()) return -100;  // KISS: host app commands
  if (!aprsCanBeacon(cfg)) return -100;

  // Fresh sensor snapshot (the chip temperature always rides along).
  static SensorReadings read;
  sensorsRead(read);

  char frame[256];
  size_t len = aprsBuildBeacon(cfg, frame, sizeof(frame), read, banner);
  if (len == 0) return -100;

  int16_t st = aprsSendTextFrame(cfg, frame, len);
  if (st == RADIOLIB_ERR_NONE) displayNoteTx("BEACON");
  flogLine("TX BCN %s %.5f,%.5f %ub %s", (banner && banner[0]) ? banner : "pos",
           (double)cfg.latitude, (double)cfg.longitude, (unsigned)len,
           (st == RADIOLIB_ERR_NONE) ? "ok" : "err");
  return st;
}

// Manual "send a beacon now": the CLI `beacon` verb, the web configurator button
// and the OLED menu action all come through here, so the three behave the same.
//
// ★★ NORMA DEL OPERADOR, CONFIRMADA EL 2026-09-16: UN RASTREADOR NO BALIZA SIN
//    FIJACION GPS, Y LA ORDEN A MANO SE IGNORA. ★★
//   En modo 1/2 (rastreador y digi+rastreador) esta funcion manda la posicion
//   REAL si el GPS tiene fijacion, y **si no la tiene no manda NADA y contesta
//   -110** ("sin fijacion GPS"): la orden se ignora, no se inventa una posicion.
//   Eso vale para las tres puertas (verbo `beacon` del CLI, boton del
//   configurador web, menu de la pantalla) y para la app.
//
//   ★ LA UNICA EXCEPCION ES EL DOBLE TOQUE DEL BOTON, y es a proposito: ese gesto
//     existe justo para lo contrario, para decir "manda donde estuve de verdad"
//     (peticion del operador, 2026-09-13). Va por trackerBeaconLastKnown(), que
//     llama a aprsSendTrackerBeacon() con lastKnown=true: la posicion sale de la
//     FLASH, con SU hora y con el aviso ">ULTIMA CONOCIDA". Aqui NO: por una
//     orden de la consola, del configurador o de la app no se recurre a la flash.
//
//   HISTORIA (para que nadie lo "arregle" otra vez): hasta hoy esta funcion, sin
//   fijacion, mandaba la ULTIMA CONOCIDA. Se colo el 2026-09-13 al corregir el
//   fallo de verdad de entonces (mandaba la posicion FIJA configurada en modo
//   rastreador, que es justo lo que el operador prohibio) y contradecia la tabla
//   §3 de docs/MANUAL_DE_USO.md, que dice "Modo 1 o 2 sin fijacion: NO manda
//   posicion". La norma y el manual manda; el codigo ya dice lo mismo.
//
//   En modo 0 (repetidor fijo) la baliza es la posicion configurada, porque para
//   una estacion fija ese punto ES su posicion.
int16_t aprsSendManualBeacon(const DigiConfig &cfg) {
  if (cfg.mode != 0) {
    if (gpsPowered() && gpsGet().fix) {
      // 'M' = manual, and manualAllowed: the operator asked for it explicitly, so
      // the KISS "the host app commands" rule does not silence it.
      return aprsSendTrackerBeacon(cfg, gpsGet(), 'M', nullptr, true);
    }
    return -110;  // sin fijacion: la orden se ignora (ni posicion fija ni flash)
  }
  if (!autoBeaconAllowed()) return -100;  // KISS: the host app commands
  return aprsSendBeacon(cfg, nullptr, true);
}

// One-shot notice beacon (the power banners): same anti-guess rule as the manual
// beacon, but the text must survive. In tracker modes the notice rides the
// tracker beacon when the GPS has a live fix (real position) and becomes a status
// packet when it does not, so the configured point is never published as if it
// were real. Mode 0 keeps the configured-coordinate beacon carrying the text.
// An unpowered GPS counts as "no fix": this is the state at boot, because
// powerBootCheck() announces "NODO ONLINE" before gpsInit().
int16_t aprsSendBannerBeacon(const DigiConfig &cfg, const char *text) {
  if (!callsignBeaconOk(cfg)) return -100;  // never on air as NOCALL/empty
  if (cfg.mode != 0) {
    if (gpsPowered() && gpsGet().fix) {
      return aprsSendTrackerBeacon(cfg, gpsGet(), 'M', text);
    }
    return aprsSendStatus(cfg, text);
  }
  return aprsSendBeacon(cfg, text);
}

// Positionless weather report: "_DDHHMMz" + the standard weather block. It has
// no position and no symbol, so aprs.fi / findu record the readings as coming
// from this station WITHOUT changing its digipeater/car symbol. This is the
// packet that makes weather charts appear (the old comment-embedded block was
// ignored: "no weather reports for...").
int16_t aprsSendWeather(const DigiConfig &cfg) {
  // Scheduled in the main loop and offered by the CLI/web panel; the KISS rule
  // wins here (see autoBeaconAllowed) and the CLI reports the refusal.
  if (!autoBeaconAllowed()) return -100;
  if (!cfg.wxSensorActive) return -100;
  const GpsData &g = gpsGet();
  if (!g.timeValid || !g.dateValid) return -110;  // the format needs the stamp
  static SensorReadings read;
  sensorsRead(read);
  if (!read.wxOk) return -100;

  char block[40];
  if (buildWxBlock(cfg, read, block, sizeof(block)) == 0) return -100;

  char frame[128];
  int n = snprintf(frame, sizeof(frame), "%s>%s%s:_%02u%02u%02uz%s",
                   cfg.callsign, destOf(cfg), effectivePath(cfg).c_str(),
                   (unsigned)g.utcDay, (unsigned)g.utcH, (unsigned)g.utcM,
                   block);
  if (n <= 0 || (size_t)n >= sizeof(frame)) return -100;
  int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);
  if (st == RADIOLIB_ERR_NONE) {
    displayNoteTx("WX");
    flogLine("TX WX %s", block);
  }
  return st;
}

// Message started by the operator (CLI / web / menu): respects the TX mute and
// carries a message number so the peer can acknowledge it.
// Pending message waiting for its ack (APRS101 chapter 14: resend the same
// message, with the same number, every 30 s until acknowledged).
namespace {
constexpr uint32_t kMsgRetryMs = 30000;
struct PendingMsg {
  bool active = false;
  char to[12] = "";
  char text[64] = "";
  uint16_t id = 0;
  uint8_t tries = 0;   // transmissions already made
  uint32_t nextMs = 0;
};
PendingMsg gPend;
}  // namespace

bool aprsMsgPending(char *to, size_t toLen, uint16_t *id, uint8_t *tries) {
  if (!gPend.active) return false;
  if (to && toLen) {
    strncpy(to, gPend.to, toLen - 1);
    to[toLen - 1] = '\0';
  }
  if (id) *id = gPend.id;
  if (tries) *tries = gPend.tries;
  return true;
}

// An ack (or rej) came back: is it for the message we are waiting on?
bool aprsMsgAcked(uint16_t id, bool rejected) {
  if (!gPend.active || id != gPend.id) return false;
  flogLine("RX %s %s %03u (%u intentos)", gPend.to, rejected ? "REJ" : "ACK",
           (unsigned)id, (unsigned)gPend.tries);
  String p = rejected ? "REJECTED " : "ACK ";
  p += gPend.to;
  displayPopup(p.c_str());
  // ★ Vibracion al ACUSE de un mensaje nuestro (2026-09-15): zumbido, distinto de todos los
  //   clics. Es la confirmacion de que el mensaje HA LLEGADO (o de que lo han rechazado).
  hapticAviso(HAP_ACUSE);
  displayNoteMsgEnd(gPend.to, rejected ? MSG_END_REJ : MSG_END_ACK);
  gPend.active = false;
  return true;
}

// Called from the main loop: resend while there is no ack, then give up.
void aprsMsgTick(const DigiConfig &cfg, uint32_t now) {
  if (!gPend.active) return;
  if ((int32_t)(now - gPend.nextMs) < 0) return;
  const uint8_t retries = (uint8_t)(gPend.tries - 1);  // resends already made
  if ((int)retries >= cfg.msgRetries) {
    flogLine("TX MSG SIN ACUSE %s %03u (1 envio + %u reintentos)", gPend.to,
             (unsigned)gPend.id, (unsigned)retries);
    String p = "NO ACK ";
    p += gPend.to;
    displayPopup(p.c_str());
    displayNoteMsgEnd(gPend.to, MSG_END_NOACK);
    gPend.active = false;
    return;
  }
  char addr[16];
  snprintf(addr, sizeof(addr), "%-9s", gPend.to);
  char frame[256];
  int n = snprintf(frame, sizeof(frame), "%s>%s%s::%s:%s{%03u", cfg.callsign,
                   destOf(cfg), effectivePath(cfg).c_str(), addr, gPend.text,
                   (unsigned)gPend.id);
  if (n <= 0 || (size_t)n >= sizeof(frame)) {
    gPend.active = false;
    return;
  }
  gPend.tries++;
  gPend.nextMs = now + kMsgRetryMs;
  int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);
  flogLine("TX MSG reintento %u/%u -> %s %03u (%s)", (unsigned)(gPend.tries - 1),
           (unsigned)cfg.msgRetries, gPend.to, (unsigned)gPend.id,
           (st == RADIOLIB_ERR_NONE) ? "ok" : "err");
}

int16_t aprsSendMessage(const DigiConfig &cfg, const char *to, const char *text) {
  if (to == nullptr || to[0] == '\0' || text == nullptr || text[0] == '\0') {
    return -100;
  }
  if (!callsignBeaconOk(cfg)) return -100;  // never on air without a callsign
  String t = text;
  String body = t;
  if (body.length() > 200) body = body.substring(0, 200);
  const uint16_t mid = (uint16_t)(gMsgId++ % 1000);
  char id[8];
  snprintf(id, sizeof(id), "{%03u", (unsigned)mid);
  body += id;
  char addr[16];
  snprintf(addr, sizeof(addr), "%-9s", to);
  char frame[256];
  int n = snprintf(frame, sizeof(frame), "%s>%s%s::%s:%s", cfg.callsign, destOf(cfg),
                   effectivePath(cfg).c_str(), addr, body.c_str());
  if (n <= 0 || (size_t)n >= sizeof(frame)) return -100;
  int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);  // honours the mute
  if (st == RADIOLIB_ERR_NONE) {
    displayNoteTx("MSG");
    flogLine("TX MSG -> %s %s", to, t.c_str());
    // Wait for the ack; aprsMsgTick() resends it (same number) if it does not
    // arrive, and gives up after cfg.msgRetries extra attempts.
    gPend.active = true;
    strncpy(gPend.to, to, sizeof(gPend.to) - 1);
    gPend.to[sizeof(gPend.to) - 1] = '\0';
    strncpy(gPend.text, body.substring(0, body.length() - 4).c_str(),
            sizeof(gPend.text) - 1);
    gPend.text[sizeof(gPend.text) - 1] = '\0';
    gPend.id = mid;
    gPend.tries = 1;
    gPend.nextMs = millis() + kMsgRetryMs;
  }
  return st;
}

// Broadcast bulletin (APRS101 chapter 14): a message addressed to BLNn, which
// every station displays for everybody. n = 0..9, 0 = the general one.
int16_t aprsSendBulletin(const DigiConfig &cfg, const char *text, char group) {
  if (text == nullptr || text[0] == '\0') return -100;
  if (!callsignBeaconOk(cfg)) return -100;
  if (group < '0' || group > '9') group = '0';
  char addr[16];
  snprintf(addr, sizeof(addr), "BLN%c     ", group);
  char frame[256];
  int n = snprintf(frame, sizeof(frame), "%s>%s%s::%s:%s", cfg.callsign, destOf(cfg),
                   effectivePath(cfg).c_str(), addr, text);
  if (n <= 0 || (size_t)n >= sizeof(frame)) return -100;
  int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);
  if (st == RADIOLIB_ERR_NONE) {
    displayNoteTx("BLN");
    flogLine("TX BLN%c %s", group, text);
  }
  return st;
}

// APRS object (APRS101 chapter 11): ";NAME     *DDHHMMz" + position + symbol +
// comment. It appears on every map as a separate item. kill=true sends the
// same name marked with '_' and no position, which removes it everywhere.
int16_t aprsSendObject(const DigiConfig &cfg, const char *name, double lat,
                       double lon, const char *comment, bool kill) {
  if (name == nullptr || name[0] == '\0') return -100;
  if (!callsignBeaconOk(cfg)) return -100;
  const GpsData &g = gpsGet();
  if (!kill && !(g.timeValid && g.dateValid)) return -100;  // stamp needs a clock
  char nm[10];
  snprintf(nm, sizeof(nm), "%-9.9s", name);
  char t[16];
  snprintf(t, sizeof(t), "%02u%02u%02uz", (unsigned)g.utcDay, (unsigned)g.utcH,
           (unsigned)g.utcM);
  char frame[256];
  if (kill) {
    int n = snprintf(frame, sizeof(frame), "%s>%s%s:;%s_%s", cfg.callsign, destOf(cfg),
                     effectivePath(cfg).c_str(), nm, t);
    if (n <= 0 || (size_t)n >= sizeof(frame)) return -100;
    int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);
    if (st == RADIOLIB_ERR_NONE) {
      displayNoteTx("OBJ");
      flogLine("TX OBJ borrado %s", nm);
    }
    return st;
  }
  char pos[24];
  encodePosition(lat, lon, pos, sizeof(pos), cfg.overlay[0], 0);
  int n = snprintf(frame, sizeof(frame), "%s>%s%s:;%s*%s%s%c%s", cfg.callsign,
                   destOf(cfg), effectivePath(cfg).c_str(), nm, t, pos, cfg.symbol,
                   (comment != nullptr) ? comment : "");
  if (n <= 0 || (size_t)n >= sizeof(frame)) return -100;
  int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);
  if (st == RADIOLIB_ERR_NONE) {
    displayNoteTx("OBJ");
    flogLine("TX OBJ %s %.5f,%.5f %s", nm, lat, lon,
             (comment != nullptr) ? comment : "");
  }
  return st;
}

const char *aprsLastHeardCall() {
  // The ring lives in a stack array, so the call must be copied into a static
  // buffer before returning the pointer (a pointer into `hs` would dangle).
  static char last[12] = "";
  HeardStation hs[kHeardSize];
  uint8_t n = aprsHeardStations(hs, kHeardSize);
  last[0] = '\0';
  if (n > 0) {
    strncpy(last, hs[0].call, sizeof(last) - 1);
    last[sizeof(last) - 1] = '\0';
  }
  return last;
}

// Status packet (CA2RXU: sent once a day; here also at boot). text overrides the
// configured status when given (tracker boot banner while the GPS has no fix).
// Every caller is an automatic one (boot / 24 h / GPS notice), so the KISS rule
// applies: the host app commands.
int16_t aprsSendStatus(const DigiConfig &cfg, const char *text) {
  if (!autoBeaconAllowed()) return -100;  // KISS: the host app commands
  const char *body = (text && text[0] != '\0') ? text : cfg.status;
  if (body[0] == '\0') return -100;
  char frame[256];
  int n = snprintf(frame, sizeof(frame), "%s>%s%s:>%s", cfg.callsign, destOf(cfg),
                   effectivePath(cfg).c_str(), body);
  if (n <= 0 || (size_t)n >= sizeof(frame)) return -100;
  int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);
  if (st == RADIOLIB_ERR_NONE) displayNoteTx("STATUS");
  // Registro del viaje: los paquetes de estado no dejaban ninguna linea, asi
  // que el aviso de arranque ("En marcha, buscando satelites") no se podia
  // comprobar despues. Una linea por transmision, como las balizas:
  // texto, bytes en el aire y ok/err.
  flogLine("TX STS %s %ub %s", body, (unsigned)n,
           (st == RADIOLIB_ERR_NONE) ? "ok" : "err");
  return st;
}

// --- Compressed position report (APRS101 chapter 9) --------------------------
// 13 characters: <symbol table><4 lat><4 lon><symbol code><c><s><T>, all base-91
// (value + 33) except the table and the symbol code. It replaces the whole
// "ddmm.hhN/dddmm.hhW<sym>" block, so a tracker beacon gets ~13 characters
// shorter (about 10% less air time at SF12) and the receiver knows it is
// compressed because the first character is not a digit.
//   lat = 380926 * (90 - latitude)      lon = 190463 * (180 + longitude)
//   c   = course / 4                    speed = 1.08^s - 1  (knots)
//   T   = fix current (bit 5) + source RMC (bits 4-3) + compressed by software
//         (bits 2-0) = 58 -> '['
void base91_4(uint32_t v, char *out) {
  static const uint32_t kP3 = 91u * 91u * 91u;
  static const uint32_t kP2 = 91u * 91u;
  out[0] = (char)(v / kP3 + 33);
  v %= kP3;
  out[1] = (char)(v / kP2 + 33);
  v %= kP2;
  out[2] = (char)(v / 91u + 33);
  out[3] = (char)(v % 91u + 33);
}

int base91_val(float v) {
  int n = (int)(v + 0.5f);
  if (n < 0) n = 0;
  if (n > 89) n = 89;
  return n + 33;
}

// out must hold at least 14 bytes (13 + NUL).
void encodePositionCompressed(double lat, double lon, float courseDeg,
                              float speedKmh, char symbol, char tableId,
                              char *out) {
  uint32_t ilat = (uint32_t)(380926.0 * (90.0 - lat) + 0.5);
  uint32_t ilon = (uint32_t)(190463.0 * (180.0 + lon) + 0.5);
  char *p = out;
  *p++ = tableId;  // the receiver sees "not a digit" and knows it is compressed
  base91_4(ilat, p);
  p += 4;
  base91_4(ilon, p);
  p += 4;
  *p++ = symbol;
  // Course/speed: course in 4 degree steps, speed in knots as 1.08^s - 1.
  const float knots = speedKmh / 1.852f;
  const float se = logf(knots + 1.0f) / logf(1.08f);  // 1.08^s - 1 = knots
  *p++ = (char)base91_val(courseDeg / 4.0f);
  *p++ = (char)base91_val(se);
  *p++ = (char)(58 + 33);  // T: current fix, RMC, compressed by software
  *p = '\0';
}

int16_t aprsSendTrackerBeacon(const DigiConfig &cfg, const GpsData &g, char why,
                              const char *banner, bool manual, bool lastKnown) {
  if (!manual && !autoBeaconAllowed()) return -100;  // KISS: host app commands
  // ★★ LA ULTIMA CONOCIDA NO TIENE FIJACION, Y ESO ES LO NORMAL (2026-09-16) ★★
  //   La ultima posicion conocida sale de la flash (lastpos.cpp) y lastPosFill()
  //   deja `fix = false` A PROPOSITO: el GPS no la tiene ahora, se recurre a ella
  //   justo cuando no la hay. Al exigir `g.fix` sin mirar `lastKnown`, esta funcion
  //   devolvia -100 y NO SALIA NADA: la baliza de ultima conocida no funcionaba en
  //   ninguno de sus tres caminos (doble toque -> trackerBeaconLastKnown(), verbo
  //   `beacon` -> aprsSendManualBeacon(), boton del configurador -> este mismo
  //   camino). El resto de la rama ya estaba escrito y es correcto (sello de hora
  //   antiguo, aviso ">ULTIMA CONOCIDA", motivo 'L' en el registro): lo unico que
  //   faltaba era dejar pasar el paquete. Medido leyendo el codigo, no supuesto:
  //   lastpos.cpp:128-150 (lastPosFill) no pone fix en ningun momento.
  if (!g.fix && !lastKnown) return -100;
  if (!callsignBeaconOk(cfg)) return -100;  // never beacon as NOCALL/empty
  char pos[24];
  char comp[16];
  // ★ ICONO DEL MAPA POR PERFIL (2026-09-15): el mismo par (tabla, codigo) que
  //   usa el SSID del perfil (profileTxSource, unas lineas mas abajo) se usa para
  //   el icono. Ver aprsProfileIcon() para la precedencia: si el operador ha
  //   tocado el icono a mano, manda el suyo.
  char iconTable = cfg.overlay[0] ? cfg.overlay[0] : '/';
  char iconCode = cfg.symbol;
  aprsProfileIcon(cfg, iconTable, iconCode);
  encodePosition(g.lat, g.lon, pos, sizeof(pos), iconTable,
                 cfg.posAmbiguity);
  encodePositionCompressed(g.lat, g.lon, g.courseDeg, g.speedKmh, iconCode,
                           iconTable, comp);

  String s;
  s.reserve(200);
  s += profileTxSource(cfg);   // fuente: aplica el SSID del perfil de uso activo
  s += ">";
  s += destOf(cfg);
  s += effectivePath(cfg);
  s += ":";
  // Timestamped position: APRS expects "@" + DAY + HOUR + MINUTE + "z" (UTC),
  // exactly like the weather packet. Using hour/minute/second here produced an
  // invalid stamp and the servers silently dropped every tracker beacon.
  if (g.timeValid && g.dateValid) {
    char t[16];
    snprintf(t, sizeof(t), "@%02u%02u%02uz", (unsigned)g.utcDay,
             (unsigned)g.utcH, (unsigned)g.utcM);
    s += t;
  } else {
    s += "!";  // no clock (or no date yet): plain position report
  }
  if (cfg.compressedPos) {
    s += comp;
  } else {
    s += pos;
    s += iconCode;
    if (g.speedKmh >= 2.0f) {
      char cs[16];
      // fix #6: CSE/SPD must hug the symbol (no leading space) or aprs.fi
      // will not parse speed/course.
      snprintf(cs, sizeof(cs), "%03d/%03d", (int)g.courseDeg,
               (int)(g.speedKmh / 1.852f + 0.5f));
      s += cs;
    }
  }
  if (cfg.sendAltitude && g.altValid) {
    char a[16];
    snprintf(a, sizeof(a), "/A=%06d", (int)(g.altM / 0.3048f));
    s += a;
  }

  // One-shot banner ("NODO ONLINE", "DURMIENDO HASTA EL SOL"), in the same spot
  // the fixed beacon uses: right after the position and before the comment/WX.
  if (banner && banner[0] != '\0') {
    s += ' ';
    s += banner;
  }

  // Ultima posicion conocida (peticion del operador, 2026-09-13): la posicion
  // viaja como baliza de POSICION normal (para que se vea en el mapa) pero con
  // su hora real, la de cuando se tomo, y ademas con este aviso AL PRINCIPIO del
  // comentario. Asi no hace falta que nadie deduzca nada leyendo la hora: el
  // propio paquete dice que es una posicion historica. Y el ">" delante hace que
  // un cliente que solo mire el texto lo lea como aviso.
  if (lastKnown) {
    s += ">ULTIMA CONOCIDA ";
  }

  static SensorReadings read;
  sensorsRead(read);  // chip temperature always; WX/Batt when enabled
  if (cfg.wxSensorActive) appendWx(s, cfg, read);
  appendChipTemp(s, cfg, read);
  appendLowBat(s, cfg, read);
  if (cfg.comment[0] != '\0') s += cfg.comment;
  if (cfg.sendBatteryTelemetry) appendPower(s, read);

  if (s.length() == 0 || s.length() >= 255) return -100;
  int16_t st = aprsSendTextFrame(cfg, s.c_str(), s.length());
  if (st == RADIOLIB_ERR_NONE) displayNoteTx("BEACON");
  // Flash log: position line the web configurator turns into GPX/KML. Speed with
  // one decimal (%.0f hid the difference between "stopped" and "0.4 km/h of GPS
  // noise" while chasing phantom beacons) and the reason the beacon fired:
  // F = first fix, S = arranque de sesion (primer fix bueno), R = rate/clock,
  // C = corner, D = distance, M = manual,
  // L = last known position (sent on purpose, not from now: it must NOT be
  // mistaken for a new point on the exported route).
  char reason = lastKnown ? 'L' : ((why != 0) ? why : 'M');

  // --- ARRANQUE DE SESION EN EL REGISTRO (idea del operador, 2026-09-13) ---
  // Al encender, el GPS tarda en asentarse: las PRIMERAS posiciones traen errores
  // de decenas o cientos de metros y ensucian el principio del track. Aqui se
  // esperan kSesionFixes posiciones buenas antes de dar por empezada la sesion, y
  // ese primer punto queda marcado con 'S' (session start).
  //   (1) el track no arranca con una posicion mala;
  //   (2) la fecha y la hora de ESE punto son exactas, asi que el nombre del
  //       fichero (indicativo + fecha + hora) lleva la hora de verdad;
  //   (3) la web sabe donde empieza cada sesion sin adivinar por huecos de tiempo.
  // lastKnown NO cuenta: es una posicion historica mandada a mano, no una muestra
  // nueva del GPS.
  static uint8_t fixesBuenos = 0;
  static bool sesionAbierta = false;
  if (!lastKnown) {
    if (fixesBuenos < 255) fixesBuenos++;
    if (!sesionAbierta && fixesBuenos >= kSesionFixes) {
      sesionAbierta = true;
      reason = 'S';   // primer punto bueno: aqui empieza la sesion
    }
  }

  flogLine("TX TRK %.5f,%.5f %.1fkm/h %.0fdeg %.0fm %ub %s %c", g.lat, g.lon,
           g.speedKmh, g.courseDeg, g.altM, (unsigned)s.length(),
           (st == RADIOLIB_ERR_NONE) ? "ok" : "err", reason);
  return st;
}

int16_t aprsSendTextFrame(const DigiConfig &cfg, const char *frame, size_t len,
                          bool bypassMute, bool useCad) {
  // ★★ FILTRO DE 7 BITS: APRS ES TEXTO ASCII IMPRIMIBLE (2026-09-15) ★★
  // Va lo PRIMERO, antes incluso del mute y del indicativo, porque es lo que
  // garantiza que "lo que se transmite" y "lo que el registro cuenta" sean lo
  // mismo: a partir de aqui todas las comprobaciones y todos los diagTxFrame()
  // trabajan sobre la trama YA limpia.
  // Por aqui pasa TODO lo que sale al aire (balizas, meteorologia, telemetria,
  // estado, mensajes, objetos, boletines, respuestas y repeticiones), asi que
  // ningun texto puede colarse con un byte que un iGate o un cliente estricto
  // interprete como fin de trama. Un byte fuera de 0x20..0x7E (una letra
  // acentuada, una enie, un caracter de control) se sustituye por '.'.
  // NO se toca la estructura: '>' ':' ',' siguen intactos, asi que el sobre
  // "SRC>DST,PATH:info" no se altera.
  char clean[260];
  if (len >= sizeof(clean)) return -100;  // no cabe ni en el peor caso
  memcpy(clean, frame, len);
  clean[len] = '\0';
  for (size_t i = 0; i < len; i++) {
    const unsigned char c = (unsigned char)clean[i];
    if (c < 0x20 || c > 0x7E) clean[i] = '.';
  }
  frame = clean;

  if (cfg.txDisabled && !bypassMute) {
    diagTxFrame(frame, len, -102);  // muted (fail closed)
    return -102;
  }
  // ★★ NORMA DE SEGURIDAD: SIN INDICATIVO VALIDO NO SE TRANSMITE NADA (2026-09-15) ★★
  // Un nodo con el NOCALL-11 de fabrica (o con el indicativo vacio) NO puede salir al
  // aire: no es una estacion valida y ensucia la red, porque los visores lo toman por
  // una estacion de verdad.
  // Vale para TODO lo que se emite: balizas, mensajes, acuses, telemetria, meteorologia,
  // objetos, respuestas y **repeticiones**. El repetidor era justo el que se colaba:
  // metia "NOCALL-11*" en el path de las tramas que repetia (visto en el aire el
  // 2026-09-15), porque las 6 comprobaciones que habia estaban sueltas en las funciones
  // de baliza y ninguna cubria el digi.
  // Va AQUI, en el embudo por el que pasa todo lo que se transmite, y no repartido: asi
  // no se puede olvidar ninguna salida nueva que se anada manana.
  if (!callsignBeaconOk(cfg)) {
    diagTxFrame(frame, len, -103);
    return -103;  // fail closed: sin indicativo valido, ni un byte al aire
  }
  // LoRa-APRS wire prefix 0x3C 0xFF 0x01 (CA2RXU lora_utils.cpp:229): without
  // it no APRS-LoRa receiver (tracker/iGate) parses our frames.
  uint8_t payload[260];
  constexpr size_t kPrefix = 3;
  if (len == 0 || len > sizeof(payload) - kPrefix) return -100;
  payload[0] = 0x3C;
  payload[1] = 0xFF;
  payload[2] = 0x01;
  memcpy(payload + kPrefix, frame, len);

  // Listen-before-talk (CAD) with bounded retry; abort when the channel stays
  // busy (do not stomp on other stations). Back-to-back frames skip CAD.
  if (useCad && cfg.cadActive && radioReady()) {
    bool freeCh = false;
    for (int i = 0; i < 6; i++) {
      if (radioScanChannel() == RADIOLIB_CHANNEL_FREE) {
        freeCh = true;
        break;
      }
      delay(random(150, 901));
    }
    if (!freeCh) {
      diagTxFrame(frame, len, -101);
      radioRestartRx();
      return -101;
    }
  }
  int16_t st = radioSendFrame(payload, len + kPrefix);
  diagTxFrame(frame, len, st);
  return st;
}

namespace {

uint16_t gTelemSeq = 0;

int clamp255(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return v;
}

}  // namespace

int16_t aprsSendTelemetryMeta(const DigiConfig &cfg) {
  if (!autoBeaconAllowed()) return -100;  // KISS: the host app commands
  if (!(cfg.sendBatteryTelemetry || cfg.wxSensorActive)) return -100;
  // Channel order is fixed: Vbat(V) / I(mA, signed via -1280 offset, 10 mA
  // steps) / Temp(C, 0.5) / Hum(%, 0.5) / Press(hPa, 1, -880 offset).
  // PARM is built from the sensors actually present, so an absent probe shows
  // as "na" instead of a fake 0 (the chip sensor names its channel TChip, since
  // it reads the die and not the air).
  static SensorReadings r;
  sensorsRead(r);
  const char *tempName = r.tempOk ? "Temp" : (r.chipTempOk ? "TChip" : "na");
  char parm[64];
  snprintf(parm, sizeof(parm), "PARM.Vbat,I,%s,%s,%s", tempName,
           r.humOk ? "Hum" : "na", r.pressOk ? "Press" : "na");

  static const char *kMeta[] = {
      "UNIT.V,mA,C,%,hPa",
      "EQNS.0,0.02,0,0,10,-1280,0,0.5,-20,0,0.5,0,0,1,880",
      "BITS.00000000",
  };
  // Telemetry metadata must be an APRS message to ourselves
  // ("::CALLSIGN :PARM...") or aprs.fi cannot associate PARM/UNIT/EQNS/BITS
  // with the T# stream.
  char addr[16];
  snprintf(addr, sizeof(addr), "%-9s", cfg.callsign);
  char msg[160];
  int n = snprintf(msg, sizeof(msg), "%s>%s::%s:%s", cfg.callsign, destOf(cfg), addr,
                   parm);
  if (n <= 0 || (size_t)n >= sizeof(msg)) return -100;
  int16_t st = aprsSendTextFrame(cfg, msg, (size_t)n, false, true);
  if (st != RADIOLIB_ERR_NONE) return st;
  for (const char *m : kMeta) {
    n = snprintf(msg, sizeof(msg), "%s>%s::%s:%s", cfg.callsign, destOf(cfg), addr, m);
    if (n <= 0 || (size_t)n >= sizeof(msg)) return -100;
    // only the first metadata frame listens first (burst optimization)
    st = aprsSendTextFrame(cfg, msg, (size_t)n, false, false);
    if (st != RADIOLIB_ERR_NONE) return st;
  }
  return st;
}

int16_t aprsSendTelemetry(const DigiConfig &cfg) {
  if (!autoBeaconAllowed()) return -100;  // KISS: the host app commands
  if (!(cfg.sendBatteryTelemetry || cfg.wxSensorActive)) return -100;

  static SensorReadings read;
  sensorsRead(read);

  // Temperature channel: the external probe is the valid air reading; the chip
  // sensor is only the fallback (channel named TChip in the metadata).
  float tempC = read.tempOk ? (read.tempC + cfg.temperatureCorrectionC)
                            : sensorsChipTemp(read, cfg.chipTempOffsetC);

  float v = sensorsBatteryVolt(read);
  int a1 = (v > 0.0f) ? (int)(v / 0.02f + 0.5f) : 0;
  int a2 = (int)((read.inaCurrentMa + 1280.0f) / 10.0f + 0.5f);
  int a3 = (int)((tempC + 20.0f) / 0.5f + 0.5f);
  int a4 = read.humOk ? (int)(read.hum / 0.5f + 0.5f) : 0;
  int a5 = read.pressOk
               ? (int)((read.pressHpa + cfg.heightCorrectionM / 8.2296f - 880.0f) +
                       0.5f)
               : 0;

  // Metadata periodically (every 10th sequence) so aprs.fi keeps the units
  if (gTelemSeq % 10 == 0) {
    int16_t mst = aprsSendTelemetryMeta(cfg);
    if (mst != RADIOLIB_ERR_NONE) return mst;
  }

  char frame[160];
  int n = snprintf(frame, sizeof(frame),
                   "%s>%s%s:T#%03u,%03d,%03d,%03d,%03d,%03d,00000000",
                   cfg.callsign, destOf(cfg), effectivePath(cfg).c_str(),
                   (unsigned)gTelemSeq, clamp255(a1), clamp255(a2),
                   clamp255(a3), clamp255(a4), clamp255(a5));
  if (n <= 0 || (size_t)n >= sizeof(frame)) return -100;
  gTelemSeq = (uint16_t)((gTelemSeq + 1) % 1000);

  int16_t st = aprsSendTextFrame(cfg, frame, (size_t)n);
  if (st == RADIOLIB_ERR_NONE) displayNoteTx("TELEM");
  flogLine("TX TLM seq=%u %s", (unsigned)gTelemSeq,
           (st == RADIOLIB_ERR_NONE) ? "ok" : "err");
  return st;
}

void aprsHandleRadioPacket(const uint8_t *data, size_t len, float rssi, float snr) {
  if (!gCfg) return;
  DigiConfig &cfg = *gCfg;
  if (len <= 3 || data[0] != 0x3C || data[1] != 0xFF || data[2] != 0x01) return;

  String text;
  text.reserve((unsigned int)(len - 3));
  for (size_t i = 3; i < len; i++) text += (char)data[i];
  if (text.indexOf("NOGATE") >= 0) return;
// APRS third-party packet: unwrap the embedded frame.
// Example:
// F4LEK-10>APLRG1,WIDE1-1:}F4LEK-9>APDR17,TCPIP,F4LEK-10*::F4LEK-7 :Hello
// becomes:
// F4LEK-9>APDR17,TCPIP,F4LEK-10*::F4LEK-7 :Hello
int thirdParty = text.indexOf(":}");
if (thirdParty >= 0) {
    String inner = text.substring(thirdParty + 2);

    // The embedded packet must itself look like an APRS frame.
    int innerGt = inner.indexOf('>');
    int innerColon = inner.indexOf(':');

    if (innerGt <= 0 || innerColon <= innerGt + 1) return;

    text = inner;
}

  int gt = text.indexOf('>');
  int colon = text.indexOf(':');
  if (gt <= 0 || colon <= gt + 1) return;  // malformed

  String sender = text.substring(0, gt);
  if (!callsignValid(sender.c_str())) return;
  strncpy(gLastFrom, sender.c_str(), sizeof(gLastFrom) - 1);
  gLastFrom[sizeof(gLastFrom) - 1] = '\0';
  if (sender == cfg.callsign) return;  // never repeat ourselves
  if (isBlacklisted(cfg, sender)) return;

  // USB TNC mode: mirror every valid frame to the host (APRSdroid TNC2).
  tncOutputFrame(text.c_str());
  diagRxFrame(sender.c_str(), text.c_str(), rssi, snr);

  gLastRssi = rssi;
  gLastSnr = snr;
  char info0 = (colon + 1 < (int)text.length()) ? text[colon + 1] : '?';
  const char *kind = (info0 == '!' || info0 == '=' || info0 == '@' ||
                      info0 == '/')
                         ? "Bcn"
                         : (info0 == ':') ? "Msg" : "Pkt";
  displayNoteRx(sender.c_str(), rssi, snr, kind);
  heardPush(sender.c_str(), rssi, snr, text.c_str() + colon + 1);
  flogLine("RX %s %s rssi%.0f snr%.1f", sender.c_str(), kind, rssi, snr);

  // Messages ("::ADDRESSEE :..."): those addressed to us go to the
  // remote-control/query handler and are never repeated; messages for other
  // stations fall through and are digipeated like any other frame (CA2RXU).
  String info = text.substring(colon + 1);
  if (info0 == ':') {
    if (dedupeCheckAndInsert(sender, text.substring(colon + 2))) return;
    int c2 = info.indexOf(':', 1);
    String addressee = (c2 < 0) ? info.substring(1) : info.substring(1, c2);
    addressee.trim();
    if (strcasecmp(addressee.c_str(), cfg.callsign) == 0) {
      if (handleRemoteMessage(cfg, sender, info)) return;  // consumed
      // not handled: fall through and digipeat it (CA2RXU)
    }
  } else {
    // Dedupe key: sender + info tail (skip ":" and the APRS type byte), as CA2RXU.
    if (dedupeCheckAndInsert(sender, text.substring(colon + 2))) return;
  }

  // Digipeater only when enabled and not in tracker-only mode.
  if (cfg.digiMode == 0 || cfg.mode == 1) return;

  // Address part "SRC>DST," plus rewritten path plus ":INFO"
  int comma = text.indexOf(',');
  String digiPath;
  if (comma > gt + 1 && comma < colon) {
    digiPath = text.substring(comma + 1, colon);
    digiPath = buildDigiPath(cfg, digiPath);
    if (digiPath.length() == 0) return;  // nothing for us to repeat
  } else {
    return;  // no path -> no hop available
  }

  // Rebuild the full text frame and transmit.
  String outText = text.substring(0, comma + 1) + digiPath + text.substring(colon);
  if (outText.length() > 240) return;

  int16_t st = aprsSendTextFrame(cfg, outText.c_str(), outText.length());
  if (st == RADIOLIB_ERR_NONE) {
    gDigiCount = (gDigiCount + 1) % 10000;  // wrap (display/telemetry)
    displayNoteDigi(sender.c_str(), rssi, snr);
    if (!tncActive()) printDigiLog(sender, outText);
    flogLine("DG %s -> %s rssi%.0f snr%.1f", sender.c_str(), digiPath.c_str(),
             rssi, snr);
  }
}

uint8_t aprsHeardStations(HeardStation *out, uint8_t max) {
  uint8_t n = gHeardUsed < max ? gHeardUsed : max;
  for (uint8_t i = 0; i < n; i++) {
    int idx = (gHeardHead - 1 - i + kHeardSize) % kHeardSize;
    out[i] = gHeard[idx];
  }
  return n;
}
