// button.cpp — Boton FISICO (P1.10) + TACTIL CAPACITIVO (P0.11) de la T-Echo.
//
// ★★★ T-ECHO PROJECT BUTTER — LECTURA DE FONDO (2026-09-15) ★★★
//
// EL DIAGNOSTICO, MEDIDO SOBRE EL CODIGO ANTERIOR:
//
//  1) LOS TOQUES SE PERDIAN PORQUE NADIE MIRABA EL PIN. La version anterior leia el
//     nivel con `digitalRead` una vez por vuelta del bucle, y el bucle se pasa 1,5-3 s
//     pintando la tinta (el driver espera al panel con delay() dentro). Un toque que
//     empieza y acaba en ese rato no deja NINGUNA huella: al volver el bucle el pin ya
//     esta suelto, o sea el mismo nivel que antes de pintar. La funcion drainButton()
//     de main.cpp decia en su comentario que "los flancos que llegaron durante el
//     atasco se procesan en cuanto el bucle vuelve", pero eso es FALSO leyendo NIVELES:
//     solo se recupera si el dedo sigue puesto al volver. Un toque de 80 ms dentro de
//     un refresco de 1500 ms era invisible. De ahi "toques reales que nunca se
//     obedecen".
//
//  2) LA VENTANA DEL DOBLE TOQUE SE MEDIA MAL. El comentario decia "el margen se mide
//     de SUELTA a PULSACION", pero el codigo comparaba la SUELTA del primer toque con
//     la SUELTA del segundo: el operador tenia que empezar Y ACABAR el segundo toque
//     dentro de los 800 ms (unos 710 ms reales para empezar). Eso es justo el "es muy
//     mecanico, cuesta hacerlo" que reporto.
//
//  3) EL TOQUE CORTO SALIA A MITAD DE LA SEGUNDA PULSACION. La resolucion del corto no
//     miraba si el boton estaba pulsado: con un doble toque lento (segundo toque a los
//     750 ms) el corto saltaba a los 800 ms CON EL DEDO YA PUESTO, cambiando de
//     pantalla, y al soltar el segundo toque empezaba otra cuenta: DOS acciones donde
//     el operador habia hecho un gesto. Eso es un toque fantasma de libro.
//
//  4) EL TACTIL NO TENIA NINGUN FILTRO. Solo deteccion de flanco contra un `gCapLast`:
//     cualquier ruido en la pastilla (y el propio RF de la emisora, que es un fenomeno
//     CONOCIDO en esta placa y esta documentado por el autor del firmware de
//     referencia) se convierte en una accion. Con la pastilla flotando y una
//     resistencia de subida debil, un solo roce puede dar varios flancos = varias
//     acciones (varias diapositivas de golpe, pitido y vibracion repetidos).
//
// LO QUE SE HACE AHORA (y por que):
//
//  · Los flancos los captura una INTERRUPCION (GPIOTE) y se guardan con su hora en un
//    buzon. Es exactamente la arquitectura del firmware de referencia de esta placa
//    (cfr34k/t-echo-lora-aprs: `app_button` de Nordic, que arranca un muestreo por
//    temporizador de interrupcion a 25 ms en cuanto el GPIOTE ve el primer flanco).
//    Con eso, un toque durante el repintado NO se pierde: se resuelve con sus tiempos.
//  · El antirrebote es por ESTABILIDAD (el nivel tiene que aguantar kDebounceMs), no un
//    simple "no aceptes cambios antes de 25 ms". Es lo mismo que hacen Nordic
//    (2 muestras separadas 25 ms) y no se come un toque rapido de 60-90 ms.
//  · La ventana del doble toque se mide de SUELTA a PULSACION, que es lo que decia el
//    comentario, y el doble se decide EN LA PULSACION (no hay que esperar a soltar).
//  · El corto NO se resuelve nunca con el boton pulsado.
//
// License: GPL-3.0

#include "button.h"

#include "pins_board.h"   // PIN_BTN_TOUCH solo existe en el T-Echo (pins_techo.h)

namespace {

// ===========================================================================
//  LA TABLA DE TIEMPOS, EN UN SOLO SITIO Y SIN CONTRADICCIONES
// ===========================================================================
//
//  gesto           plazo                                              valor
//  ------------------------------------------------------------------------------
//  antirrebote     el nivel tiene que aguantar esto para contar        25 ms
//  LARGO           salta AL LLEGAR, mientras se mantiene              600 ms
//  ventana doble   de SOLTAR el 1er toque a PULSAR el 2º              600 ms
//  corto           se resuelve al vencer esa misma ventana            600 ms
//
//  ★ POR QUE LOS DOS ULTIMOS SON EL MISMO NUMERO (2026-09-15): con un solo plazo no
//    hay forma de que la tabla se contradiga. Antes eran dos: el corto tardaba hasta
//    800 ms en resolverse Y el doble exigia caber en esos mismos 800 ms, medidos de
//    suelta a suelta (ver el diagnostico de arriba). Ahora el operador tiene 600 ms
//    para EMPEZAR el segundo toque (antes ~710 ms para empezarlo Y acabarlo) y el
//    corto sale 200 ms antes que antes (600 en vez de 800). Es el mismo numero que
//    usa un raton (500 ms) con el margen de un boton de goma.
constexpr uint32_t kDebounceMs = 25;      // el nivel debe aguantar esto para contar
constexpr uint32_t kLongMs = 600;         // largo: salta mientras se mantiene
constexpr uint32_t kClickWindowMs = 600;  // suelta del 1º -> pulsacion del 2º

// ---------------------------------------------------------------------------
//  Buzon de flancos. Lo escribe la ISR, lo vacia el bucle.
//  16 huecos dan de sobra: en un rebote electrico caben todos los flancos y, si
//  alguien aporrea el boton durante un refresco de 3 s, tampoco se llena.
// ---------------------------------------------------------------------------
constexpr uint8_t kBuzonMax = 16;

struct Buzon {
  volatile uint32_t ms[kBuzonMax];
  volatile bool nivel[kBuzonMax];   // true = pulsado
  volatile uint8_t cabeza;          // escribe la ISR
  volatile uint8_t cola;            // lee el bucle
  volatile bool ultimo;             // ultimo nivel encolado (para no repetir)
  volatile uint32_t perdidos;       // flancos que no cupieron (diagnostico)
};

Buzon gFisico;   // boton de placa P1.10
Buzon gToque;    // pastilla capacitiva P0.11

inline void buzonMete(Buzon &b, bool nivel, uint32_t ms) {
  if (nivel == b.ultimo) return;                 // sin cambio: nada que guardar
  const uint8_t sig = (uint8_t)((b.cabeza + 1) % kBuzonMax);
  if (sig == b.cola) { b.perdidos++; return; }   // lleno (no deberia pasar)
  b.ms[b.cabeza] = ms;
  b.nivel[b.cabeza] = nivel;
  b.ultimo = nivel;
  b.cabeza = sig;
}

inline bool buzonSaca(Buzon &b, bool &nivel, uint32_t &ms) {
  if (b.cola == b.cabeza) return false;
  ms = b.ms[b.cola];
  nivel = b.nivel[b.cola];
  b.cola = (uint8_t)((b.cola + 1) % kBuzonMax);
  return true;
}

// ===========================================================================
//  NIVELES ACTIVOS. OJO CON ESTO, QUE ESTA MEDIDO Y NO ES LO QUE DICE EL PAPEL:
//  el `pins_techo.h` dice que la pastilla capacitiva es "activo a nivel alto", pero
//  en la placa, con la resistencia interna de subida puesta, se comporta ACTIVA EN
//  BAJO (es lo que decia el comentario de main.cpp, comprobado por el operador). Se
//  deja en BAJO, que es lo que funciona, y se pone aqui arriba y con nombre para que
//  cambiarlo sea una linea si alguna unidad sale al reves.
// ===========================================================================
constexpr int kBtnActivo = LOW;
#if defined(PIN_BTN_TOUCH)
constexpr int kToqueActivo = LOW;
constexpr uint32_t kToqueEstableMs = 40;   // el toque debe aguantar esto
constexpr uint32_t kToqueBloqueoMs = 250;  // ...y luego hay bloqueo: 1 toque = 1 accion
constexpr uint32_t kToqueTardeMs = 250;    // confirmado mas tarde que esto: se descarta
#endif

// ---- maquina de gestos del fisico (todo con marcas de tiempo) ----
bool gNivel = false;        // nivel CONFIRMADO (tras el antirrebote)
bool gPend = false;         // hay un cambio crudo esperando a aguantar el antirrebote
bool gPendNivel = false;
uint32_t gPendMs = 0;

uint32_t gPressMs = 0;      // instante del flanco de pulsacion confirmado
bool gLongFired = false;    // ya se aviso del largo en esta pulsacion
bool gEsperaDoble = false;  // el segundo toque empezo dentro de la ventana

uint8_t gTaps = 0;          // 1 = hay un toque esperando a resolverse
uint32_t gLastTapMs = 0;    // instante en que se SOLTO ese primer toque

// Cola de gestos ya resueltos. Un toque durante un repintado se resuelve dentro de
// la espera del panel y se queda aqui hasta que el bucle pueda ejecutarlo.
constexpr uint8_t kColaMax = 6;
ButtonEvent gCola[kColaMax];
uint8_t gColaN = 0;

ButtonFeedbackFn gFeedback = nullptr;

// ---- tactil ----
#if defined(PIN_BTN_TOUCH)
bool gToquePend = false, gToquePendNivel = false;
uint32_t gToquePendMs = 0;
// ★★ CONTADOR, NO BOOLEANO (2026-09-16) ★★
// ANTES: `bool gToqueHecho`. Con un booleano, si el operador tocaba tres veces mientras la
// pantalla pintaba (1,5 s), el TERCERO sobreescribia al primero y solo se atendia UNO: para
// bajar cuatro filas del menu habia que esperar cuatro repintados. El operador lo describio
// asi: «cuesta bastante esfuerzo y tiempo moverse por los menus, esto rompe la idea del
// Project Butter». El boton FISICO ya tenia su cola (kColaMax); el tactil no tenia nada.
// AHORA: se cuentan los toques aceptados y `buttonTouchPoll()` los va dando de uno en uno,
// asi que N toques = N navegaciones, y el repintado sigue siendo UNO (el aplazamiento de
// kAgrupaToquesMs en epaper_techo.cpp). El tope es una red de seguridad: si alguien apoya
// la mano en la pastilla, no se queda el menu girando 40 filas.
uint8_t gToquesHechos = 0;
constexpr uint8_t kToquesMax = 8;      // toques aceptados que se acumulan como mucho
uint32_t gToqueBloqueoHasta = 0;
uint32_t gTxDesde = 0, gTxHasta = 0;   // ultima ventana de transmision
bool gTxValida = false;
#endif

inline void encola(ButtonEvent ev) {
  if (ev == BTN_NONE) return;
  if (gColaN >= kColaMax) {           // se descarta el mas viejo: el nuevo manda
    for (uint8_t i = 1; i < gColaN; i++) gCola[i - 1] = gCola[i];
    gColaN--;
  }
  gCola[gColaN++] = ev;
}

// ---- ISRs: lo unico que hacen es guardar el nivel con su hora ----
void isrFisico() { buzonMete(gFisico, digitalRead(BUTTON_PIN) == kBtnActivo, millis()); }
#if defined(PIN_BTN_TOUCH)
void isrToque() { buzonMete(gToque, digitalRead(PIN_BTN_TOUCH) == kToqueActivo, millis()); }
#endif

// ===========================================================================
//  FLANCO CONFIRMADO DEL FISICO -> GESTOS
//  Se usan las marcas de tiempo CRUDAS (no el instante en que se confirma): el
//  antirrebote retrasa la DECISION 25 ms, pero no falsea CUANDO paso.
// ===========================================================================
void flancoFisico(bool pulsado, uint32_t ms) {
  if (pulsado == gNivel) return;
  gNivel = pulsado;

  if (pulsado) {                       // ---- PULSACION ----
    gPressMs = ms;
    gLongFired = false;
    // Aviso INMEDIATO (luz + pitido): aqui es donde el operador "nota" el toque,
    // sin esperar a saber si el gesto es corto (600 ms), largo o doble.
    if (gFeedback) gFeedback();
    // ¿Es el segundo toque de un doble? Se mide SUELTA -> PULSACION (ver la tabla).
    if (gTaps == 1 && (uint32_t)(ms - gLastTapMs) <= kClickWindowMs) {
      gEsperaDoble = true;
      gTaps = 0;
      gLongFired = true;               // la suelta de este toque ya no cuenta
      encola(BTN_DOUBLE);              // el doble se decide AQUI, no al soltar
    }
    return;
  }

  // ---- SUELTA ----
  if (gLongFired) { gLongFired = false; gTaps = 0; gEsperaDoble = false; return; }
  if ((uint32_t)(ms - gPressMs) >= kLongMs) {   // mantuvo sin llegar al largo: nada
    gTaps = 0;
    return;
  }
  gTaps = 1;                           // corto pendiente: lo resuelve la ventana
  gLastTapMs = ms;
}

// ===========================================================================
//  TACTIL CAPACITIVO: antirrebote + bloqueo + descarte durante el RF propio
// ===========================================================================
#if defined(PIN_BTN_TOUCH)
void flancoToque(bool pulsado, uint32_t ms) {
  if (!pulsado) return;                // solo interesa el toque, no el destrozo
  // (a) ¿cae dentro de una transmision? La pastilla se dispara con el RF propio:
  //     es un toque FANTASMA, no un dedo. Se descarta y no se vuelve a armar hasta
  //     que la pastilla se suelte (el flanco de bajada llega solo y limpia el estado).
  if (gTxValida && (int32_t)(ms - gTxDesde) >= 0 && (int32_t)(gTxHasta - ms) >= 0) {
    gToquePend = false;
    return;
  }
  // (b) bloqueo tras el toque anterior: un roce largo no vale por tres acciones.
  if ((int32_t)(ms - gToqueBloqueoHasta) < 0) { gToquePend = false; return; }
  gToqueBloqueoHasta = ms + kToqueBloqueoMs;
  // Se CUENTA, no se marca: los toques aceptados se acumulan (ver gToquesHechos) y el
  // bucle los cobra todos juntos, que es lo que permite bajar cuatro filas del menu con
  // cuatro toques aunque la pantalla solo pueda pintar una vez al final.
  if (gToquesHechos < kToquesMax) gToquesHechos++;
}
#endif

// Avanza los dos buzones y resuelve lo que venza. `ahora` = millis().
void procesa(uint32_t ahora) {
  // ------------------------------------------------ fisico
  {
    bool nivel; uint32_t ms;
    while (buzonSaca(gFisico, nivel, ms)) {
      // El cambio anterior aguanto lo suficiente -> era real.
      if (gPend && (uint32_t)(ms - gPendMs) >= kDebounceMs) {
        flancoFisico(gPendNivel, gPendMs);
        gPend = false;
      }
      gPend = true; gPendNivel = nivel; gPendMs = ms;
    }
    if (gPend && (uint32_t)(ahora - gPendMs) >= kDebounceMs) {
      flancoFisico(gPendNivel, gPendMs);
      gPend = false;
    }

    // LARGO: salta al llegar el umbral, mientras se mantiene.
    if (gNivel && !gLongFired && (uint32_t)(ahora - gPressMs) >= kLongMs) {
      gLongFired = true;
      gTaps = 0;
      gEsperaDoble = false;            // un largo no es la segunda mitad de un doble
      encola(BTN_LONG);
    }

    // CORTO: al vencer la ventana del doble. ★ NUNCA con el boton pulsado ni con un
    // flanco sin confirmar: ahi esta el fallo que partia el doble toque en dos.
    if (gTaps == 1 && !gEsperaDoble && !gNivel && !gPend &&
        (uint32_t)(ahora - gLastTapMs) > kClickWindowMs) {
      gTaps = 0;
      encola(BTN_SHORT);
    }
  }

  // ------------------------------------------------ tactil
#if defined(PIN_BTN_TOUCH)
  {
    bool nivel; uint32_t ms;
    while (buzonSaca(gToque, nivel, ms)) {
      if (gToquePend && (uint32_t)(ms - gToquePendMs) >= kToqueEstableMs) {
        flancoToque(gToquePendNivel, gToquePendMs);
        gToquePend = false;
      }
      gToquePend = true; gToquePendNivel = nivel; gToquePendMs = ms;
    }
    if (gToquePend && (uint32_t)(ahora - gToquePendMs) >= kToqueEstableMs) {
      // Confirmado TARDE (el bucle estuvo ciego mas de lo razonable): no se puede
      // responder de un nivel que no se ha visto sostenerse. Se descarta.
      if ((uint32_t)(ahora - gToquePendMs) <= kToqueTardeMs) {
        flancoToque(gToquePendNivel, gToquePendMs);
      }
      gToquePend = false;
    }
  }
#endif
}

}  // namespace

void buttonInit() {
  // ★ PUESTA A CERO COMPLETA (2026-09-15). Esta funcion tiene que dejar el modulo en un
  //   estado conocido SIEMPRE que se llame, no solo la primera vez: si se llama dos veces
  //   (o desde el banco de pruebas, tools/banco_boton), no puede quedar dentro ni un
  //   flanco viejo ni un gesto a medias. Se descubrio probando: al no reiniciar el
  //   estado, un toque del caso anterior rechazaba el del siguiente.
  gFisico.cabeza = gFisico.cola = 0;
  gFisico.ultimo = false;
  gFisico.perdidos = 0;
  gNivel = false;
  gPend = false;
  gPendNivel = false;
  gPendMs = 0;
  gPressMs = 0;
  gLongFired = false;
  gEsperaDoble = false;
  gTaps = 0;
  gLastTapMs = 0;
  gColaN = 0;

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Estado de partida del buzon: el nivel que hay ahora mismo.
  buzonMete(gFisico, digitalRead(BUTTON_PIN) == kBtnActivo, millis());
  // ★ LA PIEZA CLAVE: el flanco lo coge el GPIOTE del chip, no el bucle. A partir de
  //   aqui, pintar la pantalla (1,5-3 s con delay() dentro) ya no puede tragarse un
  //   toque: cuando el bucle vuelva, el flanco esta en el buzon con su hora.
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), isrFisico, CHANGE);

#if defined(PIN_BTN_TOUCH)
  // El TACTIL se prepara AQUI, con el boton fisico, y no en el primer sondeo: asi la
  // interrupcion esta puesta desde el arranque y no hay ninguna ventana en la que un
  // toque no se vea. (Antes se preparaba "perezosamente" en la primera vuelta; con el
  // boton tan facil de tocar antes de la primera vuelta, mejor no depender de eso.)
  gToque.cabeza = gToque.cola = 0;
  gToque.ultimo = false;
  gToque.perdidos = 0;
  gToquePend = false;
  gToquePendNivel = false;
  gToquePendMs = 0;
  gToquesHechos = 0;
  gToqueBloqueoHasta = 0;
  gTxValida = false;
  gTxDesde = gTxHasta = 0;

  /* ★★ SIN RESISTENCIA INTERNA: COMO EL FIRMWARE DE REFERENCIA (2026-09-21) ★★
     Aqui ponia `pinMode(PIN_BTN_TOUCH, INPUT_PULLUP)`. El firmware del aleman (cfr34k), que
     funciona en esta misma placa, configura la pastilla capacitiva asi:
         {PIN_BTN_TOUCH, APP_BUTTON_ACTIVE_LOW, NRF_GPIO_PIN_NOPULL, cb_app_button}
     o sea SIN pull (NOPULL). Y tiene sentido: en una pastilla capacitiva, meterle una
     resistencia de subida interna puede cargar el pad y MATAR LA SENSIBILIDAD: la pastilla
     mueve muy poca carga y la resistencia se la come.
     Lo que decidimos nosotros ("activa en bajo con pull-up") salio de una MEDIDA de una
     unidad, no del fabricante; el aleman dice NOPULL y su firmware es el que funciona.
     SI ALGUNA UNIDAD SE QUEDA CON EL TOQUE PEGADO con esto, el arreglo es volver a PULLUP
     (es una linea) — pero antes se prueba como lo hace el que funciona. */
  pinMode(PIN_BTN_TOUCH, INPUT_PULLUP);
  buzonMete(gToque, digitalRead(PIN_BTN_TOUCH) == kToqueActivo, millis());
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_TOUCH), isrToque, CHANGE);
#endif
}

void buttonPump() { procesa(millis()); }

ButtonEvent buttonPoll() {
  procesa(millis());
  if (gColaN == 0) return BTN_NONE;
  const ButtonEvent ev = gCola[0];
  for (uint8_t i = 1; i < gColaN; i++) gCola[i - 1] = gCola[i];
  gColaN--;
  return ev;
}

void buttonSetFeedback(ButtonFeedbackFn fn) { gFeedback = fn; }

bool buttonTouchPresent() {
#if defined(PIN_BTN_TOUCH)
  return true;
#else
  return false;
#endif
}

bool buttonTouchPoll() {
#if defined(PIN_BTN_TOUCH)
  procesa(millis());
  // Uno por llamada: quien lo use en un `while` los cobra TODOS (es lo que hace
  // bombearBoton() en main.cpp). Antes era un booleano y los toques se pisaban.
  if (gToquesHechos == 0) return false;
  gToquesHechos--;
  return true;
#else
  return false;
#endif
}

void buttonNoteRadioTx(uint32_t desdeMs, uint32_t hastaMs) {
#if defined(PIN_BTN_TOUCH)
  gTxDesde = desdeMs;
  gTxHasta = hastaMs;
  gTxValida = true;
#else
  (void)desdeMs;
  (void)hastaMs;
#endif
}

uint32_t buttonLostEdges() {
#if defined(PIN_BTN_TOUCH)
  return gFisico.perdidos + gToque.perdidos;
#else
  return gFisico.perdidos;
#endif
}
