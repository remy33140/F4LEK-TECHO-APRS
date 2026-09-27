// button.cpp — Faketec button state machine (short / long / double only).
//
// Design (operator request): the long press fires as soon as it is held for
// kLongMs, WITHOUT waiting for the release, so there is nothing to guess. The
// short press is resolved after a kClickWindowMs window, which is what lets the
// firmware tell "one tap" from "two taps" reliably (same idea as a mouse double
// click). License: GPL-3.0

#include "button.h"

namespace {

// Rebote del contacto: bajado de 40 a 25 ms. Un toque RAPIDO dura 60-90 ms, asi que
// con 40 ms de filtro el flanco se retrasaba y un toque corto podia llegar a
// perderse justo en el doble toque rapido (que es el que se quiere detectar).
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kLongMs = 600;        // long fires WHILE held at this point
// Bajado de 800 a 600 ms a peticion del operador (2026-09-13): el toque largo se
// usa para CONFIRMAR en el menu (elegir el modo, guardar) y 800 ms se hacia
// largo. Sigue estando muy por encima del rebote (40 ms) y del doble toque
// (dos toques dentro de 400 ms), asi que no se confunde con nada.
// Margen entre los dos toques de un DOBLE TOQUE. Subido de 400 a 800 ms a peticion
// del operador (2026-09-13): "el doble toque siento que debe ser muy mecanico y
// sincronizado, cuesta hacerlo; es mas facil un doble toque rapido que el que
// programamos". El margen se mide de SUELTA a PULSACION, y 400 ms se queda corto
// para un boton de goma pulsado con el dedo: si el segundo toque llegaba tarde, el
// nodo lo partia en dos toques cortos (dos filas de menu, o dos acciones).
// Un raton usa 500 ms; en un boton fisico 800 ms es lo comodo. El toque corto
// simple tarda un poco mas en resolverse (hasta 800 ms), pero a cambio el doble
// toque sale a la primera.
constexpr uint32_t kClickWindowMs = 800; // max gap between the two taps

bool gRaw = false;         // debounced state (true = pressed)
bool gLastSample = false;  // last raw sample
uint32_t gLastChangeMs = 0;
uint32_t gPressStartMs = 0;
bool gLongFired = false;   // a long press was already reported for this hold

uint8_t gTaps = 0;         // consecutive short taps waiting to be resolved
uint32_t gLastTapMs = 0;

}  // namespace

void buttonInit() { pinMode(BUTTON_PIN, INPUT_PULLUP); }

ButtonEvent buttonPoll() {
  const bool sample = (digitalRead(BUTTON_PIN) == LOW);
  const uint32_t now = millis();

  if (sample != gLastSample && now - gLastChangeMs > kDebounceMs) {
    gLastChangeMs = now;
    gLastSample = sample;
    if (sample) {  // pressed
      gRaw = true;
      gPressStartMs = now;
      gLongFired = false;
    } else {  // released
      gRaw = false;
      if (gLongFired) {
        gLongFired = false;  // the long press was already reported
        gTaps = 0;
        return BTN_NONE;
      }
      // Too slow to be a long press but held that long: ignore it.
      if (now - gPressStartMs >= kLongMs) {
        gTaps = 0;
        return BTN_NONE;
      }
      // Short tap: wait for the window before deciding short vs double.
      if (gTaps > 0 && now - gLastTapMs <= kClickWindowMs) {
        gTaps = 0;
        return BTN_DOUBLE;
      }
      gTaps = 1;
      gLastTapMs = now;
    }
  }

  // Long press: fires while still held (nothing to guess, no release needed).
  if (gRaw && !gLongFired && now - gPressStartMs >= kLongMs) {
    gLongFired = true;
    gTaps = 0;
    return BTN_LONG;
  }

  // Resolve a pending single tap once the double-click window closes.
  if (gTaps == 1 && now - gLastTapMs > kClickWindowMs) {
    gTaps = 0;
    return BTN_SHORT;
  }

  return BTN_NONE;
}
