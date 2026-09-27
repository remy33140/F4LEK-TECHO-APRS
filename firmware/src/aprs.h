// aprs.h — APRS-LoRa position beacon (next milestone after radio-blink)
// Frame format interoperable with the CA2RXU ecosystem: LoRa payload =
// 0x3C 0xFF 0x01 + ASCII AX25 UI text "SRC>APL2OY[,path]:!DDMM.mmN/DDDMM.mmE<sym>"
// (same as CA2RXU lora_utils.cpp:229 and cfr34k aprs_build_frame; el destino es nuestra
//  matricula APL2OY, ver el comentario de DigiConfig::tocall).
// License: GPL-3.0

#pragma once

#include "config.h"
#include "gps.h"
#include "sensors.h"

// Heard stations ring (Phase E: OLED stations scene + ?APRSD query).
struct HeardStation {
  char call[12];
  float rssi;
  float snr;
  uint32_t ms;
  bool hasPos;
  double lat;
  double lon;
};

// TX gating (N-03 / 03_seguridad): no beacon unless a real callsign and a
// position are configured (defaults NOCALL-11/0,0 never go on air).
bool aprsCanBeacon(const DigiConfig &cfg);

// Build the beacon frame into out (NUL-terminated). Appends WX telemetry
// (wxSensorActive), comment and battery voltage (sendBatteryTelemetry).
// banner (optional) is a one-shot status text ("NODO ONLINE", ...).
// Returns length, 0 when the config forbids beaconing.
size_t aprsBuildBeacon(const DigiConfig &cfg, char *out, size_t maxLen,
                       const SensorReadings &r, const char *banner = nullptr);

// Send one position beacon now (CAD-lite when cfg.cadActive). Returns
// RADIOLIB_ERR_NONE on success; custom codes: -100 = not configured,
// -101 = channel busy (CAD aborted). banner: see aprsBuildBeacon.
// manual=true marks a transmission a human asked for explicitly: automatic
// callers leave it false and are silenced while the KISS host drives the node
// (operator decision, see autoBeaconAllowed() in aprs.cpp).
int16_t aprsSendBeacon(const DigiConfig &cfg, const char *banner = nullptr,
                       bool manual = false);

// Manual "send a beacon now" (CLI `beacon`, web configurator button, OLED menu).
// Tracker modes must publish where the node really is: with mode != 0 and the GPS
// powered with a live fix it sends the tracker beacon (reason 'M' = manual);
// otherwise (mode 0, or a tracker whose GPS has no fix yet) it sends the normal
// configured-coordinate beacon. All three call sites share this decision.
int16_t aprsSendManualBeacon(const DigiConfig &cfg);

// One-shot notice beacon used by the power banners ("NODO ONLINE",
// "DURMIENDO HASTA EL SOL"). Same anti-guess rule as the manual beacon, but the
// text must survive: mode 0 (fixed station) sends the configured position
// carrying the text; in tracker modes the text rides the tracker beacon when the
// GPS has a live fix, and without a fix it becomes a status packet, never a
// guessed position. An unpowered GPS counts as "no fix".
int16_t aprsSendBannerBeacon(const DigiConfig &cfg, const char *text);

// Send any APRS-LoRa text frame (CAD-lite when cfg.cadActive). Used by the
// beacon and the digipeater TX. Returns RadioLib codes; -101 = channel busy,
// -102 = TX muted (cfg.txDisabled), -103 = no valid callsign.
// ★ THIS IS THE ONLY ROAD TO radioSendFrame(): nothing in the firmware may
// transmit without going through here, because here live the mute, the callsign
// rule and the 7-bit ASCII filter. A byte outside 0x20..0x7E in `frame` is
// replaced by '.' before anything else (APRS is 7-bit text and one accented
// letter can break the frame at an iGate); the "SRC>DST,PATH:info" structure is
// left untouched.
// bypassMute lets remote-control replies through while the node is muted (only
// authorized managers reach that path). useCad=false is for back-to-back frames
// (telemetry metadata burst).
int16_t aprsSendTextFrame(const DigiConfig &cfg, const char *frame, size_t len,
                          bool bypassMute = false, bool useCad = true);

// --- Digipeater (RX path) ---

// Bind the config pointer used by the RX handler (call once at boot).
void aprsBindConfig(DigiConfig *cfg);

// RX callback (fits radioSetRxCallback): validates an APRS-LoRa frame,
// applies blacklist/dedupe and digipeats when the path/mode allow it.
void aprsHandleRadioPacket(const uint8_t *data, size_t len, float rssi, float snr);

// Frames digipeated since boot (for the WebSerial status reply).
uint32_t aprsDigiCount();

// Callsign of the last validated received frame ("" if none).
const char *aprsLastFrom();

// --- APRS telemetry (aprs.fi channels) ---
// T# sequence + PARM/UNIT/EQNS/BITS metadata; channels Vbat(V), I(mA signed),
// Temp(C), Hum(%), Press(hPa). Gated by sendBatteryTelemetry/wxSensorActive.
int16_t aprsSendTelemetry(const DigiConfig &cfg);
int16_t aprsSendTelemetryMeta(const DigiConfig &cfg);

// Positionless weather report ("_DDHHMMz" + standard block): the packet that
// makes aprs.fi/findu show the station as a weather station, without touching
// its position or symbol. Needs a valid GPS date + time.
int16_t aprsSendWeather(const DigiConfig &cfg);

// APRS message started by the operator: ":DEST     :text{NNN" so the peer can
// acknowledge it. Respects the TX mute.
int16_t aprsSendMessage(const DigiConfig &cfg, const char *to, const char *text);

// Pending-message bookkeeping: the node resends an unacknowledged message (same
// number) every 30 s and gives up after cfg.msgRetries extra attempts. Called
// from the main loop; aprsMsgAcked() is called when an ack/rej arrives.
void aprsMsgTick(const DigiConfig &cfg, uint32_t now);
bool aprsMsgAcked(uint16_t id, bool rejected);
bool aprsMsgPending(char *to, size_t toLen, uint16_t *id, uint8_t *tries);

// Bulletin for everybody (APRS message to BLNn, group '0'..'9') and APRS object
// (";NAME     *DDHHMMz" + position; kill = remove it everywhere).
int16_t aprsSendBulletin(const DigiConfig &cfg, const char *text, char group);
int16_t aprsSendObject(const DigiConfig &cfg, const char *name, double lat,
                       double lon, const char *comment, bool kill);

// Bench helper (USB CLI "q"): answer to an APRS query without transmitting, so
// the query engine can be tested with a single board.
String aprsQueryText(DigiConfig &cfg, const String &q);

// Bench helper (USB CLI "rxs"): feed a received APRS message as if it had
// arrived over the air ("from" + info field, e.g. ":EA2OY-7  :ack001"), so the
// message, ack and remote-control paths can be tested with one board. Returns
// true when the node consumed it (replied or deliberately ignored).
bool aprsInjectMessage(DigiConfig &cfg, const char *from, const char *to,
                       const char *body);

// Bench helper (USB CLI "battest <mV>"): the " BAT BAJA" comment as it
// would be added for that voltage running on the battery ("" when it would not).
String aprsLowBatText(const DigiConfig &cfg, int mv);

// Threshold (mV) below which that warning is added: 3500, or 3600 on E22P.
int aprsLowBatWarnMv();

// Callsign of the last station heard ("" when none): default addressee for the
// "message to the last heard" menu action.
const char *aprsLastHeardCall();

// Tracker beacon with GPS data (Phase C): timestamp, lat/lon, CSE/SPD, alt.
// why: trip-log reason letter (F first fix, R rate, C corner, D distance,
// M manual). banner (optional): the same one-shot status text the fixed beacon
// carries ("NODO ONLINE", ...), appended right after the position.
// manual=true is for the transmissions a human asked for (CLI `trkbeacon`, OLED
// menu): they still go out while the KISS host drives the node. Automatic
// callers (trackerLoop) leave it false.
// lastKnown=true marks a position that is NOT from now: it is the last known
// fix, sent on purpose with ITS OWN time. It only changes the trip log (reason
// 'L' instead of 'M') so the exported route is not polluted with a stale point
// as if it were a new one.
int16_t aprsSendTrackerBeacon(const DigiConfig &cfg, const GpsData &g,
                              char why = 0, const char *banner = nullptr,
                              bool manual = false, bool lastKnown = false);

// Status packet (">text"): sent at boot and every 24 h when cfg.status is set.
// text (optional) overrides cfg.status when given: the tracker boot path uses it
// to announce the node is alive while the GPS still has no fix, without touching
// the stored configuration. Every transmission leaves one "TX STS" trip-log line
// (text, bytes and ok/err) so the boot notice can be verified.
int16_t aprsSendStatus(const DigiConfig &cfg, const char *text = nullptr);

// Heard stations (newest first). Returns how many were copied.
uint8_t aprsHeardStations(HeardStation *out, uint8_t max);
