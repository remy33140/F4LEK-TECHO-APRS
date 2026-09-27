// radio.h — SX1262 physical layer (radio-blink milestone)
// Init at APRS-LoRa 433.775 MHz SF12/BW125/CR4/5 (CA2RXU ecosystem, N-12).
// Test-only helper left: the RX log stream (OFF by default). The old periodic
// raw TX test ("RADIOBLINK") was removed: see radioSendFrame() in radio.cpp.
// License: GPL-3.0

#pragma once

#include "config.h"

// Initialize the radio (SPI + SX1262). Returns true when ready to receive.
bool radioSetup(const DigiConfig &cfg);

// Non-blocking: handles RX events, the periodic AGC reset and the LED blink.
// It NEVER transmits: every frame on the air is asked for by aprsSendTextFrame().
void radioLoop();

// Status (for the WebSerial protocol "status" reply)
bool radioReady();
int radioLastErr();        // last RadioLib return code (0 = none)
const char *radioState();  // "OFF" | "RX" | "TX" | "ERR"

// Radio module of this build: "HT-RA62" (SX1262, up to 22 dBm) or "E22P"
// (Ebyte with PA, 12 dBm of drive -> ~1 W of output). The web configurator
// uses it to warn about the power limit.
const char *radioModuleName();

// ★ PLACA de este build, para que el configurador NO TENGA QUE ADIVINARLA (2026-09-21).
//   Devuelve uno de estos tres, que son los que se fabrican:
//     "T-Echo"          -> LilyGO T-Echo y T-Echo Plus (los dos, misma electronica de
//                          bateria: celda LiPo 1S)
//     "Faketec HT-RA62" -> Faketec / ProMicro con SX1262
//     "Faketec E22P"    -> Faketec / ProMicro con E22P-433M30S (lleva booster MT3608)
//   PARA QUE SIRVE: los umbrales de bateria (corte y despertar) NO son los mismos en las
//   tres, y el configurador web no tenia forma de saber cual tenia delante: recomendaba
//   los de la Faketec a todo el mundo, y en un T-Echo eso apaga el nodo con la bateria a
//   media carga (ver config.h, sleepCutMv). Con esto, el configurador propone los suyos.
const char *boardName();
uint32_t radioRxCount();
uint32_t radioTxCount();
// millis() of the last successful TX (0 = never). The LoRa send is blocking,
// so the screen cannot repaint while the radio is on air; the OLED uses this
// to keep the TX badge visible for a short window afterwards.
uint32_t radioLastTxMs();
float radioLastRssi();
float radioLastSnr();
float radioLastFreqErr();     // frequency error of the last packet (Hz)
uint32_t radioCrcErrCount();  // CRC-failed packets since boot

// Test switch (fail-closed: OFF after boot)
void radioSetRxLog(bool on);
bool radioRxLog();

// Global TX mute (config txDisabled). Kept here for the RX-log/diag helpers and
// as the radio-level mirror of the mute; the rule that actually stops a frame
// lives in aprsSendTextFrame() (beacon, digi, telemetry, everything).
void radioSetMuted(bool muted);
bool radioMuted();

// Put the radio to sleep (SPI sleep; E22P: power pin LOW) and stop RX.
// Used by the power module before System OFF (N-03).
void radioShutdown();

// Apply a new TX power (dBm) to the running radio (live, no reboot needed).
void radioApplyPower(uint8_t dbm);

// Output power (dBm) currently applied to the radio (shown in the status).
uint8_t radioPowerDbm();

// RX callback: called from radioLoop() with each successfully received LoRa
// payload (raw bytes) + RSSI/SNR. Set once at boot (APRS RX handler).
typedef void (*RadioRxCallback)(const uint8_t *data, size_t len, float rssi,
                                float snr);
void radioSetRxCallback(RadioRxCallback cb);

// Transmit raw bytes over LoRa (called ONLY by aprsSendTextFrame(), which adds
// the 0x3C 0xFF 0x01 prefix and enforces mute/callsign/7-bit text). Re-enables RX.
// Returns the RadioLib return code (RADIOLIB_ERR_NONE = ok).
// Nothing else in the firmware may call this: one way to the air, one place to
// audit. See the note above radioSendFrame() in radio.cpp.
int16_t radioSendFrame(const uint8_t *data, size_t len);

// LoRa CAD (listen before talk): RADIOLIB_CHANNEL_FREE when the channel is free.
int16_t radioScanChannel();

// Put the radio back into RX (after CAD aborts or manual standby).
void radioRestartRx();

// Periodic AGC maintenance (warm sleep + calibrate all + re-apply boosted gain
// and the undocumented 0x8B5 RX patch). radioLoop() calls it every 60 s;
// exposed for bench tests.
void radioAgcReset();
