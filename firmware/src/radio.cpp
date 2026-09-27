// radio.cpp — SX1262 physical layer (radio-blink milestone)
// RadioLib 7.7.1. Init sequence mirrors CA2RXU lora_utils.cpp + NavaTastic
// SX126xInterface (TCXO 1.8 V with XTAL fallback, DIO2 RF switch, RXEN P0.17
// on HT-RA62, boosted RX gain). License: GPL-3.0

#include "radio.h"

#include "button.h"   // buttonNoteRadioTx(): la pastilla tactil se dispara con el RF propio

#include <Arduino.h>
#include <RadioLib.h>

// Radio wiring (pins_board.h):
//   - LilyGO T-Echo / Plus -> pins_techo.h   (SX1262; NO tiene RXEN: usa el DIO2 interno)
// El resto de este fichero no cambia: solo cambian los numeros de pin.
#include "pins_board.h"
#include "tnc.h"

namespace {

// SX1262 subclass exposing the protected register write so the undocumented
// Heltec/Semtech RX sensitivity patch (bit 0 of 0x8B5, via Meshtastic
// #9571/#9777) can be applied and re-applied after internal calibrations.
class SX1262Ex : public SX1262 {
 public:
  explicit SX1262Ex(Module *mod) : SX1262(mod) {}
  int16_t writeReg8(uint16_t addr, uint8_t value) {
    return writeRegister(addr, &value, 1);
  }
};

SX1262Ex *gRadio = nullptr;
int gLastErr = 0;
bool gReady = false;
bool gRxLog = false;
bool gMuted = false;
uint32_t gRxCount = 0;
uint32_t gTxCount = 0;
float gLastRssi = 0.0f;
float gLastSnr = 0.0f;
float gLastFreqErr = 0.0f;
uint32_t gCrcErrCount = 0;

volatile bool gRxFlag = false;
volatile bool gTxBusy = false;

RadioRxCallback gRxCb = nullptr;

uint32_t gLedUntilMs = 0;
uint32_t gLastSentMs = 0;  // last successful TX, whatever the source (OLED badge)
uint8_t gPowerDbm = 0;     // output power currently applied to the radio

constexpr uint32_t kAgcResetMs = 60000;  // Meshtastic AGC reset cadence
constexpr uint32_t kHeartbeatMs = 2000;      // Meshtastic-style heartbeat
constexpr uint32_t kCounterWrap = 10000;     // counters wrap to 0 (display/telemetry)
constexpr uint8_t kMaxPacketLen = 255;

uint32_t gLastBeatMs = 0;
uint32_t gLastAgcMs = 0;
float gFreqMHz = 433.775f;

void onDio1() {
  if (!gTxBusy) gRxFlag = true;
}

void ledPulse(uint32_t ms) {
  gLedUntilMs = millis() + ms;
  digitalWrite(LED_BUILTIN, HIGH);
}

void ledUpdate() {
  digitalWrite(LED_BUILTIN, (int32_t)(millis() - gLedUntilMs) < 0 ? HIGH : LOW);
}

void printRxLog(size_t len) {
  Serial.print(F("{\"radio\":\"rx\",\"len\":"));
  Serial.print(len);
  Serial.print(F(",\"rssi\":"));
  Serial.print(gLastRssi, 1);
  Serial.print(F(",\"snr\":"));
  Serial.print(gLastSnr, 1);
  Serial.println(F("}"));
}

void handleRx() {
  uint8_t buf[kMaxPacketLen];
  size_t len = (size_t)gRadio->getPacketLength();
  if (len > sizeof(buf)) len = sizeof(buf);
  int16_t st = gRadio->readData(buf, len);
  if (st == RADIOLIB_ERR_NONE) {
    gRxCount = (gRxCount + 1) % kCounterWrap;
    gLastRssi = gRadio->getRSSI(true);
    gLastSnr = gRadio->getSNR();
    gLastFreqErr = gRadio->getFrequencyError();
    ledPulse(40);
    if (gRxCb) gRxCb(buf, len, gLastRssi, gLastSnr);
    // The RX log is a text line nobody asked for: it must not land in the middle
    // of a TNC/KISS session (the app would see it as garbage between frames).
    if (gRxLog && !tncActive()) printRxLog(len);
  } else if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    gCrcErrCount = (gCrcErrCount + 1) % kCounterWrap;
    if (gRxLog && !tncActive()) Serial.println(F("{\"radio\":\"crc\"}"));
  }
}

}  // namespace

// ★ UNICO CAMINO AL AIRE (2026-09-15).
// De aqui para arriba NO hay ninguna otra llamada: todo lo que se transmite pasa
// por aprsSendTextFrame(), que es donde viven el mute, la norma del indicativo y
// el filtro de 7 bits. Aqui habia antes doTestTx(), que emitia "RADIOBLINK NNNN"
// cada 10 s por este mismo camino directo, SIN prefijo LoRa, SIN formato APRS y
// SIN indicativo: se la saltaba todo. Se elimino por decision del operador (un
// aparato que va al monte no lleva dentro una funcion capaz de emitir basura
// periodica). Si algun dia hace falta una prueba de radio, que pase por
// aprsSendTextFrame() como todo lo demas.
int16_t radioSendFrame(const uint8_t *data, size_t len) {
  if (!gReady || !gRadio) return RADIOLIB_ERR_UNKNOWN;
  // ★ T-ECHO PROJECT BUTTER (2026-09-15): se apunta la VENTANA en que el emisor esta
  //   en el aire. La pastilla capacitiva del T-Echo se dispara con el RF propio (es un
  //   fenomeno conocido de esta placa y lo dice el autor del firmware de referencia:
  //   "The transmitter interferes with the touch button"), asi que la capa del boton
  //   descarta cualquier toque cuyo flanco caiga aqui dentro: eso es un toque FANTASMA,
  //   no un dedo. El fisico no se toca: si el operador pulsa, es el.
  const uint32_t txInicioMs = millis();
  gTxBusy = true;
  int16_t st = gRadio->transmit(data, len);
  gTxBusy = false;
  buttonNoteRadioTx(txInicioMs, millis());
  gRadio->startReceive();
  if (st == RADIOLIB_ERR_NONE) {
    gTxCount = (gTxCount + 1) % kCounterWrap;
    gLastSentMs = millis();
    ledPulse(150);
  } else {
    gLastErr = st;
  }
  return st;
}

bool radioSetup(const DigiConfig &cfg) {
  SPI.begin();  // variant pins (P0.02/P1.15/P1.11)

  static Module module(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
  static SX1262Ex radio(&module);
  gRadio = &radio;

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  float freqMHz = (float)cfg.frequencyHz / 1000000.0f;
  gFreqMHz = freqMHz;
  int8_t power = (int8_t)cfg.powerDbm;

  // TCXO 1.8 V first; XTAL retry if the module has no TCXO (N-02 TCXO_OPTIONAL)
  int16_t st = radio.begin(freqMHz, cfg.signalBandwidthKhz, cfg.spreadingFactor,
                           cfg.codingRate4, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                           power, 8, SX126X_DIO3_TCXO_VOLTAGE, false);
  if (st != RADIOLIB_ERR_NONE) {
    st = radio.begin(freqMHz, cfg.signalBandwidthKhz, cfg.spreadingFactor,
                     cfg.codingRate4, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                     power, 8, 0.0f, false);
  }
  if (st != RADIOLIB_ERR_NONE) {
    gLastErr = st;
    return false;
  }

  radio.setDio1Action(onDio1);
  radio.setCRC(true);
  // Current limit for the PA (standard value in this ecosystem).
  // BENCH NOTE: raising it to 200 mA was tested on 2026-09-11 to see whether the
  // HT-RA62 amplifier was being starved; it made no difference, so it is back to
  // 140 mA. If a "hears but is not heard" case appears again, look at the radio
  // module / antenna first.
  radio.setCurrentLimit(140);          // mA
  radio.setRxBoostedGainMode(true);    // SX1262 RX sensitivity
  gPowerDbm = (uint8_t)power;          // remember what was applied (status)
  radio.setDio2AsRfSwitch(true);   // conmutacion TX/RX interna (DIO2)
  // HT-RA62: P0.17 = RXEN = enable del camino RX/LNA (RadioLib lo sube en RX y
  // lo baja en TX/idle). E22P: RADIOLIB_NC (usa su pin de alimentacion).
  radio.setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);

  // Undocumented RX sensitivity patch: bit 0 of register 0x8B5 (Heltec/Semtech
  // via Meshtastic #9571/#9777). CALIBRATE_ALL clears it, so radioAgcReset()
  // re-applies it every minute.
  int16_t patchSt = radio.writeReg8(0x08B5, 0x01);
  if (!tncActive()) {
    Serial.print("radio: 0x8B5 RX patch ");
    Serial.println(patchSt == RADIOLIB_ERR_NONE ? "applied" : "FAILED");
  }

  radio.startReceive();
  gReady = true;
  return true;
}

void radioLoop() {
  if (!gReady) return;

  uint32_t now = millis();

  // NOTE: here went the periodic raw TX test (doTestTx, "RADIOBLINK"). radioLoop()
  // no longer transmits anything by itself: it only services RX, the AGC reset and
  // the LED. Every frame on the air is asked for by aprsSendTextFrame().

  if (gRxFlag) {
    gRxFlag = false;
    handleRx();
  }

  // Periodic AGC reset so the 0x8B5 RX patch survives internal calibrations
  if (now - gLastAgcMs >= kAgcResetMs) {
    gLastAgcMs = now;
    radioAgcReset();
  }

  // Meshtastic-style heartbeat: short blink every 2 s when nothing else lights
  if (now - gLastBeatMs >= kHeartbeatMs) {
    gLastBeatMs = now;
    if ((int32_t)(now - gLedUntilMs) >= 0) ledPulse(30);
  }

  ledUpdate();
}

bool radioReady() { return gReady; }

int radioLastErr() { return gLastErr; }

const char *radioState() {
  if (!gReady) return gLastErr ? "ERR" : "OFF";
  return gTxBusy ? "TX" : "RX";
}

const char *radioModuleName() {
  return "SX1262";
}

// Placa de este build. Las tres que se fabrican, y el porque en radio.h.
// OJO: el T-Echo normal y el Plus comparten electronica de bateria (LiPo 1S), asi que para
// lo que esto sirve (los umbrales de bateria) son la misma cosa.
const char *boardName() {
  return "T-Echo";
}

uint32_t radioRxCount() { return gRxCount; }
uint32_t radioTxCount() { return gTxCount; }
uint32_t radioLastTxMs() { return gLastSentMs; }
float radioLastRssi() { return gLastRssi; }
float radioLastSnr() { return gLastSnr; }
float radioLastFreqErr() { return gLastFreqErr; }
uint32_t radioCrcErrCount() { return gCrcErrCount; }

void radioSetRxLog(bool on) { gRxLog = on; }
bool radioRxLog() { return gRxLog; }

void radioSetMuted(bool muted) { gMuted = muted; }
bool radioMuted() { return gMuted; }

void radioShutdown() {
  // No power pin: the module sleeps via SPI (radio.sleep()).
  if (gReady && gRadio) gRadio->sleep(true);  // keepConfig (N-02)
  gReady = false;
  gRxFlag = false;
}

void radioApplyPower(uint8_t dbm) {
  if (!gReady || !gRadio) return;
  gRadio->setOutputPower((int8_t)dbm);
  gPowerDbm = dbm;
}

uint8_t radioPowerDbm() { return gPowerDbm; }

void radioSetRxCallback(RadioRxCallback cb) { gRxCb = cb; }

int16_t radioScanChannel() {
  if (!gReady || !gRadio) return RADIOLIB_ERR_UNKNOWN;
  return gRadio->scanChannel();
}

void radioRestartRx() {
  if (!gReady || !gRadio) return;
  gRadio->startReceive();
}

// Periodic AGC maintenance (Meshtastic/NavaTastic resetAGC pattern): a warm
// sleep powers down the analog frontend (a plain standby->RX cycle does NOT
// reset a stuck AGC), then calibrate all blocks, re-tune image rejection,
// re-apply boosted gain + the 0x8B5 patch and resume RX.
void radioAgcReset() {
  if (!gReady || !gRadio) return;
  gTxBusy = true;  // DIO1 during the reset must not be treated as RX
  gRadio->sleep(true);
  gRadio->standby(RADIOLIB_SX126X_STANDBY_RC, true);
  gRadio->calibrate(RADIOLIB_SX126X_CALIBRATE_ALL);
  gRadio->calibrateImage(gFreqMHz);
  gRadio->setDio2AsRfSwitch(true);
  gRadio->setRxBoostedGainMode(true);
  gRadio->writeReg8(0x08B5, 0x01);
  gRadio->startReceive();
  gTxBusy = false;
}
