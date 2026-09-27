// config.cpp — configuration load/validate/serialize (schema v1)
// License: GPL-3.0

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ax25.h"
#include "flog.h"   // flogLine(): el registro del aviso de claves ignoradas

void configSetDefaults(DigiConfig &cfg) {
  cfg = DigiConfig();
}

// ★ INDICATIVO: LA MISMA VARA QUE YA USA LA RADIO (2026-09-15).
// Antes esto era una comprobacion floja (3..15 caracteres, y aceptaba espacios),
// asi que se podia guardar un indicativo que NO se puede representar en AX.25:
//   - con espacio ("EA2 OY-7"): en AX.25 el espacio es RELLENO, asi que el
//     indicativo se lee cortado ("EA2");
//   - con mas de 6 caracteres antes del SSID: no cabe en el campo de direccion.
// En los dos casos la trama sale al aire y las demas estaciones no la pueden
// interpretar: aire gastado en algo ilegible.
// Ahora se delega en ax25ValidAddress(), que es exactamente el analisis que
// aplican ax25Build()/ax25Parse() (1..6 caracteres de A-Z, 0-9 y '/' para
// portatil, y SSID opcional 0..15). Un indicativo que pasa por aqui se puede
// poner en el aire; uno que no pasa, se rechaza al guardarlo.
// NO se prohibe la '/' a proposito: "EA2OY/P" es una operacion portatil valida.
static bool validCallsign(const char *s) { return ax25ValidAddress(s); }

// ★ TEXTO LIBRE: SOLO ASCII IMPRIMIBLE (2026-09-15).
// APRS viaja en 7 bits. Un comentario, un estado, un nombre de objeto o un
// boletin con una letra acentuada, una enie o un caracter de control rompe la
// trama en cuanto pasa por un iGate o por un cliente que no sea tolerante.
// Se sustituye el byte malo por '.' (no se borra): asi el texto conserva su
// longitud, el operador VE en el configurador que habia algo raro, y la trama
// sale siempre legal. El byte 0 no puede llegar aqui: el JSON ya corta la cadena.
static void keepPrintableAscii(char *s) {
  if (s == nullptr) return;
  for (; *s != '\0'; s++) {
    const unsigned char c = (unsigned char)*s;
    if (c < 0x20 || c > 0x7E) *s = '.';
  }
}

// Boolean field merge: accept a real JSON true/false AND 0/1 (the CLI "set"
// types numbers as ints, and a bare is<bool>() silently ignored them while the
// CLI still answered "OK"). Missing key = keep the current value.
static bool jsonBool(JsonVariantConst v, bool current) {
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<int>()) return v.as<int>() != 0;
  return current;
}

// ===========================================================================
//  ★★ LO QUE SE IGNORA, SE DICE (2026-09-15) ★★
// ===========================================================================
// QUE PROBLEMA RESUELVE: cada campo de aqui abajo se aplica con un "if (el tipo
// encaja)", asi que una clave CONOCIDA con un valor que no encaja en su tipo
// ("beaconInterval": "cada media hora", "latitude": "42N", profiles con el SSID
// como texto) se descartaba EN SILENCIO y la funcion contestaba true: el
// configurador web decia "guardado", el operador se iba tranquilo y el ajuste
// seguia como estaba. Un compañero perdio media hora con los perfiles por esto
// (el aparato decia que si y no se guardaba nada).
//
// COMO SE ARREGLA: cada rechazo por tipo deja el NOMBRE DE LA CLAVE aqui, y al
// final se avisa por dos caminos:
//   - errMsg (el mensaje que devuelve el protocolo/USB), y
//   - flogLine() (el registro del viaje, para quien no estaba delante).
// No es un error fatal a proposito: el contrato de esta funcion es "aplica lo
// que entiendas y no toques lo demas", y romperlo ahora (devolver false ante
// cualquier clave rara) dejaria sin guardar configuraciones buenas por un campo
// suelto. Lo que NO puede seguir pasando es que se calle.
//
// ★ OJO CON LA TRAMPA DE SIEMPRE EN ESTE FICHERO: `in` es un JsonVariantConst, y
//   aqui se usan SOLO comprobaciones de solo lectura (is<JsonObjectConst>(),
//   is<JsonArrayConst>(), is<const char *>(), is<int>()...). Las mutables
//   (is<JsonObject>(), is<JsonArray>()) dan SIEMPRE false en un variant constante:
//   es lo que tenia los perfiles muertos sin que nadie se enterara.
namespace {
char gIgnoradas[128] = "";   // nombres de clave separados por comas ("" = ninguna)
bool gHuboIgnoradas = false;

// Se llama SOLO desde configFromJson(), que limpia el buffer al empezar.
void ignoraReset() {
  gIgnoradas[0] = '\0';
  gHuboIgnoradas = false;
}

void ignoraAvisa(const char *key) {
  if (key == nullptr || key[0] == '\0') return;
  const size_t usado = strlen(gIgnoradas);
  if (usado >= sizeof(gIgnoradas) - 2) return;   // ya no cabe: se queda con lo dicho
  const char *sep = (usado != 0) ? ", " : "";
  snprintf(gIgnoradas + usado, sizeof(gIgnoradas) - usado, "%s%s", sep, key);
  gHuboIgnoradas = true;
}

// Clave conocida pero con un valor que no se puede leer como su tipo.
// JsonVariantConst::isNull() tambien es true para una clave AUSENTE, asi que eso
// (lo normal en una actualizacion parcial) no se avisa.
bool ignoraSiNoEncaja(JsonVariantConst v, const char *key) {
  if (v.isNull()) return false;
  ignoraAvisa(key);
  return true;
}

// Numero entero esperado. Un float ("15.5") y un texto caen aqui: un decimal en
// un campo de minutos NO se redondea a la baja en silencio, se avisa.
bool ignoraSiNoEsEntero(JsonVariantConst v, const char *key) {
  if (v.isNull() || v.is<int>() || v.is<long>()) return false;
  ignoraAvisa(key);
  return true;
}

// Numero (entero o decimal) esperado.
bool ignoraSiNoEsNumero(JsonVariantConst v, const char *key) {
  if (v.isNull() || v.is<int>() || v.is<long>() || v.is<float>() ||
      v.is<double>()) {
    return false;
  }
  ignoraAvisa(key);
  return true;
}

// Texto esperado.
bool ignoraSiNoEsTexto(JsonVariantConst v, const char *key) {
  if (v.isNull() || v.is<const char *>()) return false;
  ignoraAvisa(key);
  return true;
}

// ★★ TABLA/OVERLAY DE UN SIMBOLO APRS (APRS101 cap. 20) — UNA SOLA VARA ★★
// El PRIMERO de los dos caracteres que forman un icono dice la tabla:
//   '/'  tabla primaria
//   '\'  tabla alternativa
//   '0'-'9' / 'A'-'Z'  tabla alternativa con ese overlay
// Cualquier otra cosa cae a la primaria (mismo criterio de siempre: una config
// rara no puede romper la trama). Devuelve false SOLO cuando el caracter no es
// representable, para que quien llame pueda avisar en vez de cambiarlo a espaldas
// del operador. Se usa desde la clave `overlay` y desde el icono por perfil.
bool jsonSymbolTable(char c, char &out) {
  if (c == '\\') { out = '\\'; return true; }
  if (c >= '0' && c <= '9') { out = c; return true; }
  if (c >= 'A' && c <= 'Z') { out = c; return true; }
  if (c >= 'a' && c <= 'z') { out = (char)(c - 'a' + 'A'); return true; }  // overlays en mayusculas
  if (c == '/') { out = '/'; return true; }
  out = '/';
  return false;
}

// Codigo de simbolo: el SEGUNDO caracter del icono. Tiene que ser ASCII
// imprimible (los 94 codigos de las dos tablas salen de ahi). Se valida porque
// un espacio o un caracter de control en esa posicion produce una trama que los
// demas no pueden interpretar: mejor no guardarlo que emitirlo.
bool jsonSymbolCodeOk(char c) {
  return (unsigned char)c > 0x20 && (unsigned char)c < 0x7F;
}
}  // namespace

bool configFromJson(DigiConfig &cfg, JsonVariantConst in, String &errMsg) {
  ignoraReset();   // ver "LO QUE SE IGNORA, SE DICE" arriba
  // NOTE: JsonVariantConst must be checked as JsonObjectConst (the mutable
  // JsonObject check is always false on a const variant).
  if (!in.is<JsonObjectConst>()) {
    errMsg = "config must be a JSON object";
    return false;
  }
  // Merge onto the current config: a partial update (one key from the OLED
  // menu, CLI "set", remote "set") must NOT reset the other fields to defaults.
  DigiConfig next = cfg;
  char buf[64];

  // --- strings ---
  ignoraSiNoEsTexto(in["callsign"], "callsign");
  if (in["callsign"].is<const char *>()) {
    strncpy(buf, in["callsign"] | "NOCALL-11", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char *p = buf; *p; p++) *p = toupper(*p);
    if (!validCallsign(buf)) { errMsg = "invalid callsign"; return false; }
    strncpy(next.callsign, buf, sizeof(next.callsign) - 1);
  }
  ignoraSiNoEsTexto(in["path"], "path");
  if (in["path"].is<const char *>()) {
    strncpy(buf, in["path"] | "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    strncpy(next.path, buf, sizeof(next.path) - 1);
  }
  // Mensaje de dormido: texto libre (p. ej. telefono). No toca mayusculas.
  // Va a la pantalla, no al aire, pero se limpia igual: el mismo texto puede
  // acabar en un comentario si el operador lo copia.
  ignoraSiNoEsTexto(in["sleepMsg"], "sleepMsg");
  if (in["sleepMsg"].is<const char *>()) {
    strncpy(buf, in["sleepMsg"] | "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    keepPrintableAscii(buf);
    strncpy(next.sleepMsg, buf, sizeof(next.sleepMsg) - 1);
  }
  // ★ EL TOCALL YA NO SE LEE (2026-09-21): dejo de ser un ajuste y paso a ser una
  //   declaracion del firmware (DigiConfig::kKachoSystemTocall). Si llega por aqui
  //   (consola `set tocall ...`, configurador, app o menu del aparato) SE IGNORA A
  //   PROPOSITO: el nodo manda siempre nuestra matricula, venga de donde venga el paquete.
  //   Antes esto validaba el valor (mayusculas, alfanumerico, max 6) y lo guardaba.
  ignoraSiNoEsTexto(in["tocall"], "tocall");
  // Una ruta por modo de trabajo (repetidor / rastreador / ambos).
  struct { const char *key; char *dst; size_t len; } paths[] = {
      {"pathDigi", next.pathDigi, sizeof(next.pathDigi)},
      {"pathTracker", next.pathTracker, sizeof(next.pathTracker)},
      {"pathBoth", next.pathBoth, sizeof(next.pathBoth)},
  };
  for (auto &p : paths) {
    ignoraSiNoEsTexto(in[p.key], p.key);
    if (in[p.key].is<const char *>()) {
      strncpy(p.dst, in[p.key] | "", p.len - 1);
      p.dst[p.len - 1] = '\0';
      for (char *q = p.dst; *q; q++) *q = toupper(*q);
    }
  }
  // Comentario y estado: van DENTRO de la trama APRS (comentario de la baliza,
  // texto del paquete de estado), asi que solo ASCII imprimible.
  ignoraSiNoEsTexto(in["comment"], "comment");
  if (in["comment"].is<const char *>()) {
    strncpy(next.comment, in["comment"] | "", sizeof(next.comment) - 1);
    keepPrintableAscii(next.comment);
  }
  ignoraSiNoEsTexto(in["status"], "status");
  if (in["status"].is<const char *>()) {
    strncpy(next.status, in["status"] | "", sizeof(next.status) - 1);
    keepPrintableAscii(next.status);
  }
  ignoraSiNoEsTexto(in["msgText"], "msgText");
  if (in["msgText"].is<const char *>()) {
    strncpy(next.msgText, in["msgText"] | "", sizeof(next.msgText) - 1);
    keepPrintableAscii(next.msgText);
  }
  ignoraSiNoEsEntero(in["msgRetries"], "msgRetries");
  if (in["msgRetries"].is<int>()) {
    int v = in["msgRetries"] | 3;
    if (v < 0) v = 0;
    if (v > 5) v = 5;
    next.msgRetries = v;
  }
  ignoraSiNoEsTexto(in["sotaRecent"], "sotaRecent");
  if (in["sotaRecent"].is<const char *>()) {
    strncpy(next.sotaRecent, in["sotaRecent"] | "", sizeof(next.sotaRecent) - 1);
    keepPrintableAscii(next.sotaRecent);
  }
  ignoraSiNoEsTexto(in["sotaLast"], "sotaLast");
  if (in["sotaLast"].is<const char *>()) {
    strncpy(next.sotaLast, in["sotaLast"] | "", sizeof(next.sotaLast) - 1);
    keepPrintableAscii(next.sotaLast);
  }
  ignoraSiNoEsTexto(in["sotaPlan"], "sotaPlan");
  if (in["sotaPlan"].is<const char *>()) {
    strncpy(next.sotaPlan, in["sotaPlan"] | "", sizeof(next.sotaPlan) - 1);
    keepPrintableAscii(next.sotaPlan);
  }
  ignoraSiNoEsTexto(in["overlay"], "overlay");
  if (in["overlay"].is<const char *>()) {
    const char *ov = in["overlay"] | "/";
    char out = '/';  // primary table by default
    // Misma vara que el icono por perfil (jsonSymbolTable): un caracter que no
    // sea tabla valida cae a la primaria, que es el comportamiento de siempre.
    jsonSymbolTable(ov[0], out);
    next.overlay[0] = out;
    next.overlay[1] = '\0';
  }
  // ★ AMBIGUEDAD DE POSICION: 0..4 (2026-09-15).
  //   El 4 es el MAXIMO que sigue produciendo una trama LEGAL: con el se borran
  //   los dos digitos de los minutos Y los dos decimales, pero el PUNTO DECIMAL
  //   se queda (APRS101 cap. 6 exige un campo de posicion de longitud fija, y el
  //   punto forma parte de el). Antes se borraba tambien el punto y el resultado
  //   ("DD  N/DDD   E") no lo parseaba nadie: el operador creia difuminar su
  //   posicion y en realidad emitia una trama rota. Ver encodePosition().
  //   Se RECHAZA lo que no se puede representar (no se recorta en silencio): un
  //   ajuste que promete mas privacidad de la que se puede emitir sin romper la
  //   trama tiene que dar error, no mentir.
  ignoraSiNoEsEntero(in["posAmbiguity"], "posAmbiguity");
  if (in["posAmbiguity"].is<int>()) {
    int v = in["posAmbiguity"] | 0;
    if (v < 0 || v > 4) { errMsg = "posAmbiguity 0..4 (dígitos borrados)"; return false; }
    next.posAmbiguity = v;
  }
  next.compressedPos = jsonBool(in["compressedPos"], next.compressedPos);
  ignoraSiNoEncaja(in["compressedPos"], "compressedPos");
  ignoraSiNoEsTexto(in["blacklist"], "blacklist");
  if (in["blacklist"].is<const char *>()) {
    strncpy(next.blacklist, in["blacklist"] | "", sizeof(next.blacklist) - 1);
    keepPrintableAscii(next.blacklist);
  }
  ignoraSiNoEsTexto(in["managers"], "managers");
  if (in["managers"].is<const char *>()) {
    strncpy(buf, in["managers"] | "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char *p = buf; *p; p++) *p = toupper(*p);
    keepPrintableAscii(buf);
    strncpy(next.managers, buf, sizeof(next.managers) - 1);
  }
  ignoraSiNoEsTexto(in["symbol"], "symbol");
  if (in["symbol"].is<const char *>()) {
    // ★ El codigo de simbolo APRS es UN caracter (el PRIMERO de los dos que
    //   forman el icono; el otro es `overlay`, justo arriba). Aqui NO se acepta
    //   el par entero ("/>"): el firmware separa tabla y codigo desde siempre, y
    //   admitir las dos formas dejaria dos maneras de decir lo mismo en la misma
    //   configuracion. El icono POR PERFIL si admite el par (ver profiles).
    const char *s = in["symbol"] | "#";
    if (s[0]) {
      // Antes se aceptaba CUALQUIER byte (un espacio, un control) y salia al aire
      // una trama ilegible. Ahora se RECHAZA con su mensaje: es la misma regla
      // que ya se aplica al indicativo y a posAmbiguity.
      if (!jsonSymbolCodeOk(s[0])) {
        errMsg = "symbol: 1 caracter ASCII imprimible";
        return false;
      }
      next.symbol = s[0];
    }
  }

  // --- numbers ---
  // Floating point fields accept whole numbers too: "set latitude 42" from the
  // CLI/remote used to be silently ignored (ArduinoJson does not treat an int
  // as a float).
  bool hasLat = in["latitude"].is<float>() || in["latitude"].is<int>() ||
                in["latitude"].is<long>();
  bool hasLon = in["longitude"].is<float>() || in["longitude"].is<int>() ||
                in["longitude"].is<long>();
  bool hasBw = in["signalBandwidth"].is<float>() ||
               in["signalBandwidth"].is<int>() || in["signalBandwidth"].is<long>();
  bool hasTempCorr = in["temperatureCorrection"].is<float>() ||
                     in["temperatureCorrection"].is<int>() ||
                     in["temperatureCorrection"].is<long>();
  bool hasChipOff = in["chipTempOffset"].is<float>() ||
                    in["chipTempOffset"].is<int>() ||
                    in["chipTempOffset"].is<long>();
  // Aviso de lo que no encaja en el tipo esperado (ver "LO QUE SE IGNORA, SE
  // DICE"): un texto donde va un numero, o un decimal donde va un entero.
  ignoraSiNoEsEntero(in["beaconInterval"], "beaconInterval");
  ignoraSiNoEsNumero(in["latitude"], "latitude");
  ignoraSiNoEsNumero(in["longitude"], "longitude");
  ignoraSiNoEsEntero(in["digiMode"], "digiMode");
  ignoraSiNoEsEntero(in["frequency"], "frequency");
  ignoraSiNoEsEntero(in["spreadingFactor"], "spreadingFactor");
  ignoraSiNoEsEntero(in["codingRate4"], "codingRate4");
  ignoraSiNoEsNumero(in["signalBandwidth"], "signalBandwidth");
  ignoraSiNoEsEntero(in["power"], "power");
  ignoraSiNoEsEntero(in["heightCorrection"], "heightCorrection");
  ignoraSiNoEsNumero(in["chipTempOffset"], "chipTempOffset");
  ignoraSiNoEsNumero(in["temperatureCorrection"], "temperatureCorrection");
  ignoraSiNoEsEntero(in["sleepCutMv"], "sleepCutMv");
  ignoraSiNoEsEntero(in["sleepWakeMv"], "sleepWakeMv");
  ignoraSiNoEsEntero(in["mode"], "mode");
  ignoraSiNoEsEntero(in["epdRotation"], "epdRotation");
  ignoraSiNoEsEntero(in["screenTimeoutSecs"], "screenTimeoutSecs");
  ignoraSiNoEsEntero(in["trackerIntervalSecs"], "trackerIntervalSecs");
  ignoraSiNoEsEntero(in["trackerMinDistanceM"], "trackerMinDistanceM");
  ignoraSiNoEsEntero(in["telemetryIntervalMin"], "telemetryIntervalMin");
  ignoraSiNoEsEntero(in["wxIntervalMin"], "wxIntervalMin");
  ignoraSiNoEsEntero(in["trackerMinSpacing"], "trackerMinSpacing");
  ignoraSiNoEsEntero(in["smartBeaconPreset"], "smartBeaconPreset");

  if (in["beaconInterval"].is<int>()) {
    int v = in["beaconInterval"] | 15;
    // ★ MINIMO 15 MINUTOS (2026-09-15). Antes 10. El motivo es el mismo que en la
    //   telemetria: la baliza fija de posicion se comparte con el resto de la red, y 15 es
    //   lo que usa la comunidad (Ricardo/CA2RXU). Se avisa, no se prohibe: si alguien quiere
    //   experimentar, que lo haga, pero el firmware no lo pone facil por defecto.
    //   OJO: esto NO afecta al doble toque del boton, que manda baliza cuando el operador
    //   quiere y no pasa por este intervalo (ver trackerBeaconNow()).
    if (v < 15) { errMsg = "beaconInterval >= 15"; return false; }
    next.beaconIntervalMin = v;
  }
  if (hasLat) {
    float v = in["latitude"].as<float>();
    if (v < -90.0f || v > 90.0f) { errMsg = "latitude out of range"; return false; }
    next.latitude = v;
  }
  if (hasLon) {
    float v = in["longitude"].as<float>();
    if (v < -180.0f || v > 180.0f) { errMsg = "longitude out of range"; return false; }
    next.longitude = v;
  }
  if (in["digiMode"].is<int>()) {
    int v = in["digiMode"] | 0;
    if (v < 0 || v > 2) { errMsg = "digiMode 0..2"; return false; }
    next.digiMode = v;
  }
  if (in["frequency"].is<long>()) {
    long v = in["frequency"] | APRS_LORA_DEFAULT_FREQ_HZ;
    if (v < 430000000L || v > 928000000L) { errMsg = "frequency 430..928 MHz"; return false; }
    next.frequencyHz = v;
  }
  if (in["spreadingFactor"].is<int>()) {
    int v = in["spreadingFactor"] | 12;
    if (v < 5 || v > 12) { errMsg = "spreadingFactor 5..12"; return false; }
    next.spreadingFactor = v;
  }
  if (in["codingRate4"].is<int>()) {
    int v = in["codingRate4"] | 5;
    if (v < 5 || v > 8) { errMsg = "codingRate4 5..8"; return false; }
    next.codingRate4 = v;
  }
  if (hasBw) {
    float v = in["signalBandwidth"].as<float>();
    if (!(v == 62.5f || v == 125.0f || v == 250.0f || v == 500.0f)) {
      errMsg = "signalBandwidth 62.5/125/250/500";
      return false;
    }
    next.signalBandwidthKhz = v;
  }
  if (in["power"].is<int>()) {
    int v = in["power"] | CFG_DEFAULT_TX_POWER_DBM;
    if (v < 2) { errMsg = "power >= 2 dBm"; return false; }
    if (v > CFG_MAX_TX_POWER_DBM) v = CFG_MAX_TX_POWER_DBM;  // clamp to radio
    next.powerDbm = v;
  }
  if (in["heightCorrection"].is<int>()) {
    next.heightCorrectionM = in["heightCorrection"] | next.heightCorrectionM;
  }
  if (hasChipOff) {
    float v = in["chipTempOffset"].as<float>();
    if (v < -10.0f || v > 10.0f) { errMsg = "chipTempOffset -10..10"; return false; }
    next.chipTempOffsetC = v;
  }
  if (hasTempCorr) {
    float v = in["temperatureCorrection"].as<float>();
    if (v < -5.0f || v > 5.0f) { errMsg = "temperatureCorrection -5..5"; return false; }
    next.temperatureCorrectionC = v;
  }
  if (in["sleepCutMv"].is<int>()) {
    int v = in["sleepCutMv"] | 3400;
    if (v < 2500 || v > 4200) { errMsg = "sleepCutMv 2500..4200"; return false; }
    next.sleepCutMv = v;
  }
  if (in["sleepWakeMv"].is<int>()) {
    int v = in["sleepWakeMv"] | 3710;
    if (v < 2600 || v > 4500) { errMsg = "sleepWakeMv 2600..4500"; return false; }
    next.sleepWakeMv = v;
  }
  if (in["mode"].is<int>()) {
    int v = in["mode"] | 0;
    if (v < 0 || v > 2) { errMsg = "mode 0..2"; return false; }
    next.mode = v;
  }
  // ★ Orientacion de la pantalla de tinta electronica (0..3). Ver config.h.
  if (in["epdRotation"].is<int>()) {
    int v = in["epdRotation"] | 0;
    if (v < 0 || v > 3) { errMsg = "epdRotation 0..3"; return false; }
    next.epdRotation = (uint8_t)v;
  }
  if (in["screenTimeoutSecs"].is<int>()) {
    int v = in["screenTimeoutSecs"] | 0;
    if (v < 0 || v > 3600) { errMsg = "screenTimeoutSecs 0..3600"; return false; }
    next.screenTimeoutSecs = v;
  }
  if (in["trackerIntervalSecs"].is<int>()) {
    int v = in["trackerIntervalSecs"] | 120;
    if (v < 10 || v > 3600) { errMsg = "trackerIntervalSecs 10..3600"; return false; }
    next.trackerIntervalSecs = v;
  }
  if (in["trackerMinDistanceM"].is<int>()) {
    int v = in["trackerMinDistanceM"] | 0;
    if (v < 0 || v > 5000) { errMsg = "trackerMinDistanceM 0..5000"; return false; }
    next.trackerMinDistanceM = v;
  }
  if (in["telemetryIntervalMin"].is<int>()) {
    int v = in["telemetryIntervalMin"] | next.telemetryIntervalMin;
    // ★ 0 = solo a mano; si no, 15 minutos es el MINIMO y 720 (12 h) el maximo
    //   (2026-09-15, peticion del operador: con 10 minutos se satura la red).
    if (v != 0 && (v < 15 || v > 720)) {
      errMsg = "telemetryIntervalMin: 0 (solo manual) o 15..720 min";
      return false;
    }
    next.telemetryIntervalMin = v;
  }
  // ★ INTERVALO DE METEOROLOGIA (2026-09-15): el paquete WX iba FIJO a 15 minutos
  //   escrito dentro de main.cpp y no habia forma de cambiarlo. Ahora es un ajuste
  //   con la MISMA regla que la telemetria: 0 = solo a mano (no se programa ningun
  //   envio automatico), o 15..720 minutos con pasos de 15 en el menu.
  //   OJO con la trampa de este fichero: `in` es un JsonVariantConst, asi que las
  //   comprobaciones MUTABLES (is<JsonArray>(), is<JsonObject>()) dan siempre false y
  //   se saltan el bloque en silencio devolviendo true. Para numeros is<int>() va bien
  //   (igual que en la telemetria); para objetos/arrays hay que usar las versiones
  //   const (is<JsonObjectConst>()), como hace la comprobacion de arriba.
  if (in["wxIntervalMin"].is<int>()) {
    int v = in["wxIntervalMin"] | next.wxIntervalMin;
    if (v != 0 && (v < 15 || v > 720)) {
      errMsg = "wxIntervalMin: 0 (solo manual) o 15..720 min";
      return false;
    }
    next.wxIntervalMin = v;
  }
  if (in["trackerMinSpacing"].is<int>()) {
    int v = in["trackerMinSpacing"] | next.trackerMinSpacingSecs;
    if (v < 10 || v > 120) { errMsg = "trackerMinSpacing 10..120"; return false; }
    next.trackerMinSpacingSecs = v;
  }
  if (in["smartBeaconPreset"].is<int>()) {
    int v = in["smartBeaconPreset"] | 0;
    if (v < 0 || v > 3) { errMsg = "smartBeaconPreset 0..3"; return false; }
    next.smartBeaconPreset = v;
  }

  // --- perfiles de uso (SSID + tiempos por perfil) ---
  // ★ OJO CON LA COMPROBACION "CONST" (arreglado 2026-09-15): en un JsonVariantConst hay
  //   que comprobar la version CONST (JsonArrayConst). La comprobacion mutable
  //   (JsonArray) SIEMPRE da false en un variant constante -- es el mismo fallo que la
  //   nota de arriba documenta para JsonObject/JsonObjectConst, que si estaba corregido.
  //   Consecuencia mientras estuvo mal: los perfiles NO se aplicaban NUNCA por JSON y el
  //   protocolo contestaba ok:true sin cambiar nada (medido en el nodo de N0CALL).
  if (!in["profiles"].isNull() && !in["profiles"].is<JsonArrayConst>()) {
    ignoraAvisa("profiles");   // p.ej. un objeto en vez de una lista: no se lee NADA de el
  }
  if (in["profiles"].is<JsonArrayConst>()) {
    JsonArrayConst arr = in["profiles"].as<JsonArrayConst>();
    int n = (int)arr.size();
    if (n > 4) n = 4;
    // SSIDs distintos entre los perfiles de TRACKER (1,2,3): regla anti-duplicado. El 0
    // (fijo/digi) usa el callsign del nodo y su SSID se ignora, pero se valida como dato.
    for (int i = 0; i < n; i++) {
      if (!arr[i].is<JsonObjectConst>()) continue;   // entrada rara: se salta, no se inventa
      JsonObjectConst p = arr[i];
      uint8_t s = (uint8_t)(p["ssid"] | 0);
      if (s > 15) { errMsg = "profile ssid 1..15"; return false; }
      next.profileSsid[i] = s;
      int slow = p["slowSec"] | 0;
      int fast = p["fastSec"] | 0;
      int dist = p["distM"] | 0;
      if (slow < 0 || slow > 3600) { errMsg = "profile slowSec 0..3600"; return false; }
      if (fast  < 0 || fast  > 3600) { errMsg = "profile fastSec 0..3600"; return false; }
      if (dist  < 0 || dist  > 5000) { errMsg = "profile distM 0..5000"; return false; }
      next.profileSlowSec[i] = slow;
      next.profileFastSec[i] = fast;
      next.profileDistM[i] = dist;
      // ★ ICONO DEL MAPA POR PERFIL (2026-09-15). Se acepta el PAR ("/#", "/b",
      //   "/[" o "/>") en UNA sola cadena, que es como se escribe un icono APRS
      //   (tabla + codigo, APRS101 cap. 20) y como lo enseña el configurador. Si
      //   llega un solo caracter se toma como CODIGO y la tabla se queda como
      //   esta (asi una configuracion vieja o un script corto no rompen nada).
      //   Se guarda partido en profileOverlay[i] + profileSymbol[i], que es como
      //   lo necesita el montador de tramas.
      char sym[4] = "";
      strncpy(sym, p["symbol"] | "", sizeof(sym) - 1);
      if (sym[0] != '\0') {
        if (sym[1] != '\0') {
          if (!jsonSymbolTable(sym[0], next.profileOverlay[i][0])) {
            errMsg = "profile symbol: tabla / \\ 0-9 A-Z";
            return false;
          }
          if (!jsonSymbolCodeOk(sym[1])) {
            errMsg = "profile symbol: 2o caracter ASCII imprimible";
            return false;
          }
          next.profileSymbol[i][0] = sym[1];
        } else {
          if (!jsonSymbolCodeOk(sym[0])) {
            errMsg = "profile symbol: caracter ASCII imprimible";
            return false;
          }
          next.profileSymbol[i][0] = sym[0];
        }
        next.profileSymbol[i][1] = '\0';
      }
      // Clave suelta para la tabla/overlay, por si algun dia interesa mandarlas
      // por separado (hoy nadie lo hace: el configurador manda el par).
      if (p["overlay"].is<const char *>()) {
        if (!jsonSymbolTable((p["overlay"] | "/")[0], next.profileOverlay[i][0])) {
          errMsg = "profile overlay: tabla / \\ 0-9 A-Z";
          return false;
        }
      }
    }
    // anti-duplicado entre los perfiles 1..3 (los de tracker con SSID propio)
    for (int i = 1; i < 4; i++) {
      for (int j = i + 1; j < 4; j++) {
        if (next.profileSsid[i] != 0 && next.profileSsid[i] == next.profileSsid[j]) {
          errMsg = "profiles no pueden repetir el mismo SSID";
          return false;
        }
      }
    }
  }

  // --- booleans ---
  // NOTE: keep the "if present" style here. Assigning unconditionally with a
  // default would silently reset every switch on any partial update (menu, CLI
  // "set", remote command): that bug turned WX and telemetry off unnoticed.
  // jsonBool() accepts a real JSON bool AND 0/1: the CLI types "set x 1" as a
  // number, and with a bare is<bool>() the change was ignored while the CLI
  // still answered "OK" (found with queriesEnabled).
  next.cadActive = jsonBool(in["cadActive"], next.cadActive);
  next.sendBatteryTelemetry =
      jsonBool(in["sendBatteryTelemetry"], next.sendBatteryTelemetry);
  next.wxSensorActive = jsonBool(in["wxSensorActive"], next.wxSensorActive);
  next.txDisabled = jsonBool(in["txDisabled"], next.txDisabled);
  next.remoteEnabled = jsonBool(in["remoteEnabled"], next.remoteEnabled);
  // Lo que no encaja en un booleano (un texto, un array...) se avisa. jsonBool()
  // acepta bool y 0/1 a proposito (el CLI escribe numeros), asi que un 2 se lee
  // como "si" y NO se avisa: es una convencion documentada, no un tipo erroneo.
  ignoraSiNoEncaja(in["cadActive"], "cadActive");
  ignoraSiNoEncaja(in["sendBatteryTelemetry"], "sendBatteryTelemetry");
  ignoraSiNoEncaja(in["wxSensorActive"], "wxSensorActive");
  ignoraSiNoEncaja(in["txDisabled"], "txDisabled");
  ignoraSiNoEncaja(in["remoteEnabled"], "remoteEnabled");
  // TNC bridge protocol (0 off / 1 TNC2 / 2 KISS). Backward compatibility with
  // the old boolean "tncMode": true becomes 1 (TNC2 text, what it used to be),
  // false becomes 0. Never 2: a configuration saved before KISS cannot turn the
  // node into a binary KISS modem on its own. The CLI reaches this same merge,
  // so "set tncMode on" and "set tncProtocol 2" both work. When both keys arrive
  // in the same object the new one wins (the old one is already a mirror).
  if (in["tncProtocol"].is<int>() || in["tncProtocol"].is<bool>()) {
    int v = in["tncProtocol"].as<int>();
    if (v < CFG_TNC_OFF || v > CFG_TNC_KISS) {
      errMsg = "tncProtocol 0..2";
      return false;
    }
    next.tncProtocol = (uint8_t)v;
  } else if (in["tncMode"].is<bool>() || in["tncMode"].is<int>()) {
    next.tncProtocol = (in["tncMode"].as<int>() != 0) ? CFG_TNC_TNC2 : CFG_TNC_OFF;
  }
  // PIN de emparejamiento Bluetooth: EXACTAMENTE 6 dígitos (lo exige el
  // SoftDevice: BLE_GAP_PASSKEY_LEN). Se acepta texto ("123456") y número
  // (123456), porque el configurador manda texto y un JSON hecho a mano puede
  // llevar el número: con un solo tipo el otro se ignoraba en silencio.
  if (in["blePin"].is<const char *>() || in["blePin"].is<long long>() ||
      in["blePin"].is<int>()) {
    char p[8];
    if (in["blePin"].is<const char *>()) {
      strncpy(p, in["blePin"] | "", sizeof(p) - 1);
      p[sizeof(p) - 1] = '\0';
    } else {
      snprintf(p, sizeof(p), "%06ld", (long)in["blePin"].as<long>());
    }
    bool ok = (strlen(p) == 6);
    for (size_t i = 0; ok && i < 6; i++) {
      if (p[i] < '0' || p[i] > '9') ok = false;
    }
    if (!ok) {
      errMsg = "blePin: 6 digitos";
      return false;
    }
    strncpy(next.blePin, p, sizeof(next.blePin) - 1);
  }
  next.bleEnabled = jsonBool(in["bleEnabled"], next.bleEnabled);
  next.sceneAutoAdvance =
      jsonBool(in["sceneAutoAdvance"], next.sceneAutoAdvance);
  next.popups = jsonBool(in["popups"], next.popups);
  next.sendAltitude = jsonBool(in["sendAltitude"], next.sendAltitude);
  next.gpsEco = jsonBool(in["gpsEco"], next.gpsEco);
  next.trackerSleep = jsonBool(in["trackerSleep"], next.trackerSleep);
  next.gpsInDigi = jsonBool(in["gpsInDigi"], next.gpsInDigi);
  next.queriesEnabled = jsonBool(in["queriesEnabled"], next.queriesEnabled);
  ignoraSiNoEncaja(in["bleEnabled"], "bleEnabled");
  ignoraSiNoEncaja(in["sceneAutoAdvance"], "sceneAutoAdvance");
  ignoraSiNoEncaja(in["popups"], "popups");
  ignoraSiNoEncaja(in["sendAltitude"], "sendAltitude");
  ignoraSiNoEncaja(in["gpsEco"], "gpsEco");
  ignoraSiNoEncaja(in["trackerSleep"], "trackerSleep");
  ignoraSiNoEncaja(in["gpsInDigi"], "gpsInDigi");
  ignoraSiNoEncaja(in["queriesEnabled"], "queriesEnabled");

  // "Ambos" (2) is an always-on digipeater: the timed sleep would stop the
  // repetition between beacons, so it is forced off in that mode.
  if (next.mode == 2) next.trackerSleep = false;

  // cross-field sanity
  if (next.sleepWakeMv <= next.sleepCutMv) {
    errMsg = "sleepWakeMv must be > sleepCutMv";
    return false;
  }

  // ★ AVISO DE LO QUE SE HA IGNORADO (ver "LO QUE SE IGNORA, SE DICE" arriba).
  //   Un error explicito (errMsg ya puesto) manda sobre el aviso: es mas grave y
  //   hay que arreglarlo primero. Si no lo hay, la operacion se aplica igual
  //   (contrato de "merge parcial") pero NO se calla: el mensaje dice que claves
  //   no se han podido leer, y queda tambien en el registro del viaje.
  //   Ojo con la distincion, que es la que hace util el aviso: "ignorada" NO es
  //   "no aplicada". Si el valor no encaja en el tipo, el campo se queda COMO
  //   ESTABA (el merge solo escribe lo que entiende), asi que el operador tiene
  //   que saber que aquello que mando no ha entrado.
  if (gHuboIgnoradas) {
    if (errMsg.length() == 0) {
      errMsg = "ignorado (tipo incorrecto): ";
      errMsg += gIgnoradas;
    }
    flogLine("CFG ignoradas: %s", gIgnoradas);
  }

  cfg = next;  // atomic: only applied when fully valid
  return true;
}

void configToJson(const DigiConfig &cfg, JsonObject o) {
  o["callsign"] = cfg.callsign;
  // ★ El tocall se PUBLICA, pero no se guarda ni se acepta (2026-09-21): sale siempre la
  //   matricula declarada por el firmware. Asi quien pregunte por el cable (configurador,
  //   app o `get tocall`) ve lo que el nodo manda DE VERDAD, y no puede fiarse de un valor
  //   viejo que hubiera quedado en la flash.
  o["tocall"] = DigiConfig::kKachoSystemTocall;
  o["path"] = cfg.path;
  o["pathDigi"] = cfg.pathDigi;
  o["pathTracker"] = cfg.pathTracker;
  o["pathBoth"] = cfg.pathBoth;
  o["comment"] = cfg.comment;
  o["status"] = cfg.status;
  o["msgText"] = cfg.msgText;
  o["msgRetries"] = cfg.msgRetries;
  o["sotaRecent"] = cfg.sotaRecent;
  o["sotaLast"] = cfg.sotaLast;
  o["sotaPlan"] = cfg.sotaPlan;
  o["overlay"] = cfg.overlay;
  o["posAmbiguity"] = cfg.posAmbiguity;
  o["compressedPos"] = cfg.compressedPos;
  char sym[2] = {cfg.symbol, 0};
  o["symbol"] = sym;
  o["beaconInterval"] = cfg.beaconIntervalMin;
  o["latitude"] = cfg.latitude;
  o["longitude"] = cfg.longitude;
  o["digiMode"] = cfg.digiMode;
  o["blacklist"] = cfg.blacklist;
  o["frequency"] = cfg.frequencyHz;
  o["spreadingFactor"] = cfg.spreadingFactor;
  o["codingRate4"] = cfg.codingRate4;
  o["signalBandwidth"] = cfg.signalBandwidthKhz;
  o["power"] = cfg.powerDbm;
  o["cadActive"] = cfg.cadActive;
  o["sendBatteryTelemetry"] = cfg.sendBatteryTelemetry;
  o["wxSensorActive"] = cfg.wxSensorActive;
  o["telemetryIntervalMin"] = cfg.telemetryIntervalMin;
  // Intervalo del paquete meteorologico propio (0 = solo a mano). Mismo nombre que
  // en la entrada, para que `set wxIntervalMin 55`, el menu y el configurador web
  // usen todos la misma clave.
  o["wxIntervalMin"] = cfg.wxIntervalMin;
  o["heightCorrection"] = cfg.heightCorrectionM;
  o["temperatureCorrection"] = cfg.temperatureCorrectionC;
  o["chipTempOffset"] = cfg.chipTempOffsetC;
  o["sleepCutMv"] = cfg.sleepCutMv;
  o["sleepWakeMv"] = cfg.sleepWakeMv;
  o["txDisabled"] = cfg.txDisabled;
  o["remoteEnabled"] = cfg.remoteEnabled;
  o["tncProtocol"] = cfg.tncProtocol;  // 0 off / 1 TNC2 / 2 KISS
  // Kept for compatibility with saved files and scripts written before the
  // selector: it is only a mirror of the protocol (0 = off, anything else = on).
  o["tncMode"] = (cfg.tncProtocol != CFG_TNC_OFF);
  o["managers"] = cfg.managers;
  o["bleEnabled"] = cfg.bleEnabled;
  o["blePin"] = cfg.blePin;
  o["mode"] = cfg.mode;
  o["epdRotation"] = cfg.epdRotation;
  o["sleepMsg"] = cfg.sleepMsg;
  o["sceneAutoAdvance"] = cfg.sceneAutoAdvance;
  o["screenTimeoutSecs"] = cfg.screenTimeoutSecs;
  o["popups"] = cfg.popups;
  o["trackerIntervalSecs"] = cfg.trackerIntervalSecs;
  o["trackerMinDistanceM"] = cfg.trackerMinDistanceM;
  o["trackerMinSpacing"] = cfg.trackerMinSpacingSecs;
  o["smartBeaconPreset"] = cfg.smartBeaconPreset;
  o["sendAltitude"] = cfg.sendAltitude;
  o["gpsEco"] = cfg.gpsEco;
  o["trackerSleep"] = cfg.trackerSleep;
  o["gpsInDigi"] = cfg.gpsInDigi;
  // Perfiles de uso: SSID y tiempos/metros por perfil (0=fijo 1=peaton 2=bici 3=coche).
  JsonArray prof = o["profiles"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject p = prof.add<JsonObject>();
    p["ssid"] = cfg.profileSsid[i];
    p["slowSec"] = cfg.profileSlowSec[i];
    p["fastSec"] = cfg.profileFastSec[i];
    p["distM"] = cfg.profileDistM[i];
    // ★ ICONO POR PERFIL (2026-09-15): el PAR que se lee en el configurador y en
    //   la lista del menu ("/#", "/[", "/b", "/>"), no los dos campos sueltos.
    char ic[4];
    const char tabla = cfg.profileOverlay[i][0] ? cfg.profileOverlay[i][0] : '/';
    const char cod   = cfg.profileSymbol[i][0] ? cfg.profileSymbol[i][0] : '#';
    snprintf(ic, sizeof(ic), "%c%c", tabla, cod);
    p["symbol"] = ic;
  }
  o["queriesEnabled"] = cfg.queriesEnabled;
}
