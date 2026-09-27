// gps.cpp — GPS por Serial1. En la Faketec es un u-blox con MOSFET propio en
// P0.24; en el LilyGO T-Echo es un Quectel L76K SIN MOSFET propio (se alimenta
// del MOSFET general de periferia), asi que alli `PIN_GPS_EN` vale NO_PIN y el
// core de Arduino ignora sin mas las llamadas (comprueba el rango y sale).
// TinyGPS++ (mikalhart, same parser as NavaTastic/Meshtastic).
// License: GPL-3.0

#include "gps.h"

#include "pins_board.h"   // PIN_GPS_EN segun la placa
#include "protocol.h"     // protocolHostWrite(): el eco NMEA sale por donde entro la orden

#include <TinyGPS++.h>

#include <string.h>

namespace {

TinyGPSPlus gReader;
GpsData gData;
bool gPowered = false;
bool gEcho = false;  // raw NMEA echo (diagnostics)
constexpr uint32_t kBaud = 9600;  // u-blox default

// TinyGPS++ does not expose the "satellites in view" sentence ($..GSV), which is
// the only sign of life when there is no fix yet, so the lines are also parsed
// here: field 4 is the number in view and the signal of each satellite sits
// every 4 fields after that.
char gLine[110];
uint8_t gLineLen = 0;

// Module family guessed from the sentences it sends (used by gpsColdStart).
enum class Vendor { Unknown, Ublox, MediaTek, Quectel, SiRF };
Vendor gVendor = Vendor::Unknown;
char gSeen[64] = "";  // summary of the sentence types seen, for "gps info"

void noteSeen(const char *type) {
  // type points at the 3 letters after the talker ($GP***), but the string
  // continues with the rest of the sentence: keep only those 3 characters.
  char t[4] = {type[0], type[1], type[2], '\0'};
  for (int i = 0; i < 3; i++) {
    if (t[i] == '\0' || t[i] == ',' || t[i] == '*') return;
  }
  if (strstr(gSeen, t) != nullptr) return;
  if (gSeen[0] != '\0') strncat(gSeen, ",", sizeof(gSeen) - strlen(gSeen) - 1);
  strncat(gSeen, t, sizeof(gSeen) - strlen(gSeen) - 1);
}

void parseLine(const char *line) {
  if (line[0] != '$') return;
  const char *type = line + 3;  // 3 letters after the talker
  if (strlen(type) >= 3) noteSeen(type);
  if (strncmp(type, "GSV", 3) == 0) {
    const char *s = line;
    int field = 0;
    uint8_t inView = 0;
    uint8_t best = 0;
    while (*s != '\0' && *s != '*') {
      const char *comma = strchr(s, ',');
      const size_t len = comma ? (size_t)(comma - s) : strlen(s);
      field++;
      if (field == 4 && len > 0) {
        inView = (uint8_t)atoi(s);
      } else if (field >= 8 && ((field - 8) % 4) == 0 && len > 0) {
        const int snr = atoi(s);
        if (snr > best) best = (uint8_t)snr;
      }
      if (!comma) break;
      s = comma + 1;
    }
    gData.satsInView = inView;
    gData.bestSnr = best;
    return;
  }
  // Version banners tell which chip is behind the port.
  if (strncmp(type, "TXT", 3) == 0 || strncmp(type, "VER", 3) == 0) {
    if (strstr(line, "u-blox") || strstr(line, "u-blox") ||
        strstr(line, "ROM CORE") || strstr(line, "PROTVER") ||
        strstr(line, "ublox")) {
      gVendor = Vendor::Ublox;
    }
  }
  if (strncmp(line, "$PMTK", 5) == 0) gVendor = Vendor::MediaTek;
  else if (strncmp(line, "$PQTM", 5) == 0) gVendor = Vendor::Quectel;
  else if (strncmp(line, "$PSRF", 5) == 0) gVendor = Vendor::SiRF;
}

void refresh() {
  if (gReader.location.isValid() && gReader.location.age() < 5000) {
    gData.fix = true;
    gData.lat = gReader.location.lat();
    gData.lon = gReader.location.lng();
    gData.lastFixMs = millis();
    gData.sats = gReader.satellites.isValid() ? gReader.satellites.value() : 0;
    gData.hdop = gReader.hdop.isValid() ? gReader.hdop.hdop() : 0.0f;
    gData.speedKmh = gReader.speed.isValid() ? gReader.speed.kmph() : 0.0f;
    gData.courseDeg = gReader.course.isValid() ? gReader.course.deg() : 0.0f;
    gData.altM = gReader.altitude.isValid() ? gReader.altitude.meters() : 0.0f;
    gData.altValid = gReader.altitude.isValid();
    if (gReader.time.isValid() && gReader.time.age() < 5000) {
      gData.timeValid = true;
      gData.utcH = gReader.time.hour();
      gData.utcM = gReader.time.minute();
      gData.utcS = gReader.time.second();
    }
    if (gReader.date.isValid() && gReader.date.age() < 5000) {
      gData.dateValid = true;
      gData.utcYear = gReader.date.year();
      gData.utcMonth = gReader.date.month();
      gData.utcDay = gReader.date.day();
    }
  } else if (millis() - gData.lastFixMs > 10000) {
    gData.fix = false;
  }
}

}  // namespace

void gpsInit() {
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, LOW);  // GPS off until needed (eco)
  gPowered = false;
}

void gpsPower(bool on) {
  if (on == gPowered) return;
  if (on) {
    digitalWrite(PIN_GPS_EN, HIGH);
    delay(50);
    Serial1.begin(kBaud);
  } else {
    Serial1.end();
    digitalWrite(PIN_GPS_EN, LOW);
    // No module, no satellites: do not leave stale values on screen.
    gData.satsInView = 0;
    gData.bestSnr = 0;
    gLineLen = 0;
  }
  gPowered = on;
}

bool gpsPowered() { return gPowered; }

void gpsSetRawEcho(bool on) { gEcho = on; }
bool gpsRawEcho() { return gEcho; }

void gpsUpdate() {
  if (!gPowered) return;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    gReader.encode(c);
    // Eco crudo del NMEA (diagnostico). Sale por la misma puerta que las respuestas: por el
    // cable si el diagnostico se encendio por el cable, y por el aire si se encendio desde la
    // app por Bluetooth.
    if (gEcho) protocolHostWrite(&c, 1);
    if (c == '\n') {
      gLine[gLineLen] = '\0';
      if (gLineLen > 6) parseLine(gLine);
      gLineLen = 0;
    } else if (c != '\r' && gLineLen < sizeof(gLine) - 1) {
      gLine[gLineLen++] = c;
    }
  }
  refresh();
}

const GpsData &gpsGet() { return gData; }

const char *gpsVendor() {
  switch (gVendor) {
    case Vendor::Ublox: return "u-blox";
    case Vendor::MediaTek: return "MediaTek";
    case Vendor::Quectel: return "Quectel";
    case Vendor::SiRF: return "SiRF";
    default: return "?";
  }
}

const char *gpsSeenSentences() { return gSeen[0] ? gSeen : "-"; }

// NMEA checksum: XOR of every character between '$' and '*'.
bool gpsSendNmea(const char *sentence) {
  if (!gPowered || sentence == nullptr || sentence[0] == '\0') return false;
  const char *body = (sentence[0] == '$') ? sentence + 1 : sentence;
  uint8_t cs = 0;
  const char *p = body;
  while (*p != '\0' && *p != '*') {
    cs ^= (uint8_t)(*p);
    p++;
  }
  char out[96];
  int n = snprintf(out, sizeof(out), "$%s*%02X\r\n", body, cs);
  if (n <= 0 || (size_t)n >= sizeof(out)) return false;
  Serial1.write((const uint8_t *)out, (size_t)n);
  Serial1.flush();
  return true;
}

bool gpsColdStart() {
  if (!gPowered) return false;
  if (gVendor == Vendor::MediaTek) {
    // PMTK104: full cold start (clears almanac, ephemeris and position).
    return gpsSendNmea("PMTK104");
  }
  // Default: u-blox UBX-CFG-RST with navBbrMask = 0xFFFF (cold start) and
  // resetMode = 0x02 (controlled software reset, GPS only).
  uint8_t msg[12] = {0xB5, 0x62, 0x06, 0x04, 0x04, 0x00,
                     0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00};
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 10; i++) {
    ckA = (uint8_t)(ckA + msg[i]);
    ckB = (uint8_t)(ckB + ckA);
  }
  msg[10] = ckA;
  msg[11] = ckB;
  Serial1.write(msg, sizeof(msg));
  Serial1.flush();
  return true;
}

bool gpsWaitFix(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    gpsUpdate();
    if (gData.fix) return true;
    delay(10);
  }
  return gData.fix;
}

float gpsDistanceM(double lat1, double lon1, double lat2, double lon2) {  constexpr double R = 6371000.0;
  double p1 = lat1 * M_PI / 180.0;
  double p2 = lat2 * M_PI / 180.0;
  double dp = (lat2 - lat1) * M_PI / 180.0;
  double dl = (lon2 - lon1) * M_PI / 180.0;
  double a = sin(dp / 2) * sin(dp / 2) +
             cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return (float)(2 * R * atan2(sqrt(a), sqrt(1 - a)));
}

float gpsBearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * M_PI / 180.0;
  double p2 = lat2 * M_PI / 180.0;
  double dl = (lon2 - lon1) * M_PI / 180.0;
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = atan2(y, x) * 180.0 / M_PI;
  if (b < 0) b += 360.0;
  return (float)b;
}
