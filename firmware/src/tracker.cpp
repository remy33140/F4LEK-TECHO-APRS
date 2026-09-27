// tracker.cpp — tracker mode + SmartBeaconing + GPS management (Phase C).
// Presets (standard SmartBeaconing, speeds in km/h, rates in seconds):
//   Human: low 5 / high 20, slow 1800 s / fast 300 s
//   Bike:  low 10 / high 40, slow 900 s / fast 180 s
//   Car:   low 20 / high 100, slow 300 s / fast 30 s
// GPS management (operator design):
//   - first fix after boot: GPS stays on until it fixes (with timeout/retry)
//   - gpsEco OFF (default): GPS always on while needed
//   - gpsEco ON: movement >=3 km/h sustained -> GPS stays on;
//     stationary -> duty cycle 20 s on / 120 s off (warm/hot re-fix);
//     always powers on ~20 s before the next beacon.
// License: GPL-3.0

#include "tracker.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <math.h>

#include "aprs.h"
#include "display.h"
#include "gps.h"
#include "lastpos.h"
#include "power.h"

namespace {

struct SmartPreset {
  float lowKmh;
  float highKmh;
  uint16_t slowSec;
  uint16_t fastSec;
  float turnAngle;
  float turnSlope;
  uint16_t turnTimeSec;
};

const SmartPreset kPresets[4] = {
    {0, 0, 0, 0, 0, 0, 0},                 // 0 = off (fixed interval)
    {5, 20, 1800, 300, 30, 30, 5},         // 1 = human
    {10, 40, 900, 180, 30, 30, 5},         // 2 = bike
    {20, 100, 300, 30, 22, 30, 5},         // 3 = car (fast rate 30 s and a
                                           //     sensitive turn threshold: 22°
                                           //     marks junctions and bends)
};

// Fallback spacing when trackerMinSpacingSecs is not usable.
constexpr uint32_t kDefaultSpacingMs = 30000;

uint32_t minSpacingMs(const DigiConfig &cfg) {
  int s = cfg.trackerMinSpacingSecs;
  if (s < 10) s = 10;
  if (s > 120) s = 120;
  return (uint32_t)s * 1000u;
}

// GPS eco tuning (operator: 3 km/h sustained, warm re-fix cycles)
constexpr float kMoveKmh = 3.0f;
constexpr uint32_t kMoveHoldMs = 6000;    // >= kMoveKmh during >= 6 s
constexpr uint32_t kIdleHoldMs = 60000;   // stationary >= 60 s -> duty cycle
constexpr uint32_t kOnSampleMs = 20000;   // eco: 20 s on
constexpr uint32_t kOffIdleMs = 120000;   // eco: 120 s off
constexpr uint32_t kPreWakeMs = 20000;    // power on before the beacon
// Cold start of a NEO-6M (and most GNSS modules) can take several minutes, so
// the GPS is NOT switched off until it achieves the first fix of this boot
// (operator request). Only once a fix has been achieved does the timeout/retry
// cycle below apply, to save battery when the sky view is lost.
constexpr uint32_t kFirstFixMaxMs = 0;       // 0 = wait indefinitely
constexpr uint32_t kAcquireMaxMs = 180000;   // 3 min to re-acquire after a fix
constexpr uint32_t kAcquireRetryMs = 60000;  // then retry every minute

uint32_t gLastBeaconMs = 0;
uint32_t gLastCornerMs = 0;
uint32_t gDistMoveSince = 0;  // sustained movement for the distance trigger
double gLastLat = 0.0, gLastLon = 0.0;
float gLastCourse = 0.0f;
float gLastSpeedKmh = 0.0f;
bool gFirstFixBeacon = true;

// GPS manager state
uint32_t gGpsOnSince = 0;
uint32_t gGpsRetryAt = 0;
uint32_t gEcoOffAt = 0;
uint32_t gMoveSince = 0;
uint32_t gIdleSince = 0;
bool gMoving = false;
bool gEverFixed = false;  // a fix has been achieved since boot

uint32_t smartRateSec(const SmartPreset &p, float speedKmh) {
  if (speedKmh <= p.lowKmh) return p.slowSec;
  if (speedKmh >= p.highKmh) return p.fastSec;
  float t = (speedKmh - p.lowKmh) / (p.highKmh - p.lowKmh);
  return (uint32_t)(p.slowSec - t * (p.slowSec - p.fastSec));
}

// "Really moving" threshold for the distance trigger, per profile. A GPS with a
// poor solution (indoors, multipath around buildings) invents 3-4 km/h readings
// and 100-200 m position jumps: with a flat 3 km/h gate a parked node was still
// beaconing every few minutes. A car reaches 8 km/h in a second or two and a
// bike 6, while a walker never does - so the walking profile keeps the low gate
// (that is the profile that needs "every X metres" to work at all).
constexpr uint32_t kDistMoveHoldMs = 10000;  // ...and held for >= 10 s

float distMoveKmh(const DigiConfig &cfg) {
  if (cfg.smartBeaconPreset == 3) return 8.0f;  // car
  if (cfg.smartBeaconPreset == 2) return 6.0f;  // bike
  return kMoveKmh;                              // walking / no profile
}

// Baliza estando parado: el operador pidio que el nodo siga publicando su
// posicion aunque no se mueva, pero despacio (una trama cada 15 minutos, unas
// 2 tramas por hora). Antes, "parado" significaba silencio total y los mapas
// (aprs.fi, lora.ham-radio-op.net) se quedaban con una posicion de hace horas
// mientras las tramas de tiempo seguian llegando. Gasta muy poco aire: 15
// minutos entre tramas es 1/30 del ritmo de marcha.
constexpr uint32_t kParkedBeaconMs = 15u * 60u * 1000u;

uint32_t currentRateMs(const DigiConfig &cfg) {
  if (cfg.smartBeaconPreset > 0 && cfg.smartBeaconPreset < 4) {
    // Ajustes por perfil (si el usuario los afino): 0 = default comunitario.
    const SmartPreset &p = kPresets[cfg.smartBeaconPreset];
    uint16_t slow = cfg.profileSlowSec[cfg.smartBeaconPreset]
                  ? (uint16_t)cfg.profileSlowSec[cfg.smartBeaconPreset] : p.slowSec;
    uint16_t fast = cfg.profileFastSec[cfg.smartBeaconPreset]
                  ? (uint16_t)cfg.profileFastSec[cfg.smartBeaconPreset] : p.fastSec;
    SmartPreset eff = p; eff.slowSec = slow; eff.fastSec = fast;
    return smartRateSec(eff, gLastSpeedKmh) * 1000u;
  }
  return (uint32_t)cfg.trackerIntervalSecs * 1000u;
}

uint32_t remainingMs(const DigiConfig &cfg, uint32_t now) {
  if (gLastBeaconMs == 0) return 0;
  uint32_t rate = currentRateMs(cfg);
  // Parado en modo rastreador (o rastreador + repetidor) la unica baliza que
  // puede salir es la lenta de parado, asi que la cuenta atras debe decir 15
  // minutos y no el ritmo de marcha. En modo repetidor la misma cifra alimenta
  // el ahorro del GPS y alli no existe la baliza de parado: se deja intacta.
  if (cfg.mode != 0 && rate < kParkedBeaconMs &&
      gLastSpeedKmh < distMoveKmh(cfg)) {
    rate = kParkedBeaconMs;
  }
  uint32_t elapsed = now - gLastBeaconMs;
  return (elapsed >= rate) ? 0 : (rate - elapsed);
}

// why (optional): reason the beacon is due, for the trip log: 'F' first fix,
// 'R' rate/clock, 'C' corner, 'D' distance, 'M' manual (not set here).
bool beaconDue(const DigiConfig &cfg, const GpsData &g, uint32_t now,
               char *why = nullptr) {
  if (why) *why = 0;
  if (gFirstFixBeacon) {
    if (why) *why = 'F';
    return true;
  }

  bool due = false;
  uint32_t intervalMs = (uint32_t)cfg.trackerIntervalSecs * 1000u;

  // Sustained movement, for the distance trigger only. A single jittery GPS
  // sample (2-5 km/h plus a 100-200 m position jump, typical indoors or with
  // multipath) must never fire a beacon for a node that never moved: seen on
  // the bench, parked, sending every 3 minutes. The timer needs distMoveKmh
  // held for kDistMoveHoldMs, with a hysteresis band so one bad fix does not
  // reset it.
  const float moveKmh = distMoveKmh(cfg);
  if (g.speedKmh >= moveKmh) {
    if (gDistMoveSince == 0) gDistMoveSince = now;
  } else if (g.speedKmh < moveKmh * 0.5f) {
    gDistMoveSince = 0;
  }
  const bool moving = (gDistMoveSince != 0) &&
                      (now - gDistMoveSince >= kDistMoveHoldMs);

  // Baliza de parado. Se calcula aqui, antes del ritmo, porque tiene que ganar
  // a las dos barreras que dejaban al nodo mudo: el silencio de parado y el
  // filtro de distancia minima (un nodo parado nunca recorre la distancia
  // configurada). El disparo por falso movimiento sigue igual de callado.
  const bool parkedDue = !moving && (now - gLastBeaconMs >= kParkedBeaconMs);

  if (cfg.smartBeaconPreset > 0 && cfg.smartBeaconPreset < 4) {
    const SmartPreset &p0 = kPresets[cfg.smartBeaconPreset];
    // tiempo lento/rapido: override del perfil si el usuario lo afino (0 = default)
    uint16_t slow = cfg.profileSlowSec[cfg.smartBeaconPreset]
                  ? (uint16_t)cfg.profileSlowSec[cfg.smartBeaconPreset] : p0.slowSec;
    uint16_t fast = cfg.profileFastSec[cfg.smartBeaconPreset]
                  ? (uint16_t)cfg.profileFastSec[cfg.smartBeaconPreset] : p0.fastSec;
    SmartPreset p = p0; p.slowSec = slow; p.fastSec = fast;
    uint32_t rate = smartRateSec(p, g.speedKmh) * 1000u;
    if (now - gLastBeaconMs >= rate) {
      due = true;
      if (why) *why = 'R';
    }

    // corner pegging
    if (!due && g.speedKmh > p.lowKmh) {
      float dCourse = fabsf(g.courseDeg - gLastCourse);
      if (dCourse > 180.0f) dCourse = 360.0f - dCourse;
      float threshold = p.turnAngle + p.turnSlope / max(g.speedKmh, 1.0f);
      if (dCourse > threshold &&
          now - gLastCornerMs > (uint32_t)p.turnTimeSec * 1000u &&
          (now - gLastBeaconMs) >= minSpacingMs(cfg)) {
        due = true;
        gLastCornerMs = now;
        if (why) *why = 'C';
      }
    }
  } else {
    if (now - gLastBeaconMs >= intervalMs) {
      due = true;
      if (why) *why = 'R';
    }
  }

  // Distance trigger (operator request for driving: 30 s can cover a lot of
  // ground and swallow junctions). Once the configured distance has been
  // covered AND the minimum spacing has elapsed, beacon immediately: this is
  // what turns trackerMinDistanceM into "every X metres" instead of a filter.
  // Speed gate = sustained movement (see `moving` above): parked, the GPS can
  // jump 100+ m with a 2-4 km/h reading and this trigger would fire beacons for
  // a node that never moved.
  if (!due && cfg.trackerMinDistanceM > 0 && moving) {
    float moved = gpsDistanceM(gLastLat, gLastLon, g.lat, g.lon);
    if (moved >= (float)cfg.trackerMinDistanceM &&
        (now - gLastBeaconMs) >= minSpacingMs(cfg)) {
      due = true;
      if (why) *why = 'D';
    }
  }
  // Distance trigger propio del PERFIL (si el usuario lo afino; 0 = usar el global).
  if (!due && cfg.smartBeaconPreset > 0 && cfg.smartBeaconPreset < 4 &&
      cfg.profileDistM[cfg.smartBeaconPreset] > 0 && moving) {
    float moved = gpsDistanceM(gLastLat, gLastLon, g.lat, g.lon);
    if (moved >= (float)cfg.profileDistM[cfg.smartBeaconPreset] &&
        (now - gLastBeaconMs) >= minSpacingMs(cfg)) {
      due = true;
      if (why) *why = 'D';
    }
  }

  // Parked = silence, except for the slow parked beacon (kParkedBeaconMs).
  // Without this gate the slow rate (300 s in the car profile) fires while the
  // node sits still and the distance filter below lets it through as soon as
  // the GPS drifts 150 m, so a parked node published a wandering position every
  // few minutes (seen on the bench). A corner pegging needs speed > 20 km/h, so
  // it is already covered by `moving`.
  if (due && !gFirstFixBeacon && !moving && !parkedDue) due = false;

  // Minimum distance gate: a beacon that came from the clock (slow/fast rate)
  // still has to represent real progress, or the channel is wasted on a node
  // that has barely moved. The parked beacon is exempt on purpose: standing
  // still it can never cover that distance, and its whole job is to keep the
  // maps from going stale.
  if (due && cfg.trackerMinDistanceM > 0 && !gFirstFixBeacon && !parkedDue) {
    float dist = gpsDistanceM(gLastLat, gLastLon, g.lat, g.lon);
    if (dist < (float)cfg.trackerMinDistanceM) due = false;
  }
  // Motivo 'P' (parado) para el registro del viaje: distingue la baliza lenta
  // de aparcado de una de ritmo normal.
  if (due && parkedDue && why) *why = 'P';
  if (!due && why) *why = 0;
  return due;
}

}  // namespace

void trackerInit() {
  gpsInit();
  // Se lee la ultima posicion que quedo guardada en la flash (de este arranque o
  // de un paseo anterior). Solo se LEE: no se escribe nada aqui. Asi el doble
  // toque puede mandarla aunque el GPS todavia no haya fijado.
  lastPosLoad();
}

void gpsManage(const DigiConfig &cfg, uint32_t now, uint32_t nextBeaconInMs) {
  bool needed = (cfg.mode != 0) || (cfg.mode == 0 && cfg.gpsInDigi);

  // ★★ LA SESION "FIJAR COORDS" VA PRIMERO Y MANDA SOBRE EL MODO (2026-09-15) ★★
  //
  // QUE PROBLEMA ARREGLA: la sesion de captura enciende el GPS por su cuenta
  // (trackerSetCoordsStart), pero esta funcion decide el estado del GPS SOLO por
  // el modo de trabajo. En un REPETIDOR con "GPS en repetidor" apagado, `needed`
  // es false, asi que la linea de abajo apagaba el modulo en la MISMA vuelta del
  // bucle en que la sesion lo habia encendido -- y peor todavia: el `return` de
  // mas abajo impide que se llame a gpsUpdate(), o sea que la sesion se quedaba
  // esperando una fijacion que nadie iba a leer NUNCA. De ahi el "Buscando
  // GPS..." eterno y el aviso pidiendo activar "GPS en repetidor".
  //
  // AHORA: mientras hay una captura en marcha, la sesion es la UNICA que manda
  // sobre el GPS: se enciende (si no lo estaba), se lee el NMEA y NO se apaga,
  // sea cual sea el modo y sea cual sea el ajuste "GPS en repetidor".
  //
  // ★ LO QUE NO CAMBIA, Y ES LO IMPORTANTE: al terminar la sesion, esta funcion
  //   vuelve a decidir por el modo como siempre. Un repetidor sigue con el GPS
  //   APAGADO y su ahorro de bateria intacto; lo que se enciende es SOLO durante
  //   la captura. Y si el GPS ya estaba encendido por otro motivo (modo
  //   rastreador, "GPS en repetidor", ahorro de GPS), la sesion NO lo apaga al
  //   terminar: se lo devuelve como estaba (ver setCoordsSueltaGps()).
  if (trackerSetCoordsActive()) {
    if (!gpsPowered()) {
      gpsPower(true);
      gGpsOnSince = now;
      gGpsRetryAt = 0;
      gEcoOffAt = 0;
      gIdleSince = 0;
    }
    gpsUpdate();
    return;
  }

  // EL ESTADO DEL GPS DEPENDE SIEMPRE DEL MODO ACTUAL, NUNCA DE LO ANTERIOR.
  // Gazapo corregido el 2026-09-13 (lo cazo el operador): al cambiar de modo EN
  // CALIENTE, el GPS se quedaba encendido para siempre. Pasaba al pasar del modo
  // 2 al 0 con "GPS en repetidor" activado: el camino de abajo ("en modo
  // rastreador el GPS siempre esta encendido") se saltaba la comprobacion de si
  // sobraba, y nadie lo apagaba. Un digipeater de montana a bateria se quedaba
  // con el GPS chupando decenas de mA sin que nadie lo hubiera pedido.
  // Aqui se decide UNA vez, mirando solo el modo y la configuracion actuales.
  if (needed == gpsPowered()) {
    // Ya esta como debe estar. Solo queda la logica fina (eco) mas abajo.
  } else {
    gpsPower(needed);
    if (needed) {
      gGpsOnSince = now;
      gEcoOffAt = 0;
      gIdleSince = 0;
    } else {
      // Se apaga: se limpian los temporizadores del ahorro para que el proximo
      // encendido empiece de cero y no herede un estado viejo.
      gEcoOffAt = 0;
      gIdleSince = 0;
      gMoveSince = 0;
      gMoving = false;
    }
  }

  if (!needed) return;

  // --- first fix after boot: stay on until it fixes (timeout/retry) ---
  if (!gpsGet().fix) {
    if (!gpsPowered()) {
      if (gGpsRetryAt != 0 && (int32_t)(now - gGpsRetryAt) < 0) return;
      gpsPower(true);
      gGpsOnSince = now;
    }
    gpsUpdate();
    // In tracker / both modes the GPS NEVER sleeps (operator decision): waiting
    // for satellites is the whole point of those modes, and a module that gets
    // switched off loses its hot start and starts over from scratch. The
    // timeout/retry cycle only applies to a digipeater using GPS as a bonus.
    const bool trackerMode = (cfg.mode != 0);
    uint32_t limit = trackerMode ? 0 : (gEverFixed ? kAcquireMaxMs : kFirstFixMaxMs);
    if (!gpsGet().fix && limit != 0 && now - gGpsOnSince > limit) {
      gpsPower(false);
      gGpsRetryAt = now + kAcquireRetryMs;
    }
    return;
  }
  gEverFixed = true;  // from now on the timeout/retry cycle applies

  // --- eco disabled (or tracker/both mode): always on while needed ---
  if (!cfg.gpsEco || cfg.mode != 0) {
    if (!gpsPowered()) {
      gpsPower(true);
      gGpsOnSince = now;
      gEcoOffAt = 0;
    }
    gpsUpdate();
    gMoving = true;
    return;
  }

  // --- eco enabled: movement detection + duty cycle ---
  if (!gpsPowered()) {
    bool waitingOff = (gEcoOffAt != 0) && (now - gEcoOffAt < kOffIdleMs) &&
                      (nextBeaconInMs > kPreWakeMs);
    if (waitingOff) return;
    gpsPower(true);
    gGpsOnSince = now;
    gEcoOffAt = 0;
    gIdleSince = 0;
  }
  gpsUpdate();

  const GpsData &g = gpsGet();
  if (g.fix && g.speedKmh >= kMoveKmh) {
    if (gMoveSince == 0) gMoveSince = now;
    gIdleSince = 0;
    if (now - gMoveSince >= kMoveHoldMs) gMoving = true;  // sustained movement
  } else {
    gMoveSince = 0;
    if (gIdleSince == 0) gIdleSince = now;
    if (now - gIdleSince >= kIdleHoldMs) gMoving = false;  // stationary
  }

  // always power on shortly before the next beacon
  if (nextBeaconInMs <= kPreWakeMs) gMoving = true;

  if (gMoving) return;

  // stationary: 20 s on / 120 s off (warm re-fix thanks to the backup domain)
  if (now - gGpsOnSince >= kOnSampleMs) {
    gpsPower(false);
    gEcoOffAt = now;
  }
}

int16_t trackerBeaconNow(const DigiConfig &cfg) {
  gGpsRetryAt = 0;
  if (!gpsPowered()) {
    gpsPower(true);
    gGpsOnSince = millis();
  }
  if (!gpsWaitFix(15000)) {
    displayPopup("Tracker: no GPS fix");
    return -110;
  }
  const GpsData &g = gpsGet();
  // manual=true: this is the "send a tracker beacon now" a human asked for (CLI
  // `trkbeacon`, OLED menu), so the KISS "host app commands" gate does not stop
  // it. The automatic loop uses aprsSendTrackerBeacon() with the flag off.
  int16_t st = aprsSendTrackerBeacon(cfg, g, 'M', nullptr, true);
  if (st == RADIOLIB_ERR_NONE) {
    gLastBeaconMs = millis();
    gLastLat = g.lat;
    gLastLon = g.lon;
    gLastCourse = g.courseDeg;
    gLastSpeedKmh = g.speedKmh;
    gFirstFixBeacon = false;
    if (gLastCornerMs == 0) gLastCornerMs = millis();
    // Es una posicion real: se recuerda en la flash para el doble toque futuro.
    lastPosSave(g);
  }
  return st;
}

// Doble toque del boton cuando el GPS NO tiene fijacion: manda la ULTIMA
// posicion conocida, la del pico mas reciente (aunque sea de un paseo anterior).
// Peticion del operador (2026-09-13): mejor publicar donde estuvo de verdad que
// publicar la posicion fija configurada, que no es una posicion real.
int16_t trackerBeaconLastKnown(const DigiConfig &cfg) {
  GpsData g;
  bool fixTimeValid = false;
  if (!lastPosFill(g, fixTimeValid)) {
    displayPopup("Tracker: no known position");
    return -111;
  }

  // Con la hora a la que se tomo (no la de ahora: ver lastpos.h). Asi quien lo
  // vea sabe que es la ultima conocida y no una posicion nueva.
  int16_t st = aprsSendTrackerBeacon(cfg, g, 'M', nullptr, true, true);
  if (st == RADIOLIB_ERR_NONE) {
    // OJO: NO se toca gLastBeaconMs ni gLastLat/gLastLon. Esta baliza es un
    // envio a mano de una posicion vieja: no debe alterar el ritmo del
    // rastreador ni contarse como "aqui estoy".
    if (!gpsGet().fix) displayPopup("Sent last known position");
  }
  return st;
}

bool trackerHasLastKnown() { return lastPosValid(); }

// ---------------------------------------------------------------------------
// "Fijar coordenadas actuales" desde el menu (ver tracker.h)
// ---------------------------------------------------------------------------
namespace {
// Lecturas SEGUIDAS con fijacion antes de dar la posicion por buena. A una frase
// por segundo son unos 20 s: barato comparado con la fijacion, y evita guardar
// el salto tipico de los primeros segundos tras el fix.
constexpr uint8_t kStableSamples = 20;

// ★★ LA FIJACION NO TIENE TOPE DE TIEMPO (2026-09-15) ★★
// Decision EXPLICITA del operador: "a veces fijar cuesta 2 minutos que 15; eso no
// deberia ser un problema". Y tiene razon: depende del cielo, de si el nodo esta
// dentro o fuera, y de los dias que lleve el GPS apagado. Un tope solo sirve para
// rendirse justo cuando faltaban diez segundos.
// Lo que SI hay que garantizar es que el operador no se quede atrapado ni a
// oscuras: mientras dura la captura la pantalla dice en QUE paso esta y cuantos
// satelites ve, y un toque de boton CANCELA la sesion (trackerSetCoordsCancel()).
// ESA es la red de seguridad, no un reloj.
//
// ★ POR QUE SE DICE AQUI Y NO SOLO EN EL MENU: esta es la funcion que tiene el GPS
//   encendido. Si algun dia alguien mete un tope de tiempo en esta sesion, que sea
//   a sabiendas de que el modulo se queda encendido hasta que se guarde o se
//   cancele, y de que el operador pidio expresamente que NO lo hubiera.

// ESTADO DE LA SESION. Los valores son los que documenta tracker.h (y los que
// leen las dos pantallas): no se renumeran.
uint8_t gSetCoordsEstado = TRK_COORDS_IDLE;
uint8_t gSetCoordsDone = 0;         // lecturas seguidas con fijacion
bool gSetCoordsWasPowered = false;  // como estaba el GPS antes de empezar
uint32_t gSetCoordsLastSave = 0;
}  // namespace

uint8_t trackerSetCoordsPhase() { return gSetCoordsEstado; }
uint8_t trackerSetCoordsEstado() { return gSetCoordsEstado; }
uint8_t trackerSetCoordsDone() { return gSetCoordsDone; }
uint8_t trackerSetCoordsNeed() { return kStableSamples; }
bool trackerSetCoordsActive() {
  return gSetCoordsEstado == TRK_COORDS_BUSCANDO ||
         gSetCoordsEstado == TRK_COORDS_ASENTANDO;
}

// Apaga el GPS si lo encendio la sesion, o lo deja como estaba si ya estaba
// encendido por otro motivo (modo rastreador, "GPS en repetidor", ahorro de GPS).
// En un solo sitio porque hay TRES finales posibles: guardado, error al guardar y
// sin fijacion. Y se olvida de "como estaba" para que una segunda llamada no
// vuelva a apagar nada.
static void setCoordsSueltaGps() {
  if (!gSetCoordsWasPowered) {
    gpsPower(false);
    // Se limpian los temporizadores del ahorro, igual que cuando gpsManage apaga:
    // asi el proximo encendido (del modo que sea) empieza de cero.
    gEcoOffAt = 0;
    gIdleSince = 0;
    gMoveSince = 0;
    gMoving = false;
  }
  gSetCoordsWasPowered = gpsPowered();
}

void trackerSetCoordsStart() {
  // Se recuerda como estaba para devolverlo igual al terminar.
  gSetCoordsWasPowered = gpsPowered();
  gSetCoordsDone = 0;
  gSetCoordsEstado = TRK_COORDS_BUSCANDO;
  // El GPS lo enciende LA SESION, no el modo de trabajo: asi "Fijar coords"
  // funciona igual en repetidor, en rastreador y en "ambos", y en repetidor no
  // hace falta activar antes "GPS en repetidor" (que era pedirle al operador que
  // hiciera a mano algo que el aparato sabe hacer solo). Es un encendido
  // TEMPORAL: no se toca la configuracion y al terminar se devuelve el modulo a
  // como estaba (ver setCoordsSueltaGps y el guardia de gpsManage).
  if (!gSetCoordsWasPowered) {
    gpsPower(true);
    gGpsOnSince = millis();
    gGpsRetryAt = 0;
  }
}

// Cancelar la sesion (peticion del operador, 2026-09-15): "ya que puede tardar lo
// que tarde, que el usuario pueda salir de ahi con un boton". Deja el estado en
// IDLE (las pantallas vuelven a su sitio) y DEVUELVE EL GPS A COMO ESTABA: si lo
// encendio la sesion, se apaga. No guarda nada: solo se sale.
void trackerSetCoordsCancel() {
  if (gSetCoordsEstado == TRK_COORDS_IDLE) return;
  setCoordsSueltaGps();
  gSetCoordsDone = 0;
  gSetCoordsEstado = TRK_COORDS_IDLE;
  Serial.println("{\"setcoords\":\"cancelado\"}");
}

void trackerSetCoordsTick(uint32_t now) {
  if (!trackerSetCoordsActive()) return;   // idle, o ya terminada

  const GpsData &g = gpsGet();

  if (!g.fix) {
    // Sin fijacion todavia: se sigue esperando LO QUE HAGA FALTA (ver arriba: sin
    // tope de tiempo, decision del operador). Mientras tanto la pantalla enseña
    // los satelites que se ven, que es lo que demuestra que el aparato trabaja.
    // Si se pierde un fix que ya habia, se vuelve al principio de la cuenta (no
    // vale contar lecturas sueltas entre huecos).
    gSetCoordsEstado = TRK_COORDS_BUSCANDO;
    gSetCoordsDone = 0;
    return;
  }

  // Hay fijacion: se cuentan lecturas seguidas.
  gSetCoordsEstado = TRK_COORDS_ASENTANDO;
  gSetCoordsDone++;

  if (gSetCoordsDone < kStableSamples) return;

  // Suficientes lecturas seguidas: se guarda ESTA, la ultima valida.
  const bool ok = displaySaveCoords(g.lat, g.lon);
  gSetCoordsEstado = ok ? TRK_COORDS_GUARDADO : TRK_COORDS_ERROR;
  gSetCoordsLastSave = now;

  // Se devuelve el GPS a como estaba: si lo encendimos nosotros, se apaga; si ya
  // estaba encendido (rastreador, "GPS en repetidor"), se deja encendido.
  setCoordsSueltaGps();

  Serial.print("{\"setcoords\":\"");
  Serial.print(ok ? "ok" : "save_failed");
  Serial.print("\",\"lat\":");
  Serial.print(g.lat, 6);
  Serial.print(",\"lon\":");
  Serial.print(g.lon, 6);
  Serial.print(",\"sats\":");
  Serial.print((unsigned)g.sats);
  Serial.println("}");
}

void trackerLoop(DigiConfig &cfg, uint32_t now) {
  if (cfg.mode == 0) return;  // digi mode is handled from main (gpsInDigi)

  gpsManage(cfg, now, remainingMs(cfg, now));

  const GpsData &g = gpsGet();
  if (!gpsPowered() || !g.fix) return;

  char why = 0;
  if (!beaconDue(cfg, g, now, &why)) return;

  int16_t st = aprsSendTrackerBeacon(cfg, g, why);
  if (st == RADIOLIB_ERR_NONE) {
    gLastBeaconMs = now;
    gLastLat = g.lat;
    gLastLon = g.lon;
    gLastCourse = g.courseDeg;
    gLastSpeedKmh = g.speedKmh;
    gFirstFixBeacon = false;
    if (gLastCornerMs == 0) gLastCornerMs = now;
    // Se recuerda en la flash (con su propio limite de ritmo, ver lastpos.cpp).
    lastPosSave(g);
  }

  // Timed deep sleep between beacons (never on USB, never in "Ambos" mode:
  // the digi must keep repeating). Note: bypasses the GPS eco logic.
  if (cfg.trackerSleep && cfg.mode != 2 && !powerUsbPresent()) {
    // Reserve ~60 s for the fresh GPS fix after the reset so the next beacon
    // still fires around the configured interval.
    uint32_t asleep = (uint32_t)cfg.trackerIntervalSecs;
    if (asleep > 90) asleep -= 60;
    char b[32];
    snprintf(b, sizeof(b), "Sleeping %us", (unsigned)asleep);
    displayPopup(b);
    delay(1500);
    powerSleepTimed(cfg, asleep);
  }
}

TrackerDiag trackerDiag(const DigiConfig &cfg) {
  TrackerDiag d;
  d.firstFix = gFirstFixBeacon;
  // gMoving solo vale en el camino de ahorro del repetidor: en modo rastreador
  // gpsManage lo pone a true siempre. Informamos de lo real (si la ultima
  // baliza salio con el nodo en marcha) para que el estado no mienta.
  d.moving = (cfg.mode == 0) ? gMoving : (gLastSpeedKmh >= distMoveKmh(cfg));
  d.lastBeaconMs = gLastBeaconMs;
  d.nextBeaconMs = remainingMs(cfg, millis());
  d.lastCornerMs = gLastCornerMs;
  return d;
}
