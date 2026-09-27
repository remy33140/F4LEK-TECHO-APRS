// gps.h — u-blox GPS on Serial1 (P0.20/P0.22) with power MOSFET P0.24.
// TinyGPS++ parser, eco power control (Phase C). License: GPL-3.0

#pragma once

#include <Arduino.h>

struct GpsData {
  bool fix = false;
  double lat = 0.0;
  double lon = 0.0;
  float speedKmh = 0.0f;
  float courseDeg = 0.0f;
  float altM = 0.0f;
  bool altValid = false;
  uint8_t sats = 0;      // satellites used in the fix (GGA)
  uint8_t satsInView = 0;  // satellites the module can see (GSV) - works with no fix
  uint8_t bestSnr = 0;     // best signal among those in view (dBHz)
  float hdop = 0.0f;
  bool timeValid = false;
  uint8_t utcH = 0, utcM = 0, utcS = 0;
  bool dateValid = false;  // GPS date (for the flash log)
  uint16_t utcYear = 0;
  uint8_t utcMonth = 0, utcDay = 0;
  uint32_t lastFixMs = 0;
};

void gpsInit();
void gpsPower(bool on);
bool gpsPowered();

// Raw NMEA echo to USB (diagnostics): every byte read from the GPS is also
// written to Serial. Off by default; the diag module keeps it in sync.
void gpsSetRawEcho(bool on);
bool gpsRawEcho();

void gpsUpdate();              // feed NMEA + refresh cached data
const GpsData &gpsGet();
bool gpsWaitFix(uint32_t timeoutMs);  // blocking acquire (GPS must be on)

// --- talking TO the module (diagnostics / setup) ---------------------------
// Sends any NMEA sentence ("PUBX,00" or "$PUBX,00"); the checksum is computed
// here, so the user does not have to know how to build it. Works for u-blox,
// MediaTek ($PMTK), Quectel ($PQTM), SiRF ($PSRF)...
bool gpsSendNmea(const char *sentence);

// Cold start (wipe the stored satellite data and start over): uses the command
// of the module family detected from the sentences it sends.
bool gpsColdStart();

// Detected family: "u-blox", "MediaTek", "Quectel", "SiRF" or "?" (unknown).
const char *gpsVendor();

// Which NMEA sentence types have been seen (bitmask-free: short text summary).
const char *gpsSeenSentences();

// Great-circle helpers (used by the tracker and the stations scene).
float gpsDistanceM(double lat1, double lon1, double lat2, double lon2);
float gpsBearingDeg(double lat1, double lon1, double lat2, double lon2);
