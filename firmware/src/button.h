// button.h — Boton FISICO (P1.10, activo en bajo) y TACTIL CAPACITIVO (P0.11, T-Echo).
//
// Tres gestos con el fisico (a proposito, para que sea predecible con un boton):
//   corto  = se suelta antes del umbral de largo (se resuelve al vencer la ventana
//            de doble toque, para no confundirlo con dos cortos)
//   largo  = SALTA MIENTRAS SE MANTIENE, al llegar al umbral (no hay que adivinar
//            el momento de soltar)
//   doble  = el SEGUNDO toque EMPIEZA dentro de la ventana
// No hay gesto de "muy largo". Lo usan la interfaz y el menu (fases A/B).
//
// ★★ T-ECHO PROJECT BUTTER (2026-09-15): AQUI ESTABA EL PROBLEMA DE LOS TOQUES ★★
//
// QUE PASABA: el boton se leia a RATOS desde el bucle (`digitalRead` cada vuelta) y
// el bucle se pasa 1,5-3 s seguidos pintando la pantalla de tinta, que es bloqueante.
// Un toque que EMPEZABA Y ACABABA dentro de ese rato no se veia NUNCA: al volver el
// bucle, el pin ya estaba suelto, o sea el MISMO nivel que antes de pintar, y la
// maquina no tenia nada que procesar. El toque se perdia entero, sin rastro. No era
// cosa del antirrebote ni de la ventana: era que nadie miraba.
//
// QUE HACE AHORA, y es lo que hace el firmware de referencia de esta placa
// (cfr34k/t-echo-lora-aprs, `app_button` de Nordic): los FLANCOS los coge una
// INTERRUPCION (GPIOTE) y se guardan con su marca de tiempo en un buzon pequeno. Da
// igual lo que este haciendo el bucle: el flanco no se pierde. La maquina de gestos
// se alimenta de esas marcas de tiempo, no del `digitalRead` de turno, asi que un
// toque ocurrido durante un repintado se resuelve con SUS tiempos y se obedece en
// cuanto el panel queda libre.
//
// License: GPL-3.0

#pragma once

#include <Arduino.h>

// ★★ LA TABLA DE TIEMPOS, EN UN SOLO SITIO (T-Echo Project Butter, 2026-09-15) ★★
// Se publica aqui, y no se deja escondida dentro de button.cpp, por dos motivos:
//   1) que la tabla se pueda LEER de un vistazo (y comprobar que no se contradice), y
//   2) que el banco de pruebas (tools/banco_boton) use EXACTAMENTE estos numeros.
// Significado de cada uno, sin ambiguedad:
//   BUTTON_DEBOUNCE_MS     el nivel del pin tiene que aguantar esto para contar como flanco
//   BUTTON_LONG_MS         el LARGO salta AL LLEGAR, mientras se mantiene el dedo
//   BUTTON_CLICK_WINDOW_MS de SOLTAR el 1er toque a PULSAR el 2º (y lo que tarda el
//                          CORTO en resolverse: es el mismo plazo, a proposito)
#define BUTTON_DEBOUNCE_MS 25
#define BUTTON_LONG_MS 600
#define BUTTON_CLICK_WINDOW_MS 600

enum ButtonEvent {
  BTN_NONE = 0,
  BTN_SHORT,
  BTN_LONG,
  BTN_DOUBLE,
};

void buttonInit();

// Saca el siguiente gesto pendiente del fisico (BTN_NONE si no hay ninguno).
// Llamar desde loop(): captura, resuelve y devuelve de uno en uno.
ButtonEvent buttonPoll();

// Lo mismo que buttonPoll() pero SIN sacar nada: solo captura lo que haya en el
// buzon y resuelve los gestos cuyo plazo venza, dejandolos ENCOLADOS. Es lo que
// llama la pantalla de tinta desde sus esperas (1,5-3 s por refresco) para que un
// toque caido durante el repintado no se atienda 1 s tarde. NO ejecuta acciones:
// solo encola; las acciones las sigue ejecutando el bucle (ver main.cpp).
void buttonPump();

// Aviso INMEDIATO de "ha habido un toque": se llama en cuanto el flanco de
// PULSACION queda confirmado (unos 25 ms despues de poner el dedo), SIN esperar a
// saber si el gesto sera corto, largo o doble. Es lo que permite que la luz y el
// pitido respondan al instante aunque la accion tarde (la pantalla de tinta no se
// puede pintar antes de 1,5 s). Se ejecuta en contexto de bucle, nunca en la ISR.
typedef void (*ButtonFeedbackFn)(void);
void buttonSetFeedback(ButtonFeedbackFn fn);

// ---------------------------------------------------------------------------
//  TACTIL CAPACITIVO (solo T-Echo / T-Echo Plus: PIN_BTN_TOUCH = P0.11)
//  En las placas sin pastilla tactil esto queda inerte (buttonTouchPresent()
//  devuelve false y buttonTouchPoll() siempre false).
// ---------------------------------------------------------------------------
bool buttonTouchPresent();

// true UNA vez por toque aceptado: llamarlo EN UN `while` para cobrar la rafaga entera
// (2026-09-16: antes era un solo aviso y los toques de una rafaga se pisaban, asi que
// bajar cuatro filas del menu costaba cuatro repintados). Un toque se acepta solo si el
// nivel se mantiene estable el tiempo minimo (40 ms), si no cae dentro de una transmision
// de radio (el RF dispara la pastilla: hallazgo del firmware de referencia) y si ha pasado
// el bloqueo desde el toque anterior. Un toque = UNA accion: eso es lo que quita los
// toques fantasma.
bool buttonTouchPoll();

// El emisor de radio ha estado en el aire entre esos dos millis(). La pastilla
// capacitiva del T-Echo se dispara con el RF propio, asi que un toque cuyo flanco
// caiga ahi se DESCARTA. Lo llama radio.cpp alrededor de cada transmision.
void buttonNoteRadioTx(uint32_t desdeMs, uint32_t hastaMs);

// Contador de diagnostico: flancos que no cupieron en el buzon (deberia ser 0).
uint32_t buttonLostEdges();
