// display.h — interfaz de la pantalla de tinta electronica del T-Echo (200x200),
// implementada en epaper_techo.cpp: carrusel de escenas, avisos, menu y asistente SOTA.
// License: GPL-3.0

#pragma once

#include <Arduino.h>

#include "config.h"
#include "sensors.h"

void displayInit();

// Segunda parte del arranque de la pantalla, para las placas de TINTA ELECTRONICA.
// Tiene que llamarse DESPUES de radioSetup(): en este core el objeto global `SPI` de
// Arduino vive en el mismo periferico (SPIM3) que usa la pantalla, asi que el arranque de
// la radio deja el periferico configurado con los pines de la radio y la pantalla se queda
// sin recibir nada. Aqui se reengancha el periferico a los pines de la pantalla y se manda
// la secuencia de arranque del panel.
void displayInitTrasRadio();

// Retroiluminacion de la pantalla de tinta (P1.11, solo T-Echo). Peticion del
// operador (2026-09-15): la luz se ENCIENDE al interactuar con un boton y se
// apaga sola a los 5 s. `displayBacklightKick()` la reenciende (y renueva el
// temporizador); `displayBacklightTick()` hay que llamarla cada vuelta del bucle
// para apagarla cuando venza el plazo.
void displayBacklightKick();
void displayBacklightTick(uint32_t nowMs);

// Pitido corto del buzzer (T-Echo Plus, P0.06), sonido audible pero no agudo (2026-09-15).
// Se llama al pulsar cualquier boton (fisico o capacitivo). Bloquea ~60 ms.
void displayBeep();

// ★ AVISO SONORO DE BATERIA BAJA (T-Echo Plus, P0.06). Melodia descendente "triste"
// (peticion del operador, 2026-09-15: el aviso de bateria baja de los Nokia viejos).
// Va ANTES del popup de "me voy a dormir": el nodo se apaga y hay que enterarse.
// OJO: BLOQUEA mientras suena (~1,3 s). Solo se usa en ese camino, justo antes de
// dormir, donde ese segundo no importa. En placas sin buzzer no hace nada.
void displayLowBatTone();

// Diagnostico de la pantalla de tinta electronica en una linea, a peticion (comando "epd").
// Existe porque los mensajes de arranque NO se ven nunca: el USB no esta enumerado todavia,
// y el registro no puede usarse en el arranque (rompe el USB).
void displayDiagTexto(char *out, size_t n);

// Arranca el panel de tinta electronica, pero LLAMADA DESDE EL BUCLE PRINCIPAL, no desde
// setup(): asi el USB ya esta vivo y, si el panel se bloquea, el nodo sigue respondiendo y
// se puede diagnosticar (y volver a grabar por software).
void displayArrancaPantalla();

bool displayPresent();
bool displayIsOn();
void displayWake();
void displaySleep();

// Arm the ~4 s boot splash (firmware name, version, build date and mode).
void displaySplash();

// Bluetooth pairing splash (Meshtastic behaviour): puts the PIN the phone is
// asking for on a clean screen, waking the panel if it was off, and keeps it
// there until displayPinSplashClear() — pairing succeeded, failed or was
// abandoned — or a 60 s safety timeout, so the screen can never be left stuck.
// fromStack=true when the PIN came from the stack's passkey event, false when it
// is the PIN stored in the configuration.
void displayPinSplash(const char *pin, bool fromStack);
void displayPinSplashClear();
bool displayPinSplashActive();

// Config binding for the on-device menu (call once at boot).
void displayBindConfig(DigiConfig *cfg);

// On-device menu (2026-09-15, e-paper port). Capacitive = navigate / edit value;
// físico corto (menuShort) = enter/confirm; físico largo (menuLong) = go back.
bool menuIsOpen();
bool menuIsEditing();
void menuOpen();
void menuClose();
void menuShort();
void menuLong();
void menuNavigate();   // capacitivo: mover selección o cambiar el valor en edición
void menuEditCancel();

// Asistente de spot SOTA (sota.h/sota.cpp llevan los datos y la navegacion;
// esto son solo las funciones que main.cpp necesita ver para enrutar los
// mismos tres gestos que el menu de ajustes -- ver la nota larga junto a
// sotaOpen() en epaper_techo.cpp).
bool sotaIsOpen();
void sotaOpen();
void sotaClose();
void sotaNavigate();
void sotaShort();
void sotaLong();

// Scene navigation (button short press) and manual popup. Changing the scene
// with the button also pauses the auto-advance for a few seconds.
// ★ `porToque` (2026-09-15, T-Echo Project Butter): true cuando el cambio viene del
// TACTIL CAPACITIVO. El tactil puede ir muy rapido (no tiene ventana de doble toque),
// asi que en la tinta se AGRUPA el repintado mientras se toca seguido; el boton
// fisico, en cambio, ya llega con su ventana cumplida y no se aplaza nada.
void displayNextScene(bool porToque = false);
void displayPopup(const char *text);

// ★ Bombeo durante un repintado (T-Echo Project Butter, 2026-09-15).
// La pantalla de tinta tarda 1,5-3 s por refresco y su driver ESPERA al panel dentro
// de la vuelta del bucle: en ese rato la maquina de gestos no corria, asi que un toque
// caido al principio del refresco se atendia un refresco entero mas tarde. El driver
// llama a este gancho en sus esperas (cada 2 ms). El gancho NO ejecuta acciones: solo
// captura los flancos, resuelve los plazos vencidos y ENCOLA el gesto, para que el
// bucle lo ejecute en cuanto el panel quede libre. Lo pone main.cpp.
// ★ Desde el 2026-09-16 (Project Butter II) ese MISMO gancho lee tambien el puerto USB
//   y encola sus bytes, por el mismo motivo: el bucle se pasa el repintado entero sin
//   mirar el USB y el FIFO del CDC son 256 bytes. Tampoco ejecuta comandos: los cobra
//   el bucle (gProtocol.atiende), nunca el driver.
void displaySetPumpBoton(void (*fn)(void));

// Guarda una posicion en la configuracion (y por tanto en la flash) usando el
// mismo camino que el menu: merge + validacion + storeSave. Devuelve true si se
// guardo. Lo usa la sesion "Fijar coordenadas actuales" del rastreador.
bool displaySaveCoords(double lat, double lon);

// Blocking popup with a seconds countdown, shown on a clean screen for totalMs
// (used right before going to sleep so the notice is actually seen).
void displayPopupWait(const char *text, uint32_t totalMs);

// Event hooks (also feed the rings + popups).
// kind: "Bcn" (position/beacon), "Msg" (message), "Pkt" (other APRS frame).
void displayNoteRx(const char *from, float rssi, float snr, const char *kind);
void displayNoteDigi(const char *from, float rssi, float snr);
void displayNoteTx(const char *what);  // "BEACON", "TELEM", "MUTE"...

// New APRS message addressed to us (kind "Msg" in displayNoteRx, but this one
// carries the message BODY): stored for the "Mensajes"/"MENSAJES" scene of
// the home carousel, so a received message survives past its 8 s popup
// instead of being lost as soon as the next event arrives. `text` is the raw
// message body as decoded by aprs.cpp (handleRemoteMessage) -- it may still
// carry a trailing "{nnn" ack number, which the display layer strips before
// showing it.
void displayNoteMsg(const char *from, const char *text);

// End of one of OUR messages (the one aprs.cpp was resending while waiting for
// its ack): acknowledged, rejected, or given up after the retries. The SOTA
// scene uses it to show what became of the spot sent to APRS2SOTA.
enum MsgEnd : uint8_t { MSG_END_ACK, MSG_END_REJ, MSG_END_NOACK };
void displayNoteMsgEnd(const char *to, MsgEnd how);

// Periodic refresh (throttled internally ~500 ms).
void displayRefresh(const DigiConfig &cfg, uint32_t rxCount, uint32_t txCount,
                    uint32_t digiCount, const SensorReadings &r);
