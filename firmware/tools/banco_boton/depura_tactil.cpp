/* depura_tactil.cpp — sonda temporal para ver POR DENTRO que hace el tactil.
 * No forma parte del firmware ni del banco: es una lupa para entender un caso raro.
 * Imprime, milisegundo a milisegundo, el nivel del pin y lo que devuelve buttonTouchPoll().
 */
#include <stdint.h>
#include <stdio.h>

#include "Arduino.h"
#include "button.h"

static uint32_t gMs = 0;
uint32_t millis(void) { return gMs; }

static bool gNivelToque = true;
int digitalRead(uint32_t pin) {
  if (pin == PIN_BTN_TOUCH) return gNivelToque ? HIGH : LOW;
  return HIGH;
}
void pinMode(uint32_t, int) {}
static void (*gIsrToque)(void) = nullptr;
int attachInterrupt(uint32_t pin, void (*fn)(void), uint32_t) {
  if (pin == PIN_BTN_TOUCH) gIsrToque = fn;
  return 1;
}
uint32_t digitalPinToInterrupt(uint32_t pin) { return pin; }

struct C { uint32_t ms; bool tocado; };
static const C kCambios[] = {
  {1000, true}, {1003, false}, {1008, true}, {1012, false},
  {1017, true}, {1021, false}, {1026, true}, {1030, false},
  {1100, true}, {1160, false},
};
static const int kN = (int)(sizeof(kCambios) / sizeof(kCambios[0]));

int main(void) {
  buttonInit();
  for (gMs = 0; gMs <= 1400; gMs++) {
    for (int i = 0; i < kN; i++) {
      if (kCambios[i].ms != gMs) continue;
      gNivelToque = !kCambios[i].tocado;
      printf("t=%4u  pin=%s (flanco)  isr=%s\n", (unsigned)gMs,
             gNivelToque ? "ALTO" : "BAJO", gIsrToque ? "instalada" : "NO INSTALADA");
      if (gIsrToque) gIsrToque();
    }
    if (buttonTouchPoll()) printf("t=%4u  >>> TOQUE ACEPTADO\n", (unsigned)gMs);
  }
  printf("flancos perdidos: %u\n", (unsigned)buttonLostEdges());
  return 0;
}
