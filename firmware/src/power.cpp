// power.cpp — resilience: battery monitor + deep sleep / LPCOMP wake (N-03)
// LPCOMP wake math (NavaTastic main-nrf52.cpp): the comparator reference is a
// fraction of the ~3.3 V regulated rail; with the 0.5 divider the battery wake
// voltage is 2 * fraction * 3300 mV. nRF52840 REFSEL offers all n/16 steps
// (even n as k/8, odd n as k/16). License: GPL-3.0

#include "power.h"

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <nrf.h>

// OJO: el pin del ADC de bateria NO se toma de aqui, sino de POWER_BATTERY_ADC_PIN (mas
// abajo, junto a powerReadMv): en el T-Echo el PIN_BATTERY_ADC de pins_techo.h apunta al
// SCK de la pantalla de tinta. De este fichero se usan BATTERY_LPCOMP_AIN y BATTERY_DIVIDER,
// que si son correctos en las dos placas.
#include "pins_board.h"   // BATTERY_LPCOMP_AIN y BATTERY_DIVIDER segun la placa
#include "aprs.h"
#include "ble_kiss.h"   // bleShutdown(): el SoftDevice TIENE que estar abajo para dormir
#include "display.h"
#include "flog.h"
#include "gps.h"
#include "radio.h"
#include "tnc.h"

// Defined in variants/faketec/variant.cpp (button low-level SENSE wake).
void variant_shutdown();

namespace {
volatile bool gRtcWake = false;
uint8_t gLowCount = 0;  // consecutive low-battery readings (diagnostics)

// How long the final "going to sleep" popup stays on screen before System OFF
// (long enough to be read; the RF notice goes out right after it).
constexpr uint32_t kSleepNoticeMs = 5000;

// Low-battery policy (NavaTastic): a single threshold and 8 readings IN A ROW.
// The 20 s spacing keeps the total at ~160 s; far-out RF can make the divider
// return bogus values, so one bad reading must reset the counter and there is
// deliberately NO fast path to sleep.
constexpr uint8_t kLowReadingsNeeded = 8;
constexpr uint32_t kLowReadIntervalMs = 20000;

// Emergency fallback if the LPCOMP never reports READY (see armLpcompWake):
// sleep on the RTC for this long (microamps) and retry after the reboot.
constexpr uint32_t kLpcompFallbackSecs = 900;  // 15 min

// RTC2 timed sleep in blocks of max 500 s, then a clean reboot (System OFF
// cannot be woken by a timer). The board must already be quiet.
void rtcSleepBlocks(uint32_t secs) {
  uint32_t remaining = secs;
  while (remaining > 0) {
    uint32_t block = remaining > 500 ? 500 : remaining;
    NRF_RTC2->TASKS_STOP = 1;
    NRF_RTC2->TASKS_CLEAR = 1;
    NRF_RTC2->PRESCALER = 0;  // 32768 Hz
    NRF_RTC2->CC[0] = block * 32768u;
    NRF_RTC2->EVENTS_COMPARE[0] = 0;
    NRF_RTC2->INTENSET = RTC_INTENSET_COMPARE0_Msk;
    gRtcWake = false;
    NVIC_EnableIRQ(RTC2_IRQn);
    NRF_RTC2->TASKS_START = 1;
    NRF_POWER->TASKS_LOWPWR = 1;
    while (!gRtcWake) {
      __WFE();
    }
    NRF_RTC2->TASKS_STOP = 1;
    remaining -= block;
  }
}
}

extern "C" void RTC2_IRQHandler(void) {
  if (NRF_RTC2->EVENTS_COMPARE[0]) {
    NRF_RTC2->EVENTS_COMPARE[0] = 0;
    gRtcWake = true;
  }
}

namespace {

// REFSEL codes for fractions n/16 (1..15): even n -> k/8 (code k-1),
// odd n -> 8 + (n-1)/2 (nRF52840 bitfields).
uint32_t refselForSixteenth(uint8_t n16) {
  if (n16 % 2 == 0) return (n16 / 2) - 1;
  return 8 + (n16 - 1) / 2;
}

// Arm the LPCOMP wake source (solar recovery). Returns false when the
// comparator never reports READY: the Product Specification bounds that event
// to microseconds, so it is practically unreachable, but the caller must never
// hang here (a solar node that stops before System OFF is dead until someone
// presses a button). One re-arm attempt is made before giving up.
bool armLpcompWake(uint16_t wakeMv) {
  // batteryWake = 2 * (n/16) * 3300 mV  ->  n = wakeMv / 412.5
  uint8_t n16 = (uint8_t)((wakeMv + 206) / 412);
  if (n16 < 1) n16 = 1;
  if (n16 > 15) n16 = 15;

  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    NRF_LPCOMP->ENABLE = 0;
    NRF_LPCOMP->INTENCLR = 0xFFFFFFFF;
    // Entrada analogica del comparador, segun la PLACA (Faketec: AIN7 = P0.31;
    // T-Echo: AIN2 = P0.04). El numero de AIN no se puede reutilizar entre placas.
    NRF_LPCOMP->PSEL = (BATTERY_LPCOMP_AIN == 2)   ? LPCOMP_PSEL_PSEL_AnalogInput2
                       : (BATTERY_LPCOMP_AIN == 7) ? LPCOMP_PSEL_PSEL_AnalogInput7
                                                   : LPCOMP_PSEL_PSEL_AnalogInput7;
    NRF_LPCOMP->REFSEL = (refselForSixteenth(n16) << LPCOMP_REFSEL_REFSEL_Pos);
    NRF_LPCOMP->ANADETECT =
        LPCOMP_ANADETECT_ANADETECT_Up << LPCOMP_ANADETECT_ANADETECT_Pos;
    NRF_LPCOMP->HYST = LPCOMP_HYST_HYST_Enabled << LPCOMP_HYST_HYST_Pos;
    NRF_LPCOMP->EVENTS_READY = 0;
    NRF_LPCOMP->EVENTS_DOWN = 0;
    NRF_LPCOMP->EVENTS_UP = 0;
    NRF_LPCOMP->EVENTS_CROSS = 0;
    NRF_LPCOMP->ENABLE = LPCOMP_ENABLE_ENABLE_Enabled;
    NRF_LPCOMP->TASKS_START = 1;
    // Bounded wait (1 ms) instead of the usual endless loop.
    uint32_t t0 = micros();
    while (NRF_LPCOMP->EVENTS_READY == 0 &&
           (uint32_t)(micros() - t0) < 1000u) {
    }
    if (NRF_LPCOMP->EVENTS_READY != 0) {
      NRF_LPCOMP->EVENTS_READY = 0;
      NRF_LPCOMP->EVENTS_UP = 0;
      delay(10);
      return true;
    }
    NRF_LPCOMP->TASKS_STOP = 1;
    NRF_LPCOMP->ENABLE = 0;
  }
  return false;
}

// Put every peripheral to sleep before System OFF / timed sleep. Order matters
// (mirrors NavaTastic cpuDeepSleep): GPS MOSFET off + UART released (not ending
// Serial1 causes wake problems on ProMicro), radio sleep over SPI before
// SPI.end(), OLED off while I2C is still alive, then buses/Serial and rail.
void quiesceForSleep() {
  // ★★ EL BLUETOOTH SE APAGA AQUI, Y ES LO PRIMERO (2026-09-17) ★★
  //   Aqui iba `bleShutdown()` y se quito el 2026-09-13, cuando el Bluetooth salio de la
  //   compilacion. Vuelve a ir, y va lo PRIMERO por dos motivos que importan:
  //     1) `bleShutdown()` deja de anunciar, echa al huesped y APAGA el SoftDevice. Con el
  //        SoftDevice arriba la radio y el LFCLK se quedan encendidos, y el consumo del sueno
  //        no seria el que suponen las cuentas de la bateria (System OFF se escribe a mano
  //        aqui abajo, y el SoftDevice no se entera de nada).
  //     2) `rtcSleepBlocks()` duerme con __WFE esperando una interrupcion del RTC2: con el
  //        stack de Bluetooth y sus tasks de FreeRTOS por medio, ese sueno no seria tranquilo.
  //   Se llama siempre, tambien cuando el Bluetooth nunca arranco: la funcion se sale sola.
  bleShutdown();
  gpsPower(false);
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, LOW);
  radioShutdown();
  displaySleep();
  Wire.end();
  SPI.end();
  if (Serial1) Serial1.end();  // required for a reliable wake on nRF52 (N-03)
  Serial.flush();
  if (Serial) Serial.end();
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(PIN_3V3_EN, LOW);
}

// Set by powerInit() when GPREGRET2 says the last shutdown was a protection
// sleep; the next boot then announces "NODO ONLINE".
bool gWokeFromSleep = false;

// Announce "back online" once per wake (respects mute). This runs before
// gpsInit(), so the GPS is not powered yet: in tracker modes the notice goes out
// as a status packet (never a guessed position). Mode 0 keeps the old gate, a
// fixed station needs a configured position to beacon.
void announceOnline(const DigiConfig &cfg) {
  if (!gWokeFromSleep) return;
  gWokeFromSleep = false;
  if (!radioReady() || cfg.txDisabled) return;
  // KISS (host-driven): the app commands, so the node does not send even this
  // notice (operator decision, see tncHostDriven() in main.cpp).
  if (cfg.tncProtocol == CFG_TNC_KISS) return;
  if (cfg.mode == 0 && !aprsCanBeacon(cfg)) return;
  aprsSendBannerBeacon(cfg, "NODO ONLINE");
}

}  // namespace

void powerInit() {
  // Power-fail comparator @2.2 V: last-resort hardware protection (N-03).
  NRF_POWER->POFCON = (POWER_POFCON_THRESHOLD_V22 << POWER_POFCON_THRESHOLD_Pos) |
                      (POWER_POFCON_POF_Enabled << POWER_POFCON_POF_Pos);
  analogReference(AR_INTERNAL_3_0);

  // GPREGRET2 survives System OFF and resets: 0xA5 means "slept for battery".
  gWokeFromSleep = (NRF_POWER->GPREGRET2 == 0xA5u);
  NRF_POWER->GPREGRET2 = 0;
}

bool powerUsbPresent() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

// ★★★ DE QUE PIN SE LEE LA BATERIA, POR PLACA (2026-09-15, arreglo de la auditoria G6) ★★★
//
// QUE ESTABA MAL: aqui se leia `PIN_BATTERY_ADC`, y en `src/pins_techo.h` ese nombre vale
// **31 = P0.31**. Pero en el T-Echo y el T-Echo Plus P0.31 es el **SCK DE LA PANTALLA DE
// TINTA ELECTRONICA** (`PIN_EPD_SCK 31`): la bateria de esas placas esta en **P0.04 = AIN2**
// (lo dicen `variants/techo/variant.h` y el propio `pins_techo.h`). O sea que quien decide el
// SUEÑO POR BATERIA BAJA (`powerBootCheck` / `powerLoop`) estaba midiendo el pin del reloj de
// la pantalla. Es el arreglo que mas importa de los tres: con la lectura mal, el nodo podia
// dormirse con la bateria llena (o no dormirse nunca), y el sueño de proteccion es lo unico
// que impide dañar la celda.
//
// POR QUE NO SE TOCA `pins_techo.h`: definir alli `PIN_BATTERY_ADC` con el pin bueno (4)
// dejaria SIN USAR el define del pin malo (31), y el compilador no avisa de eso; ademas ese
// fichero lo comparten los cuatro entornos. El pin se elige AQUI, que es el unico sitio que
// lee la bateria para el sueño, y queda escrito al lado de su uso. (El `-1` que llevaba el
// define viejo en el comentario de `sensors.cpp` era justamente el sintoma de este enredo.)
//
// T-Echo / T-Echo Plus: P0.04 = AIN2. P0.31 es el SCK del panel (no la bateria).
#define POWER_BATTERY_ADC_PIN 4

uint16_t powerReadMv() {
  uint32_t sum = 0;
  // Pin del ADC del T-Echo (ver la nota de arriba): P0.04 (AIN2). Se lee 8 veces y se promedia: el divisor es de 2x1M y con radiofrecuencia
  // cerca una sola lectura puede salir falsa.
  for (int i = 0; i < 8; i++) sum += analogRead(POWER_BATTERY_ADC_PIN);  // 10-bit, 0..3.0 V
  // raw/1023 * 3000 mV * 2.0 (divisor 1/2 en las tres placas)
  float raw = (float)(sum / 8) / 1023.0f * 3000.0f * BATTERY_DIVIDER;
  // Light IIR (alpha 0.5, NavaTastic style) so a TX sag cannot fake a low
  // reading; telemetry still reports the raw value from the sensors cache.
  static float ema = 0.0f;
  if (ema < 1.0f) ema = raw;
  else ema += (raw - ema) * 0.5f;
  return (uint16_t)(ema + 0.5f);
}

bool powerBootCheck(const DigiConfig &cfg) {
  if (powerUsbPresent()) {  // bench/USB: never sleep
    announceOnline(cfg);
    return true;
  }

  delay(500);  // rail settle before the first decision (NavaTastic)

  uint16_t cut = (uint16_t)cfg.sleepCutMv;
  uint16_t mv = powerReadMv();

  if (mv >= cut + 100) {  // healthy
    announceOnline(cfg);
    return true;
  }

  // Low (or near cut): consecutive readings, external-reset case. Exactly like
  // NavaTastic: the loop stops at the first healthy reading, so only 8 low
  // readings IN A ROW can send the node back to sleep.
  uint8_t lows = 0;
  for (uint8_t i = 0; i < kLowReadingsNeeded; i++) {
    mv = powerReadMv();
    if (mv >= cut) break;
    lows++;
    delay(200);
  }
  if (!tncActive()) {
    Serial.print(F("{\"power\":\"boot\",\"mv\":"));
    Serial.print(mv);
    Serial.print(F(",\"cut\":"));
    Serial.print(cut);
    Serial.print(F(",\"lows\":"));
    Serial.print(lows);
    Serial.println(F("}"));
  }
  if (lows >= kLowReadingsNeeded) {
    // Primero el aviso sonoro (melodia triste de bateria baja) y despues el popup: el nodo
    // se apaga y hay que enterarse aunque no se este mirando la pantalla (2026-09-15).
    displayLowBatTone();
    displayPopupWait("DURMIENDO: BAT BAJA RESERVA", kSleepNoticeMs);
    powerSleepNow(cfg);  // Reserva: back to sleep, LPCOMP will wake us
  }
  announceOnline(cfg);
  return true;  // Vivo: proceed, runtime monitor will re-check
}

void powerLoop(const DigiConfig &cfg) {
  // Called every 2 s from main, but the battery is only READ every 20 s
  // (NavaTastic cadence): 8 low readings then mean ~160 s of sustained low
  // battery, not 16 s. A single recovered reading resets the counter.
  static uint32_t lastReadMs = 0;

  if (powerUsbPresent()) {
    gLowCount = 0;
    lastReadMs = 0;  // on battery again: read right away
    return;
  }

  uint32_t now = millis();
  if (lastReadMs != 0 && (now - lastReadMs) < kLowReadIntervalMs) return;
  lastReadMs = now;

  uint16_t mv = powerReadMv();
  uint16_t cut = (uint16_t)cfg.sleepCutMv;

  if (mv >= cut) {
    gLowCount = 0;
    return;
  }

  gLowCount++;

  // NavaTastic-style progress popup ("lectura n de 8")
  char pop[32];
  snprintf(pop, sizeof(pop), "LOW BAT: %u OF %u - %umV", (unsigned)gLowCount,
           (unsigned)kLowReadingsNeeded, mv);
  displayPopup(pop);

  // Deep sleep only after 8 readings in a row. No fast path on purpose: a
  // single false low reading (RF pickup on the divider) must never sleep the
  // node on its own.
  if (gLowCount >= kLowReadingsNeeded) {
    if (!tncActive()) {
      Serial.print(F("{\"power\":\"sleep\",\"mv\":"));
      Serial.print(mv);
      Serial.print(F(",\"cut\":"));
      Serial.print(cut);
      Serial.print(F(",\"lows\":"));
      Serial.print(gLowCount);
      Serial.println(F("}"));
    }
    // Last notice, longer and with a countdown, so it is seen before sleeping.
    displayLowBatTone();   // aviso sonoro antes del popup (melodia triste de bateria baja)
    displayPopupWait("DURMIENDO: BATERIA BAJA", kSleepNoticeMs);
    powerSleepNow(cfg);
  }
}

uint8_t powerLowCount() { return gLowCount; }

void powerSleepNow(const DigiConfig &cfg) {
  flogLine("EVT sleep protection (battery)");
  // Final RF notice (respects mute) and the "slept" marker, so the next boot can
  // announce "NODO ONLINE" (GPREGRET2 survives System OFF). In tracker modes the
  // notice carries the live GPS position, or goes out as a status packet when
  // there is no fix; mode 0 keeps the configured-position gate. In KISS the host
  // app commands, so the node keeps quiet (see tncHostDriven() in main.cpp).
  if (cfg.tncProtocol != CFG_TNC_KISS && radioReady() && !cfg.txDisabled &&
      (cfg.mode != 0 || aprsCanBeacon(cfg))) {
    aprsSendBannerBeacon(cfg, "DURMIENDO HASTA EL SOL");
  }
  gWokeFromSleep = false;
  NRF_POWER->GPREGRET2 = 0xA5u;

  // 1) Quiet all peripherals (GPS off, radio sleep, buses closed, rail down)
  quiesceForSleep();

  // 2) Button wake (SENSE low on P1.00)
  variant_shutdown();

  // 3) Settle before arming LPCOMP (N-03: do NOT remove)
  delay(3000);

  // 4) LPCOMP: wake when the battery rises above sleepWakeMv (solar recovery)
  bool armed = armLpcompWake((uint16_t)cfg.sleepWakeMv);

  // Re-assert the LED off just before System OFF (NavaTastic: a latched LED
  // burns ~10 mA while "asleep")
  digitalWrite(LED_BUILTIN, LOW);

  if (!armed) {
    // No wake source would be armed: never enter System OFF here (that would
    // leave a solar node dead until someone presses a button). Sleep on the RTC
    // for a while and let the reboot retry the whole cycle.
    rtcSleepBlocks(kLpcompFallbackSecs);
    NVIC_SystemReset();
    while (1) {
      delay(1000);
    }
  }

  // 5) System OFF (direct register: the app never enables the SoftDevice)
  NRF_POWER->SYSTEMOFF = 1;
  while (1) {
    delay(1000);
  }
}

void powerSleepTimed(const DigiConfig &cfg, uint32_t secs) {
  (void)cfg;
  if (secs < 5) secs = 5;

  // 1) Quiet all peripherals (same as protection sleep)
  quiesceForSleep();

  // 2) RTC2 blocks, then reboot (NavaTastic storm pattern: System OFF cannot be
  //    woken by a timer).
  rtcSleepBlocks(secs);

  NVIC_SystemReset();
  while (1) {
    delay(1000);
  }
}
