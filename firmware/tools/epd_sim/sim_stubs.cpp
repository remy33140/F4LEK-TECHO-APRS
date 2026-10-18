// The world around the e-paper driver, simulated: GPS, battery, radio, heard stations, clock.
// Values come from SimWorld (sim_world.h), set by each scenario in sim_main.cpp.
#include <Arduino.h>
#include <nrfx_spim.h>

#include "aprs.h"
#include "cli.h"
#include "config.h"
#include "diag.h"
#include "gps.h"
#include "power.h"
#include "radio.h"
#include "sensors.h"
#include "store.h"
#include "tnc.h"
#include "tracker.h"
#include "display.h"
#include "sim_world.h"

SimWorld gWorld;
SimSerial Serial;
SimSpim gSimSpim2;
SimSaadc gSimSaadc;
SimGpio gSimP0, gSimP1;
SimPower gSimPower;

uint32_t millis() { return gWorld.nowMs; }
uint32_t micros() { return gWorld.nowMs * 1000u; }

const GpsData &gpsGet() { return gWorld.gps; }
float gpsDistanceM(double lat1, double lon1, double lat2, double lon2) {
  const double k = 3.14159265358979 / 180.0;
  const double dLat = (lat2 - lat1) * k, dLon = (lon2 - lon1) * k;
  const double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * k) * cos(lat2 * k) * sin(dLon / 2) * sin(dLon / 2);
  return (float)(2 * 6371000.0 * asin(sqrt(a)));
}
float sensorsBatteryVolt(const SensorReadings &) { return gWorld.batV; }

uint8_t aprsHeardStations(HeardStation *out, uint8_t max) {
  uint8_t n = 0;
  for (; n < max && n < gWorld.nHeard; n++) out[n] = gWorld.heard[n];
  return n;
}
const char *aprsLastHeardCall() { return gWorld.nHeard ? gWorld.heard[0].call : ""; }
int16_t aprsSendManualBeacon(const DigiConfig &) { return 0; }
// Same display notes as the real aprsSendMessage() (aprs.cpp).
int16_t aprsSendMessage(const DigiConfig &, const char *to, const char *) {
  displayNoteTx("MSG");
  displayNoteMsgSent(to);
  return 0;
}
int16_t aprsSendTelemetry(const DigiConfig &) { return 0; }
int16_t aprsSendTelemetryMeta(const DigiConfig &) { return 0; }
bool cliTypedSet(DigiConfig &, const String &, const String &, String &) { return true; }
bool diagTrazaArranque() { return false; }
bool diagTrazaTaller() { return false; }
void powerSleepNow(const DigiConfig &) {}
bool powerUsbPresent() { return false; }
uint32_t radioLastTxMs() { return gWorld.lastTxMs; }
bool radioReady() { return true; }
void radioSetMuted(bool) {}
bool storeSave(const DigiConfig &) { return true; }
bool storeWipe() { return true; }
bool tncKissActive() { return false; }
bool tncKissPaused() { return false; }
int16_t trackerBeaconNow(const DigiConfig &) { return 0; }
void trackerSetCoordsStart() {}
void trackerSetCoordsCancel() {}
uint8_t trackerSetCoordsEstado() { return 0; }
uint8_t trackerSetCoordsDone() { return 0; }
uint8_t trackerSetCoordsNeed() { return 0; }
void flogLine(const char *, ...) {}
