/* banco.cpp — BANCO DE PRUEBAS DEL BOTON (T-Echo Project Butter).
 *
 * QUE MIDE, SIN PLACA: reproduce milisegundo a milisegundo lo que hace el firmware
 * de verdad cuando alguien toca el boton, y dice QUE GESTO SALE Y CUANDO. Sirve para
 * comparar el comportamiento de ANTES y de DESPUES con numeros, no con impresiones.
 *
 * COMO ESTA HECHO (y por que se puede creer):
 *   - Lo que se compila es el `button.cpp` DE VERDAD (o su version anterior, sacada
 *     de git: `button_antes.cpp`), no una copia reescrita para la prueba.
 *   - El reloj y los pines son de mentira (`Arduino.h` de este mismo directorio):
 *     `millis()` avanza cuando el banco dice, y `digitalRead()` devuelve el nivel que
 *     dice el guion de la prueba.
 *   - `attachInterrupt()` apunta la funcion que le pasa el firmware y el banco la
 *     LLAMA en cada flanco, que es exactamente lo que hace el GPIOTE del chip en la
 *     placa. Si el firmware no instala interrupcion (la version anterior), el banco no
 *     llama a nadie: eso es tambien lo que pasaba de verdad.
 *
 * QUE MODELO DEL FIRMWARE HAY ALREDEDOR (esto son las piezas que NO son del boton, y
 * son los unicos numeros "de fuera" que hay en la prueba):
 *   - Bucle libre: 1 ms en las dos versiones. Es A PROPOSITO: la version anterior
 *     llamaba a handleButton() ~72 veces seguidas cada vuelta (6 llamadas a
 *     drainButton() de 12 ms, a 1 ms por vuelta), o sea que MIENTRAS EL BUCLE CORRIA
 *     miraba el pin casi tan a menudo como la nueva. Darle 1 ms a las dos quita
 *     cualquier duda de que la comparacion este amanada: la diferencia que se mide
 *     sale del rato BLOQUEADO, que es donde esta el problema de verdad.
 *   - Ratos BLOQUEADOS POR LA PANTALLA (un refresco de tinta: 0,35-2 s): dentro de
 *     ellos el bucle no da ni una vuelta. La version NUEVA bombea el boton cada 2 ms
 *     (es lo que hace el driver: `epdEsperaPintado()` -> ePDBombea()), la anterior no
 *     miraba nada. Al salir, el bucle atiende lo que tenga pendiente (en el firmware
 *     nuevo, `handleButton()` justo despues de `displayRefresh()`).
 *   - Ratos de TRANSMISION de radio (cientos de ms, `radioSendFrame`): bloquean el
 *     bucle y NO bombean nada (la radio no llama al gancho de la pantalla). La ventana
 *     se apunta al TERMINAR, igual que hace radio.cpp con buttonNoteRadioTx().
 *
 * COMO COMPILAR Y EJECUTAR: `ejecuta_banco.ps1` en esta misma carpeta (necesita un
 * compilador de C++ del ordenador: `zig c++`, `g++` o `clang++`).
 *
 * License: GPL-3.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Arduino.h"
#include "button.h"

// ===========================================================================
//  MODELO DEL FIRMWARE ALREDEDOR (parametros, no logica del boton)
// ===========================================================================
static const uint32_t BUCLE_LIBRE_MS = 1;   // las dos versiones miran el pin a 1 ms
static const uint32_t PUMP_MS = 2;          // cada cuanto bombea el driver de la tinta

// ===========================================================================
//  RELOJ Y PINES DE MENTIRA
// ===========================================================================
static uint32_t gMs = 0;
uint32_t millis(void) { return gMs; }

static bool gNivelFisico = true;   // true = SUELTO (hay pull-up; pulsar lo baja)
static bool gNivelToque = true;

int digitalRead(uint32_t pin) {
  if (pin == BUTTON_PIN) return gNivelFisico ? HIGH : LOW;
  if (pin == PIN_BTN_TOUCH) return gNivelToque ? HIGH : LOW;
  return HIGH;
}
void pinMode(uint32_t, int) {}

static void (*gIsrFisico)(void) = nullptr;
static void (*gIsrToque)(void) = nullptr;
int attachInterrupt(uint32_t pin, void (*fn)(void), uint32_t) {
  if (pin == BUTTON_PIN) gIsrFisico = fn;
  else if (pin == PIN_BTN_TOUCH) gIsrToque = fn;
  return 1;
}
uint32_t digitalPinToInterrupt(uint32_t pin) { return pin; }

// ===========================================================================
//  GUION DE LA PRUEBA
// ===========================================================================
struct Cambio { uint32_t ms; bool toque; bool nivel; };   // nivel true = pulsado/tocado
struct Ventana { uint32_t ini, fin; };   // rato bloqueado: pantalla (bombea) o radio (no)

struct Escenario {
  const char *nombre;
  const char *quePasa;
  const Cambio *cambios; int nCambios;
  const Ventana *pantalla; int nPantalla;   // refresco de tinta: bloquea y BOMBEA
  const Ventana *radio; int nRadio;         // transmision: bloquea y NO bombea
};

// --- 1. toque corto dentro de un refresco parcial (1,5 s) -------------------
static const Cambio c1[] = { {1200, false, true}, {1280, false, false} };
static const Ventana p1[] = { {1000, 2500} };

// --- 2. toque corto con el nodo en reposo ----------------------------------
static const Cambio c2[] = { {1000, false, true}, {1080, false, false} };

// --- 3. doble toque comodo (2o empieza 370 ms despues de soltar) -----------
static const Cambio c3[] = {
  {1000, false, true}, {1080, false, false},
  {1450, false, true}, {1530, false, false},
};

// --- 4. doble toque lento (2o empieza 700 ms despues de soltar) ------------
static const Cambio c4[] = {
  {1000, false, true}, {1080, false, false},
  {1780, false, true}, {1950, false, false},
};

// --- 5. pulsacion LARGA dentro de un refresco ------------------------------
static const Cambio c5[] = { {1200, false, true}, {2200, false, false} };
static const Ventana p5[] = { {1000, 2500} };

// --- 6. TACTIL: la pastilla se dispara con el RF de nuestra transmision ----
// El RF engancha la pastilla y la deja en BAJO mientras dura la emision, y un poco mas
// alla (es un desplazamiento de nivel, no un chispazo): la pastilla se va a bajo en el
// ms 3000 y no vuelve hasta el 3550. La transmision es de 2950 a 3300.
static const Cambio c6[] = { {3000, true, true}, {3550, true, false} };
static const Ventana r6[] = { {2950, 3300} };

// --- 7. TACTIL: ruido (4 chispas) y luego un toque de verdad ---------------
static const Cambio c7[] = {
  {1000, true, true}, {1003, true, false},
  {1008, true, true}, {1012, true, false},
  {1017, true, true}, {1021, true, false},
  {1026, true, true}, {1030, true, false},
  {1100, true, true}, {1160, true, false},
};

// --- 8. boton FISICO durante una transmision de 700 ms ---------------------
static const Cambio c8[] = { {3000, false, true}, {3080, false, false} };
static const Ventana r8[] = { {2950, 3650} };

static const Escenario kEscenarios[] = {
  {"1. Toque corto CAIDO DENTRO de un refresco de tinta (1,5 s)",
   "el caso que se perdia: nadie miraba el pin mientras el panel pintaba",
   c1, 2, p1, 1, nullptr, 0},
  {"2. Toque corto con el nodo en reposo (sin refresco)",
   "latencia pura de la logica: cuando se decide el gesto",
   c2, 2, nullptr, 0, nullptr, 0},
  {"3. Doble toque comodo (2o empieza 370 ms despues de soltar)",
   "el gesto que al operador le cuesta",
   c3, 4, nullptr, 0, nullptr, 0},
  {"4. Doble toque lento (2o empieza 700 ms despues de soltar)",
   "aqui la tabla de plazos se contradecia",
   c4, 4, nullptr, 0, nullptr, 0},
  {"5. Pulsacion LARGA caida dentro de un refresco de tinta (1,5 s)",
   "abrir el menu con el panel pintando",
   c5, 2, p5, 1, nullptr, 0},
  {"6. TACTIL: la pastilla se dispara con el RF de NUESTRA transmision",
   "toque FANTASMA: nadie ha tocado (fenomeno conocido de esta placa)",
   c6, 2, nullptr, 0, r6, 1},
  {"7. TACTIL: ruido (4 chispas) y luego un toque de verdad",
   "un roce no puede valer por cuatro acciones",
   c7, 10, nullptr, 0, nullptr, 0},
  {"8. Boton FISICO durante una transmision de 700 ms",
   "el fisico NO se filtra nunca: si el operador pulsa, es el",
   c8, 2, nullptr, 0, r8, 1},
};
static const int kNumEscenarios = (int)(sizeof(kEscenarios) / sizeof(kEscenarios[0]));

// ===========================================================================
//  REGISTRO DE LO QUE PASA
// ===========================================================================
static const char *nombreEvento(ButtonEvent ev) {
  switch (ev) {
    case BTN_SHORT: return "CORTO";
    case BTN_LONG: return "LARGO";
    case BTN_DOUBLE: return "DOBLE";
    default: return "?";
  }
}

struct Accion { uint32_t ms; const char *que; };
static Accion gAcciones[64];
static int gNAcciones = 0;
static void apunta(uint32_t ms, const char *que) {
  if (gNAcciones < 64) { gAcciones[gNAcciones].ms = ms; gAcciones[gNAcciones].que = que; gNAcciones++; }
}

static uint32_t gFeedbackMs = 0;
#ifdef BANCO_NUEVO
static void feedback() { gFeedbackMs = millis(); }
#endif

// ---------------------------------------------------------------------------
//  LA LOGICA DEL TACTIL DE LA VERSION ANTERIOR: eran 15 lineas dentro de
//  main.cpp (handleButton). Se reproduce AQUI para poder comparar.
// ---------------------------------------------------------------------------
#ifndef BANCO_NUEVO
static bool viejoToquePoll() {
  static bool ini = false;
  if (!ini) { pinMode(PIN_BTN_TOUCH, INPUT_PULLUP); ini = true; }
  static bool ultimo = false;
  const bool cap = (digitalRead(PIN_BTN_TOUCH) == LOW);   // activo en BAJO, sin filtro
  bool hubo = false;
  if (cap && !ultimo) hubo = true;                        // cualquier flanco = accion
  ultimo = cap;
  return hubo;
}
#endif

// ===========================================================================
//  SIMULACION DE UN ESCENARIO
// ===========================================================================
static void simula(const Escenario &e) {
  gMs = 0;
  gNivelFisico = true;
  gNivelToque = true;
  gIsrFisico = nullptr;
  gIsrToque = nullptr;
  gNAcciones = 0;
  gFeedbackMs = 0;

  buttonInit();
#ifdef BANCO_NUEVO
  buttonSetFeedback(feedback);
#endif

  uint32_t proxTick = 0, proxPump = 0;
  const uint32_t fin = 6000;

  for (gMs = 0; gMs <= fin; gMs++) {
    // 1) cambios de nivel del guion (y la interrupcion, si el firmware la instalo)
    for (int i = 0; i < e.nCambios; i++) {
      if (e.cambios[i].ms != gMs) continue;
      if (e.cambios[i].toque) {
        gNivelToque = !e.cambios[i].nivel;
        if (gIsrToque) gIsrToque();
      } else {
        gNivelFisico = !e.cambios[i].nivel;
        if (gIsrFisico) gIsrFisico();
      }
    }
    // 2) ¿hay transmision de radio? Bloquea el bucle y NO bombea. Al terminar se
    //    apunta la ventana, igual que hace radio.cpp.
    bool radio = false;
    for (int i = 0; i < e.nRadio; i++) {
      if (gMs >= e.radio[i].ini && gMs < e.radio[i].fin) radio = true;
      if (gMs == e.radio[i].fin) {
#ifdef BANCO_NUEVO
        buttonNoteRadioTx(e.radio[i].ini, e.radio[i].fin);
#endif
      }
    }
    // 3) ¿hay refresco de pantalla? Bloquea el bucle, pero el driver BOMBEA el boton.
    bool pantalla = false;
    for (int i = 0; i < e.nPantalla; i++) {
      if (gMs >= e.pantalla[i].ini && gMs < e.pantalla[i].fin) pantalla = true;
    }
    if (radio) continue;
    if (pantalla) {
#ifdef BANCO_NUEVO
      if (gMs >= proxPump) { buttonPump(); proxPump = gMs + PUMP_MS; }
#endif
      continue;
    }
    // 4) vuelta del bucle
    if (gMs >= proxTick) {
      proxTick = gMs + BUCLE_LIBRE_MS;
#ifdef BANCO_NUEVO
      ButtonEvent ev;
      while ((ev = buttonPoll()) != BTN_NONE) apunta(gMs, nombreEvento(ev));
      if (buttonTouchPoll()) apunta(gMs, "TACTIL");
#else
      const ButtonEvent ev = buttonPoll();
      if (ev != BTN_NONE) apunta(gMs, nombreEvento(ev));
      if (viejoToquePoll()) apunta(gMs, "TACTIL");
#endif
    }
  }
}

// ===========================================================================
int main(void) {
#ifdef BANCO_NUEVO
  printf("========== BANCO DEL BOTON - CODIGO NUEVO (T-Echo Project Butter) ==========\n");
  printf("tabla de plazos (de src/button.h): antirrebote=%d  largo=%d  ventana doble=%d ms\n",
         BUTTON_DEBOUNCE_MS, BUTTON_LONG_MS, BUTTON_CLICK_WINDOW_MS);
#else
  printf("========== BANCO DEL BOTON - CODIGO ANTERIOR (el que estaba en git) ==========\n");
  printf("tabla de plazos (del fichero anterior): antirrebote=25  largo=600  ventana doble=800 ms\n");
  printf("(la ventana se medIa de SUELTA a SUELTA, y el corto no miraba si el boton estaba pulsado)\n");
#endif
  printf("lectura del pin: ");
#ifdef BANCO_NUEVO
  printf("interrupcion (GPIOTE) + bombeo cada %u ms dentro del refresco\n", (unsigned)PUMP_MS);
#else
  printf("a 1 ms MIENTRAS el bucle corre; CERO mientras el panel pinta o la radio emite\n");
#endif
  printf("\n");
  for (int i = 0; i < kNumEscenarios; i++) {
    const Escenario &e = kEscenarios[i];
    simula(e);
    printf("%s\n   (%s)\n", e.nombre, e.quePasa);
#ifdef BANCO_NUEVO
    if (gFeedbackMs) printf("   aviso inmediato al pulsar (luz + pitido): t=%u ms\n", (unsigned)gFeedbackMs);
#endif
    if (gNAcciones == 0) {
      printf("   >>> NINGUNA ACCION: el toque se ha PERDIDO\n");
    } else {
      for (int a = 0; a < gNAcciones; a++)
        printf("   accion: %-5s  t=%u ms\n", gAcciones[a].que, (unsigned)gAcciones[a].ms);
      if (gNAcciones > 1)
        printf("   >>> %d acciones para un solo gesto\n", gNAcciones);
    }
    printf("\n");
  }
  return 0;
}
