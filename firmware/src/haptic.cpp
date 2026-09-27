// haptic.cpp — Motor haptico del T-Echo Plus. Ver haptic.h.
//
// POR QUE CON LA LIBRERIA DE ADAFRUIT: los registros del DRV2605 (modo, libreria de ondas,
// ranuras de secuencia, disparo) no se inventan. El proyecto ya usa librerias de Adafruit
// para los sensores (BMP280/BME280/BME680/AHTX0/INA219), asi que se sigue el mismo camino.
//
// VENTAJA IMPORTANTE FRENTE AL BUZZER: el DRV2605 ejecuta la secuencia EL SOLO. El pitido
// del zumbador bloquea el bucle (genera la onda a mano), pero aqui se le dice "efecto 47" y
// el chip lo reproduce mientras el firmware sigue atendiendo la radio y los botones.
#include "haptic.h"

#include "pins_techo.h"   // PIN_DRV_EN solo existe en el Plus (TEchoPlus)

#if defined(PIN_DRV_EN)

#include <Wire.h>
#include <Adafruit_DRV2605.h>

namespace {

Adafruit_DRV2605 gDrv;
bool gIni = false;   // ya se intento arrancar (no repetir el intento en cada aviso)
bool gOk  = false;   // el chip contesto

}  // namespace

bool hapticInit() {
  if (gIni) return gOk;
  gIni = true;

  // Enable del motor. El chip se alimenta del regulador de 3,3 V (P0.13), que ya esta en
  // alto desde initVariant(), asi que aqui solo hay que habilitar P0.08.
  pinMode(PIN_DRV_EN, OUTPUT);
  digitalWrite(PIN_DRV_EN, HIGH);
  delay(3);   // que el chip se estabilice antes de hablarle por I2C

  if (!gDrv.begin()) {
    gOk = false;
    return false;
  }
  gDrv.selectLibrary(1);               // libreria 1 = ERM (motor de esta placa)
  gDrv.setMode(DRV2605_MODE_INTTRIG);  // disparo interno: la secuencia la hace el chip
  gOk = true;
  return true;
}

bool hapticReady() { return gOk; }

// El motor y el zumbador van juntos (solo los lleva el Plus), asi que si el motor contesta
// esta placa es un Plus. Ver el porque en haptic.h. hapticInit() ya cachea el resultado,
// asi que esto no vuelve a hablar por I2C en cada pitido.
bool hapticEsPlus() { return hapticInit(); }

bool hapticEffect(uint8_t efecto) {
  if (!hapticInit()) return false;
  gDrv.setWaveform(0, efecto);   // ranura 0: el efecto pedido
  gDrv.setWaveform(1, 0);        // 0 = fin de la secuencia
  gDrv.go();
  return true;
}

bool hapticSequence(const uint8_t *efectos, uint8_t n) {
  if (!hapticInit()) return false;
  if (!efectos || n == 0) return false;
  if (n > 8) n = 8;              // la ROM tiene 8 ranuras de secuencia
  for (uint8_t i = 0; i < n; i++) gDrv.setWaveform(i, efectos[i]);
  if (n < 8) gDrv.setWaveform(n, 0);   // cerrar la secuencia
  gDrv.go();
  return true;
}

// Los efectos de cada aviso. El porque de cada uno esta en haptic.h.
static const uint8_t kEfectoAviso[] = {
  1,    // HAP_TOQUE    Strong Click 100%: un clic seco y corto (es el que mas se repite)
  7,    // HAP_ARRANQUE Soft Bump 100%: un golpe suave, "aqui estoy"
  10,   // HAP_GPS      Double Click 100%: dos golpes, "ya se donde estoy"
  12,   // HAP_MENSAJE  Triple Click 100%: tres golpes, "tienes un mensaje"
  47,   // HAP_ACUSE    Buzz 1 100%: un zumbido corto, distinto de todos los clics
  15,   // HAP_BUSCAR   750 ms Alert 100%: lo mas largo y llamativo, para encontrar el nodo
};

void hapticAviso(HapAviso a) {
  const int i = (int)a;
  if (i < 0 || i >= (int)(sizeof(kEfectoAviso) / sizeof(kEfectoAviso[0]))) return;
  hapticEffect(kEfectoAviso[i]);
}

void hapticTickGPS(bool fixAhora) {
  static bool ultimoFix = false;
  static bool yaAviso = false;          // el PRIMER aviso tras arrancar siempre suena
  static uint32_t ultimoAvisoMs = 0;
  constexpr uint32_t kMinEntreAvisosMs = 60000;   // 1 min: no repetir si el fix va y viene

  if (fixAhora && !ultimoFix &&
      (!yaAviso || (uint32_t)(millis() - ultimoAvisoMs) > kMinEntreAvisosMs)) {
    hapticAviso(HAP_GPS);
    yaAviso = true;
    ultimoAvisoMs = millis();
  }
  ultimoFix = fixAhora;
}

#else   // ---- placas SIN motor (Faketec, T-Echo normal): todo inerte ----

bool hapticInit() { return false; }
bool hapticReady() { return false; }
bool hapticEsPlus() { return false; }
bool hapticEffect(uint8_t efecto) { (void)efecto; return false; }
bool hapticSequence(const uint8_t *efectos, uint8_t n) { (void)efectos; (void)n; return false; }
void hapticAviso(HapAviso a) { (void)a; }
void hapticTickGPS(bool fixAhora) { (void)fixAhora; }

#endif
