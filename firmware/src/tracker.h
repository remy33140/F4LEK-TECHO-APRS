// tracker.h — tracker mode + SmartBeaconing (Phase C).
// Presets (standard SmartBeaconing): Human / Bike / Car with low/high speed
// rates, corner pegging (turn angle + slope + time) and minimum distance.
// License: GPL-3.0

#pragma once

#include <Arduino.h>

#include "config.h"

void trackerInit();

// GPS power/movement/eco manager. `nextBeaconInMs` is used to power the GPS
// on shortly before the next beacon. Call every loop (tracker modes or when
// "GPS en repetidor" is enabled).
void gpsManage(const DigiConfig &cfg, uint32_t now, uint32_t nextBeaconInMs);

// Non-blocking scheduler: GPS power/eco, beacon due logic, TX, sleep-between.
void trackerLoop(DigiConfig &cfg, uint32_t now);

// Force a tracker beacon now (menu/button). Returns APRSPacket code.
int16_t trackerBeaconNow(const DigiConfig &cfg);

// Send the LAST KNOWN position (the most recent real GPS fix, remembered across
// reboots in the flash: see lastpos.h). This is what the double button tap uses
// when the GPS has no fix right now: instead of publishing the fixed
// configuration position (which is not a real position), it publishes where the
// node really was, with the time it was taken.
// Returns APRSPacket code, or -111 when there is nothing remembered yet.
int16_t trackerBeaconLastKnown(const DigiConfig &cfg);

// Is there a real GPS position remembered (from this boot or a previous one)?
bool trackerHasLastKnown();

// --- "Fijar coordenadas actuales" desde el menu de la pantalla ----------------
// Peticion del operador (2026-09-13), para colocar un digipeater en el monte:
// pulsar una vez en el menu y que el nodo haga TODO el trabajo: encender el GPS,
// esperar a que fije, dejar que se asiente, guardar la posicion en la
// configuracion y volver a apagar el GPS.
//
// Decisiones del operador que estan implementadas aqui:
//   - Se esperan kStableSamples lecturas SEGUIDAS con fijacion antes de guardar
//     (el GPS da saltos de decenas o cientos de metros justo despues de fijar;
//     guardar en el instante del fix seria guardar la peor posicion).
//   - Se guarda la ULTIMA lectura valida, no la primera.
//   - El encendido es TEMPORAL y NO se guarda en la configuracion: es una sesion
//     de un solo uso. Si el nodo se reinicia a mitad, el GPS vuelve apagado y no
//     se queda gastando bateria sin que nadie lo haya pedido.
//   - Si el GPS ya estaba encendido (p. ej. "GPS en repetidor" activado, o el modo
//     rastreador), se respeta y NO se apaga al terminar: se le devuelve a su
//     estado anterior.
//
// ★★ A QUIEN OBEDECE EL GPS DURANTE LA CAPTURA (2026-09-15) ★★
//   LO GOBIERNA LA SESION, NO EL MODO DE TRABAJO. Antes, en modo REPETIDOR la
//   sesion no servia para nada salvo que el operador activara antes "GPS en
//   repetidor": gpsManage() apagaba el modulo en la misma vuelta del bucle y ni
//   siquiera leia el NMEA, asi que la pantalla se quedaba en "Buscando GPS..."
//   para siempre. El operador dijo, con razon, que eso es pedirle a mano algo que
//   el aparato sabe hacer solo.
//   AHORA la sesion enciende el GPS por su cuenta mientras dura (lo hace
//   trackerSetCoordsStart, y gpsManage() atiende a la sesion POR DELANTE del modo)
//   y lo devuelve a como estaba al terminar. Fuera de la captura, el repetidor
//   sigue con el GPS APAGADO y su ahorro de bateria intacto.
//   - SIN TOPE DE TIEMPO (decision explicita del operador): "a veces fijar cuesta
//     2 minutos que 15; eso no deberia ser un problema". Se espera lo que haga
//     falta. Lo que SI hay es informacion constante en la pantalla (paso actual y
//     satelites a la vista) y SALIDA A MANO con un boton: ver
//     trackerSetCoordsCancel().
void trackerSetCoordsStart();

// Llamar en cada vuelta del bucle. Avanza la sesion anterior si esta en marcha.
void trackerSetCoordsTick(uint32_t now);

// Cancelar la sesion en marcha (boton en la pantalla de progreso): NO guarda
// nada, deja el estado en TRK_COORDS_IDLE y DEVUELVE EL GPS a como estaba antes de
// empezar (si lo encendio la sesion, lo apaga). Sin sesion en marcha no hace nada.
void trackerSetCoordsCancel();

// Estado de la sesion, para las pantallas. Los valores NO se renumeran: los leen
// la OLED (display.cpp) y la tinta (epaper_techo.cpp).
enum {
  TRK_COORDS_IDLE = 0,       // no hay sesion
  TRK_COORDS_BUSCANDO = 1,   // GPS encendido, esperando fijacion (lo que tarde)
  TRK_COORDS_ASENTANDO = 2,  // con fijacion, contando lecturas seguidas
  TRK_COORDS_GUARDADO = 3,   // guardada en la configuracion
  TRK_COORDS_ERROR = 4,      // no se pudo guardar
};
uint8_t trackerSetCoordsEstado();

// Compatibilidad: mismo valor que trackerSetCoordsEstado() (las pantallas viejas
// leian "fase"). 0 = nada en marcha; 1 = buscando, 2 = asentando, 3 = guardada,
// 4 = error.
uint8_t trackerSetCoordsPhase();
uint8_t trackerSetCoordsDone();
uint8_t trackerSetCoordsNeed();

// ¿Hay una captura en marcha AHORA (buscando o asentando)? Lo usa gpsManage()
// para no apagar el GPS que la sesion ha encendido, y las pantallas para saber si
// tienen que enseñar el progreso (mientras) o el resultado (al terminar).
bool trackerSetCoordsActive();

// Live tracker state for the diagnostics stream (diag.cpp).
struct TrackerDiag {
  bool firstFix;
  bool moving;
  uint32_t lastBeaconMs;
  uint32_t nextBeaconMs;
  uint32_t lastCornerMs;
};
TrackerDiag trackerDiag(const DigiConfig &cfg);
