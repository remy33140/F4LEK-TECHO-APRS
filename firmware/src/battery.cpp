// battery.cpp — ver battery.h (por que hay una sola lectura y que corrige).
// License: GPL-3.0

#include "battery.h"

#include <Arduino.h>

#include "power.h"
#include "radio.h"

namespace {

const DigiConfig *gCfg = nullptr;

// T-Echo / T-Echo Plus: la bateria esta en P0.04 = AIN2, detras de un divisor 1/2 (2 x 1 MOhm).
// (P0.31 es el reloj de la pantalla: ver la historia en sensors.cpp y power.cpp.)
constexpr int kPin = 4;
constexpr float kDivider = 2.0f;
constexpr int kSamples = 16;
constexpr uint32_t kTxQuietMs = 3000;   // tras un TX la tension tarda en recuperarse
constexpr uint32_t kEveryMs = 2000;     // la pantalla pregunta a menudo: se lee como mucho cada 2 s

float gEma = 0.0f;          // mV suavizados (0 = todavia ninguna lectura)
uint32_t gLastReadMs = 0;
int gPctShown = -1;         // porcentaje enseñado (histeresis)

// Una lectura cruda en mV, ya con el factor de correccion. Deja el ADC como lo encontro el
// resto del firmware (10 bits, 3 us): nadie mas tiene por que enterarse de este ajuste.
float readMv() {
  analogReference(AR_INTERNAL_3_0);   // 0..3,0 V a la entrada
  analogReadResolution(12);
  analogSampleTime(40);               // 40 us: fuente de 500 kOhm (divisor 2 x 1 MOhm)
  uint32_t sum = 0;
  for (int i = 0; i < kSamples; i++) sum += analogRead(kPin);
  analogSampleTime(3);
  analogReadResolution(10);
  float mv = (float)sum / kSamples / 4095.0f * 3000.0f * kDivider;
  const float cal = gCfg ? gCfg->batteryCal : 1.0f;
  return mv * ((cal >= 0.8f && cal <= 1.2f) ? cal : 1.0f);
}

// Curva de descarga de una LiPo (tension en reposo con poca carga), del 100 % al 0 % de 10 en
// 10. Es la tabla OCV que usa Meshtastic para estas mismas celdas.
constexpr uint16_t kCurve[11] = {4190, 4050, 3990, 3890, 3800, 3720, 3630, 3580, 3530, 3440, 3100};

float curvePct(float mv) {
  if (mv >= kCurve[0]) return 100.0f;
  if (mv <= kCurve[10]) return 0.0f;
  for (int i = 0; i < 10; i++) {
    if (mv >= kCurve[i + 1]) {
      const float f = (mv - kCurve[i + 1]) / (float)(kCurve[i] - kCurve[i + 1]);
      return 100.0f - 10.0f * (i + 1) + 10.0f * f;
    }
  }
  return 0.0f;
}

}  // namespace

void batteryBindConfig(const DigiConfig *cfg) { gCfg = cfg; }

bool batteryOnUsb() { return powerUsbPresent(); }

uint16_t batteryReadNowMv() {
  const float mv = readMv();
  return (mv >= 2400.0f && mv <= 4500.0f) ? (uint16_t)(mv + 0.5f) : 0;
}

uint16_t batteryMv() {
  const uint32_t now = millis();
  const uint32_t lastTx = radioLastTxMs();
  const bool txSag = lastTx != 0 && (uint32_t)(now - lastTx) < kTxQuietMs;
  const bool due = gEma < 1.0f || (uint32_t)(now - gLastReadMs) >= kEveryMs;
  if (due && (!txSag || gEma < 1.0f)) {
    gLastReadMs = now;
    const float mv = readMv();
    if (mv >= 2400.0f && mv <= 4500.0f) {
      // Suavizado ligero: un pico de RF en el divisor no puede fingir una bateria baja.
      gEma = (gEma < 1.0f) ? mv : gEma + (mv - gEma) * 0.3f;
    }
  }
  return (uint16_t)(gEma + 0.5f);
}

int batteryPercent() {
  const uint16_t mv = batteryMv();
  if (mv == 0) return -1;
  // El 0 % es el corte (el nodo se duerme ahi); el resto de la curva se reescala encima.
  const float cut = (gCfg && gCfg->sleepCutMv > 0) ? (float)gCfg->sleepCutMv : 3200.0f;
  const float p0 = curvePct(cut);
  float p = mv <= cut ? 0.0f : (curvePct(mv) - p0) / (100.0f - p0) * 100.0f;
  int pct = (int)(p + 0.5f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  // Histeresis: la tinta solo se repinta si el numero cambia de verdad.
  if (gPctShown < 0 || pct == 0 || pct == 100 || abs(pct - gPctShown) >= 2) gPctShown = pct;
  return gPctShown;
}
