// protocol.cpp — WebSerial/USB-CDC config protocol v1 implementation
// License: GPL-3.0

#include "protocol.h"

#include <ArduinoJson.h>
#include <RadioLib.h>
#include <nrf.h>  // NVIC_SystemReset() del reinicio suave (igual que cli.cpp)

#include "cli.h"
#include "diag.h"
#include "display.h"
#include "gps.h"
#include "radio.h"   // boardName() / radioModuleName() del informe de estado
#include "sensors.h"
#include "tnc.h"

/* CONTADORES DEL USB (instrumentacion, 2026-09-13).
   PARA QUE: el operador reporta que "al cabo de un rato el nodo deja de
   escucharlo, como si el USB se muriera, pero el nodo sigue vivo". Para saber si
   es que NO LLEGAN BYTES (el CDC esta roto) o que llegan y no se contestan, se
   cuentan aqui. Se consultan en el JSON de estado: status.usb.bytes / .lineas. */
uint32_t gUsbBytes = 0;
uint32_t gUsbLineas = 0;

/* ★★ CONTADORES DEL BOMBEO DEL USB (T-Echo Project Butter II, 2026-09-16) ★★
   PARA QUE: poder COMPROBAR EN LA PLACA, sin instrumentos, que el driver de la tinta esta
   leyendo el puerto durante el repintado. Se publican en `status.usb` (JSON) y en el comando
   de taller `usb` (CLI), que es una sola palabra de escribir.
     - gUsbBombeo  : bytes leidos del puerto DENTRO de las esperas del driver de la tinta.
                     Si esto sube mientras la pantalla pinta, el bombeo funciona.
     - gUsbColaMax : maximo de bytes que han llegado a estar encolados de una vez (lo que el
                     driver ha podido absorber durante un repintado).
     - gUsbDentro  : RED DE SEGURIDAD. Cuenta los comandos ejecutados dentro de un bombeo, o
                     sea DENTRO del driver: pintar dentro de un pintado. Tiene que ser 0
                     SIEMPRE. Si algun dia no lo es, alguien ha metido la ejecucion ahi.
     - gUsbTirados : caracteres que sobran de una linea mas larga que el tope (4096). No es
                     un error, es el tope: sirve para ver si alguien manda lineas enormes. */
uint32_t gUsbBombeo = 0;
uint32_t gUsbColaMax = 0;
uint32_t gUsbDentro = 0;
uint32_t gUsbTirados = 0;

/* ================= LATIDO DEL USB (diagnostico, ver protocol.h) ==============
   Una muestra por segundo en RAM: bytes recibidos, hueco mayor del bucle y si el
   segundo se quedo sin nada. NO se escribe en la flash a proposito: escribir en el
   registro es sospechoso de ser LA CAUSA del fallo, asi que usarlo de diario
   falsearia la prueba. Se consulta por RADIO, porque si el USB muere no se puede
   preguntar por el USB. */
UsbDiag gUsbDiag[kUsbDiagN];
int gUsbDiagIdx = 0;
int gUsbDiagTotal = 0;
uint32_t gUsbUltimoAlSanoMs = 0;
static uint32_t sUltimaMuestraMs = 0;
static uint32_t sUltimosBytes = 0;
static uint32_t sUltimoPasoMs = 0;

void usbDiagTick(uint32_t now) {
  const uint32_t hueco = (sUltimoPasoMs == 0) ? 0 : (now - sUltimoPasoMs);
  sUltimoPasoMs = now;
  const int ult = (gUsbDiagIdx + kUsbDiagN - 1) % kUsbDiagN;
  if (sUltimaMuestraMs != 0 && (now - sUltimaMuestraMs) < 1000) {
    // Mismo segundo: solo se apunta el hueco MAYOR del bucle. Eso es lo que caza
    // un atasco del NVMC (un salto grande entre dos pasadas del bucle).
    if (gUsbDiagTotal > 0 && hueco < 250 && hueco > gUsbDiag[ult].mayorHuecoMs) {
      gUsbDiag[ult].mayorHuecoMs = (uint8_t)hueco;
    }
    return;
  }
  const uint32_t delta = gUsbBytes - sUltimosBytes;
  sUltimosBytes = gUsbBytes;
  gUsbDiag[gUsbDiagIdx].bytes = (uint16_t)(delta > 65535u ? 65535u : delta);
  gUsbDiag[gUsbDiagIdx].mayorHuecoMs = (uint8_t)(hueco < 255 ? hueco : 255);
  gUsbDiag[gUsbDiagIdx].muerto = (delta == 0);
  gUsbDiagIdx = (gUsbDiagIdx + 1) % kUsbDiagN;
  if (gUsbDiagTotal < kUsbDiagN) gUsbDiagTotal++;
  sUltimaMuestraMs = now;
  if (delta > 0) gUsbUltimoAlSanoMs = now;
}

/* Resumen para el ?USB? por radio: cuantos segundos seguidos sin recibir nada y el
   hueco mayor del bucle en los ultimos 10 minutos. */
int usbDiagResumen(char *out, size_t n) {
  int muertosSeguidos = 0;
  for (int i = gUsbDiagTotal - 1; i >= 0; i--) {
    const int k = (gUsbDiagIdx + kUsbDiagN - 1 - i) % kUsbDiagN;
    if (!gUsbDiag[k].muerto) break;
    muertosSeguidos++;
  }
  uint8_t peor = 0;
  for (int i = 0; i < gUsbDiagTotal; i++) {
    const int k = (gUsbDiagIdx + kUsbDiagN - 1 - i) % kUsbDiagN;
    if (gUsbDiag[k].mayorHuecoMs > peor) peor = gUsbDiag[k].mayorHuecoMs;
  }
  return snprintf(out, n, "USB %s bytes %lu lineas %lu | sordos %ds | bucle peor %ums",
                  APP_BUILD_NUM, (unsigned long)gUsbBytes, (unsigned long)gUsbLineas,
                  muertosSeguidos, (unsigned)peor);
}



/* ★ RESUMEN DEL BOMBEO DEL USB (herramienta de taller, T-Echo Project Butter II).

   PARA QUE: comprobar EN LA PLACA que el driver de la tinta lee el puerto durante el
   repintado. Se pide con una sola palabra por el USB: `usb`.

   COMO SE LEE:
     - `bombeo` sube cada vez que la pantalla pinta: son los bytes que el driver ha leido
       MIENTRAS esperaba al panel (antes de este cambio tenia que ser 0 SIEMPRE).
     - `cola` (el maximo) dice cuanto ha llegado a acumularse de una vez: con el `set` del
       configurador (~1.4 KB) tiene que acercarse a esos 1400, no a los 256 bytes del FIFO.
     - `dentro` es la red de seguridad del diseno y TIENE QUE SER 0: comandos ejecutados
       dentro de un repintado (pintar dentro de un pintado). */
int usbBombeoResumen(char *out, size_t n) {
  return snprintf(out, n,
                  "USB %s lineas %lu | bombeo %lu cola %lu | dentro %lu (%s) | tirados %lu",
                  APP_BUILD_NUM, (unsigned long)gUsbLineas,
                  (unsigned long)gUsbBombeo, (unsigned long)gUsbColaMax,
                  (unsigned long)gUsbDentro, gUsbDentro == 0 ? "bien" : "REENTRADA",
                  (unsigned long)gUsbTirados);
}

// Version que se anuncia si nadie la define en la compilacion. La de verdad la
// pone platformio.ini (-DAPP_VERSION_STR): esto es solo el valor de socorro, y se
// mantiene igual para que no haya dos versiones distintas segun quien compile.
#ifndef APP_VERSION_STR
#define APP_VERSION_STR "1.0alpha"
#endif
// Numero de compilacion interno (lo pone platformio.ini). Valor de socorro por si
// alguien compila a mano: asi nunca falta el dato para saber que se grabo.
#ifndef APP_BUILD_NUM
#define APP_BUILD_NUM "b0"
#endif


/* ================= EL PUERTO USB, VISTO POR EL LECTOR ======================
   El lector (usb_lector.h) es C++ puro: no sabe que el puerto es el `Serial` del core.
   Estos dos thunks son todo el pegamento. */
namespace {
int puertoDisponible(void *ctx) {
  Stream *s = static_cast<Stream *>(ctx);
  return s->available();
}
int puertoLee(void *ctx) {
  Stream *s = static_cast<Stream *>(ctx);
  return s->read();
}

/* ★★ POR DONDE SALE LO QUE EL NODO CONTESTA (2026-09-17, Bluetooth) ★★
   Ver protocol.h. Es UN solo sitio para los dos transportes: el origen de la ultima linea
   atendida decide si la respuesta va por el cable o por el aire. La puerta del Bluetooth la
   registra `bleLinkInit()` (ble_kiss.cpp) al arrancar: asi esta cabecera no tiene que incluir
   la del enlace, que a su vez incluye esta. */
Origen gOrigen = Origen::Usb;
ProtocolSalidaFn gSalidaBle = nullptr;
bool gSalidaBleActiva = false;
ProtocolEstadoFn gEstadoBle = nullptr;

void salidaCruda(const char *p, size_t n) {
  if (n == 0) return;
  if (gOrigen == Origen::Ble && gSalidaBle != nullptr && gSalidaBleActiva) {
    // La espera es CORTA y FINITA a proposito: si el enlace esta atascado (el movil se alejo
    // y el aviso no sale), la respuesta se pierde pero el nodo NO se queda colgado. Las
    // lineas normales se vacian solas desde bleLoop(), asi que esto no espera nada.
    gSalidaBle((const uint8_t *)p, n, 250);
    return;
  }
  Serial.write((const uint8_t *)p, n);
}
}  // namespace

void protocolSalidaBle(ProtocolSalidaFn escribir, bool activa) {
  gSalidaBle = escribir;
  gSalidaBleActiva = activa;
}

void protocolEstadoBle(ProtocolEstadoFn rellenar) { gEstadoBle = rellenar; }

void protocolHostOut(const char *linea) {
  if (linea == nullptr || linea[0] == '\0') return;
  salidaCruda(linea, strlen(linea));
  salidaCruda("\n", 1);
}

void protocolHostWrite(const char *texto, size_t n) { salidaCruda(texto, n); }

/* Deja salir lo que este en la cola del enlace. Solo lo llama el reinicio: hay que avisar
   ANTES de resetear, y por Bluetooth el aviso tarda unos milisegundos en salir. Es una espera
   CORTA Y FINITA: si el enlace esta atascado se pierde el aviso, pero el nodo no se cuelga. */
void protocolFlushSalida() {
  if (gOrigen != Origen::Ble || gSalidaBle == nullptr || !gSalidaBleActiva) {
    Serial.flush();
    return;
  }
  // 400 ms de margen para vaciar la cola de avisos antes de resetear.
  gSalidaBle(nullptr, 0, 400);
}

Origen configProtocolOrigen() { return gOrigen; }

/* ★★ LOS BYTES DEL BLUETOOTH ENTRAN POR AQUI (2026-09-17) ★★
   El enlace Bluetooth (ble_kiss.cpp) llama a esto desde `bleLoop()`, con el trozo que le acaba
   de escribir el huesped. NO hay un segundo parser: los bytes van al MISMO receptor de lineas
   que los del cable, con la unica diferencia de que van marcados como Bluetooth (para que la
   respuesta salga por el aire) y de que NO se le ofrecen al TNC (el KISS es del cable: ver
   usb_lector.h). El troceado y el despacho son exactamente los mismos. */
/* ★★ LOS BYTES DEL BLUETOOTH ENTRAN POR AQUI (2026-09-17) ★★
   El enlace Bluetooth (ble_kiss.cpp) llama a esto desde `bleLoop()`, con el trozo que le acaba
   de escribir el huesped. NO hay un segundo parser: los bytes van al MISMO receptor de lineas
   que los del cable, con la unica diferencia de que van marcados como Bluetooth (para que la
   respuesta salga por el aire) y de que NO se le ofrecen al TNC (el KISS es del cable: ver
   usb_lector.h). El troceado y el despacho son exactamente los mismos.

   El puntero al protocolo lo deja `configProtocolBind()`, que llama main.cpp UNA vez al
   arrancar: el protocolo es un objeto global de main.cpp y este fichero no tiene por que
   conocerlo. */
namespace {
ConfigProtocol *gProtocol = nullptr;
uint32_t gBleAnilloPerdidos = 0;
}  // namespace

void configProtocolBind(ConfigProtocol *p) { gProtocol = p; }

bool configProtocolEmpujaBle(const uint8_t *datos, size_t n) {
  if (gProtocol == nullptr) return false;
  return gProtocol->empujaBle(datos, n);
}

void configProtocolOlvidaLineaBle() {
  if (gProtocol == nullptr) return;
  gProtocol->olvidaLineaBle();
}

uint32_t bleAnilloPerdidos() { return gBleAnilloPerdidos; }

void bleHostFeed(const uint8_t *datos, size_t n) {
  if (datos == nullptr || n == 0) return;
  // Se empuja al anillo del lector. El troceado de lineas NO ocurre aqui: ocurre en el unico
  // sitio que interpreta bytes (`UsbLector::meteByte`), cuando el bucle llama a `atiende()`,
  // que es esa misma vuelta un poco mas abajo en main.cpp. Asi una orden del Bluetooth nunca
  // se ejecuta dentro de la tarea del SoftDevice.
  if (!configProtocolEmpujaBle(datos, n)) {
    // Anillo lleno (una linea larguisima a medias): se cuenta y se deja dicho. NO se ejecuta
    // nada con la linea cortada.
    gBleAnilloPerdidos++;
  }
}

/* ★★ LEER Y ENCOLAR: LA PUERTA DEL DRIVER DE LA PANTALLA (T-Echo Project Butter II) ★★
   Esto es lo unico que el driver de la tinta puede llamar del protocolo. Saca del puerto lo
   que haya y lo deja en el anillo del lector, EN ORDEN. NO interpreta una sola linea, NO
   ejecuta un solo comando: por eso se puede llamar desde dentro de un repintado sin que
   nadie acabe pintando dentro del pintado ni escribiendo la flash en medio de una
   transaccion con el panel. La ejecucion la cobra `feed()`/`atiende()`, en el bucle. */
void ConfigProtocol::bombea(Stream &s, bool enEsperaDePantalla) {
  UsbPuerto p;
  p.ctx = &s;
  p.disponible = puertoDisponible;
  p.lee = puertoLee;
  if (lector_.bombea(p, enEsperaDePantalla) > 0) {
    // Instrumentacion de siempre: gUsbBytes son los bytes que HAN LLEGADO por el USB.
    gUsbBytes = lector_.leidos();
  }
  // Contadores del bombeo (los lee el diagnostico: ver gUsbBombeo arriba).
  gUsbBombeo = lector_.bombeadosEnEspera();
  gUsbColaMax = (uint32_t)lector_.maxPendientes();
  gUsbTirados = lector_.tirados();
}

/* El bucle: leer el puerto Y EJECUTAR lo que venga. Unica puerta que ejecuta. */
void ConfigProtocol::feed(Stream &s) {
  // Abandon a KISS frame the host left half-sent (killed app, reset mid-write)
  // before looking at new bytes: otherwise it would eat every following line as
  // KISS payload and the operator would be locked out of their own node.
  tncUsbPoll();
  // ★ Se quita la marca del driver: si alguien dejo puesta la de la pantalla (por ejemplo
  //   desde un aviso que bloquea, como displayPopupWait), aqui se limpia antes de ejecutar
  //   nada, para que la red de seguridad no cuente un falso positivo.
  lector_.marcaEnPantalla(false);
  bombea(s, false);   // leer el puerto (y encolar; ninguna linea se interpreta aqui)
  lector_.atiende();  // y ejecutar: el unico sitio donde se obedece un comando
  gUsbDentro = lector_.ejecutadasEnBombeo();   // red de seguridad: tiene que ser 0
}

// El lector nos da una linea completa (ya sin CR/LF) y de donde viene. Mismo orden que el
// feed() de antes: primero se la ofrece al TNC (una trama TNC2 la manda el TNC, no el parser
// JSON/CLI) y, si no era una trama, va al parser de siempre.
void ConfigProtocol::lineaRecibida(const char *linea, size_t n, Origen origen) {
  (void)n;  // el lector ya la deja terminada en cero
  // ★ PRIMERO EL ORIGEN, y no es un detalle: el despacho de abajo ya contesta, y la respuesta
  //   tiene que salir por donde entro el comando (ver protocolHostOut).
  origen_ = origen;
  gOrigen = origen;
  // ★ ORDEN EXACTO DEL feed() DE ANTES: primero se le ofrece al TNC (una trama TNC2 la
  //   manda el TNC, no el parser JSON/CLI) y, si no era una trama, va al parser de
  //   siempre. La comprobacion del TNC solo se hace si hay puente TNC2 encendido: asi el
  //   caso normal (USB de configuracion) no reserva memoria para nada.
  if (tncTnc2Active()) {
    String line(linea);
    if (tncHandleLine(line)) return;  // era una trama TNC2: ya la ha mandado el TNC
  }
  handleLine(linea);
  gUsbLineas++;
}

bool ConfigProtocol::hookTnc(void *ctx, uint8_t b) {
  (void)ctx;
  return tncHandleUsbByte(b);   // la regla del 0xC0 (KISS): no se toca
}

void ConfigProtocol::hookLinea(void *ctx, const char *linea, size_t n, Origen origen) {
  static_cast<ConfigProtocol *>(ctx)->lineaRecibida(linea, n, origen);
}


// ★ LA RESPUESTA SALE POR DONDE ENTRO EL COMANDO (ver protocolHostOut en la cabecera). Son
//   las dos unicas puertas de salida del protocolo: el cable y el Bluetooth.
void ConfigProtocol::sendLine(const String &s) { protocolHostOut(s.c_str()); }

static void sendJsonDoc(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  protocolHostOut(out.c_str());
}

void ConfigProtocol::replyOkWithConfig(bool persisted) {
  JsonDocument doc;
  doc["ok"] = true;
  configToJson(cfg_, doc["config"].to<JsonObject>());
  doc["persisted"] = persisted;
  sendJsonDoc(doc);
}

void ConfigProtocol::replyGet() { replyOkWithConfig(false); }

void ConfigProtocol::replyStatus() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["status"]["version"] = APP_VERSION_STR;
  doc["status"]["build"] = APP_BUILD_NUM;   // numero de compilacion interno
  // Instrumentacion del USB: si al nodo "se le muere el USB", estos numeros dicen
  // si los bytes siguen llegando (el problema seria de respuestas) o si se han
  // parado (entonces el CDC esta roto).
  {
    JsonObject usb = doc["status"]["usb"].to<JsonObject>();
    usb["bytes"] = gUsbBytes;
    usb["lineas"] = gUsbLineas;
    // ★★ BOMBEO DEL USB DURANTE EL REPINTADO (T-Echo Project Butter II, 2026-09-16) ★★
    //   `bombeo`  = bytes leidos del puerto DENTRO de las esperas del driver de la tinta.
    //               Si el operador toca la pantalla/menu y esto sube, el bombeo funciona.
    //   `colaMax` = maximo de bytes que han llegado a estar encolados de una vez: es lo que
    //               el driver ha podido absorber durante un repintado.
    //   `dentro`  = RED DE SEGURIDAD y tiene que ser 0 SIEMPRE: comandos ejecutados dentro
    //               de un bombeo (seria pintar dentro de un pintado). Si algun dia sale
    //               distinto de 0, alguien ha metido la ejecucion dentro del driver.
    usb["bombeo"] = gUsbBombeo;
    usb["colaMax"] = gUsbColaMax;
    usb["cola"] = (uint32_t)lector_.pendientes();
    usb["dentro"] = gUsbDentro;
    usb["tirados"] = gUsbTirados;
  }
  doc["status"]["uptimeMs"] = millis();
  // ★ PLACA (2026-09-21): "T-Echo" / "Faketec HT-RA62" / "Faketec E22P". El configurador
  //   web la usa para PROPONER los valores recomendados buenos de cada aparato (sobre todo
  //   los umbrales de bateria, que no son los mismos). Antes no habia forma de saberlo y
  //   recomendaba los de la Faketec a todo el mundo. Ver radio.h (boardName()).
  doc["status"]["board"] = boardName();
  JsonObject radio = doc["status"]["radio"].to<JsonObject>();
  radio["module"] = radioModuleName();  // "HT-RA62" / "E22P" (web: power limits)
  radio["state"] = radioState();
  radio["powerDbm"] = radioPowerDbm();  // what the radio is really set to
  radio["err"] = radioLastErr();
  radio["rxCount"] = radioRxCount();
  radio["txCount"] = radioTxCount();
  radio["digiCount"] = aprsDigiCount();
  radio["lastRssi"] = radioLastRssi();
  radio["lastSnr"] = radioLastSnr();
  radio["rxlog"] = radioRxLog();
  // NOTE: "txtest" (periodic raw-LoRa TX test) used to be reported here. It was
  // removed from the firmware on 2026-09-15: see radioSendFrame() in radio.cpp.
  radio["tnc"] = (int)tncProtocol();
  radio["kissPaused"] = tncKissPaused();   // override del operador (kissoff)

  JsonObject sensors = doc["status"]["sensors"].to<JsonObject>();
  sensors["wx"] = gSensorCache.wxOk;
  sensors["wxChip"] = sensorsWxName();  // which probe was detected
  sensors["ina"] = gSensorCache.inaOk;
  sensors["tempC"] = gSensorCache.tempC;
  sensors["hum"] = gSensorCache.hum;
  sensors["hPa"] = gSensorCache.pressHpa;
  sensors["vbatDivV"] = gSensorCache.vbatDivV;
  sensors["inaBusV"] = gSensorCache.inaBusV;
  sensors["inaMa"] = gSensorCache.inaCurrentMa;
  doc["status"]["display"] = displayPresent();
  doc["status"]["diag"] = diagStreaming();

  // ★★ ESTADO DEL ENLACE BLUETOOTH (2026-09-17) ★★
  //   Lo rellena el propio enlace (registra su funcion al arrancar, ver protocolEstadoBle), para
  //   que se pueda comprobar DESDE EL NODO si el Bluetooth anuncia, si hay huesped y si el PIN
  //   se esta pidiendo. `canal` lo pone el protocolo: es el dato que distingue "me contesta por
  //   el cable" de "me contesta por el aire".
  {
    JsonObject ble = doc["status"]["ble"].to<JsonObject>();
    ble["canal"] = (configProtocolOrigen() == Origen::Ble) ? "ble" : "usb";
    if (gEstadoBle != nullptr) gEstadoBle(ble);
  }

  // GPS: the web configurator offers "use the GPS coordinates" for a fixed
  // digipeater and shows whether the module has a fix yet.
  const GpsData &g = gpsGet();
  JsonObject gps = doc["status"]["gps"].to<JsonObject>();
  gps["present"] = gpsPowered();
  gps["fix"] = g.fix;
  gps["sats"] = g.sats;
  gps["inView"] = g.satsInView;
  gps["hdop"] = g.hdop;
  gps["lat"] = g.lat;
  gps["lon"] = g.lon;
  gps["altM"] = g.altValid ? g.altM : 0.0f;
  gps["speedKmh"] = g.speedKmh;
  gps["courseDeg"] = g.courseDeg;
  gps["timeValid"] = (g.timeValid && g.dateValid);
  sendJsonDoc(doc);
}

void ConfigProtocol::handleRadio(const JsonVariantConst &body) {
  if (body["rxlog"].is<bool>()) radioSetRxLog(body["rxlog"] | false);
  // ★ EL COMANDO "txtest" YA NO EXISTE (2026-09-15). Encendia una emision
  //   periodica de "RADIOBLINK" por el camino CRUDO de la radio: sin prefijo
  //   LoRa, sin formato APRS y sin indicativo. Se elimino del firmware por
  //   decision del operador. Si llega la clave se IGNORA (no se contesta error,
  //   para no romper a un configurador viejo) y el estado de abajo ya no la
  //   informa: el campo desaparece de la respuesta y el panel deja de ofrecerlo.
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject radio = doc["radio"].to<JsonObject>();
  radio["rxlog"] = radioRxLog();
  radio["muted"] = cfg_.txDisabled;
  sendJsonDoc(doc);
}

void ConfigProtocol::handleDiag(const JsonVariantConst &body) {
  if (body["active"].is<bool>()) diagSetActive(body["active"] | false);
  if (body["nmea"].is<bool>()) diagSetNmea(body["nmea"] | false);
  JsonDocument doc;
  doc["ok"] = true;
  doc["diag"]["active"] = diagActive();
  doc["diag"]["nmea"] = diagNmea();
  doc["diag"]["streaming"] = diagStreaming();
  sendJsonDoc(doc);
}

void ConfigProtocol::handleBeacon() {
  // Mode 0 (fixed digipeater): without a callsign and a configured position there is
  // nothing to send, and that is an error worth saying out loud ("beacon not
  // configured").
  //
  // ★★ EN MODO RASTREADOR (1/2) LA ORDEN LLEGA SIEMPRE, Y DECIDE aprsSendManualBeacon()
  //    (2026-09-16) ★★
  //   Con fijacion GPS manda la posicion REAL; sin fijacion **la IGNORA** y contesta
  //   -110 ("sin fijacion GPS"): un rastreador no baliza sin fix, y la orden a mano no
  //   es la excepcion (la excepcion es el doble toque del boton, que manda la ultima
  //   conocida a proposito; ver aprs.cpp y docs/MANUAL_DE_USO.md §3).
  //   Antes esta compuerta solo dejaba pasar la fijacion de AHORA y contestaba
  //   "beacon not configured" a un rastreador sin coordenadas fijas (0,0, que es como
  //   debe estar un rastreador), que es un motivo FALSO: lo que pasa es que no hay fix.
  //   Dejandola pasar, la app y el configurador reciben el motivo verdadero.
  if (cfg_.mode == 0 && !aprsCanBeacon(cfg_)) {
    replyError("beacon not configured (callsign/position)");
    return;
  }
  int16_t st = aprsSendManualBeacon(cfg_);
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject beacon = doc["beacon"].to<JsonObject>();
  beacon["tx"] = (st == RADIOLIB_ERR_NONE);
  beacon["code"] = st;
  // ★ EL MOTIVO, CON PALABRAS (2026-09-16). El `code` solo lo entiende quien lee el
  //   codigo: -110 = sin fijacion GPS (la orden se ignora en modo rastreador), -100 =
  //   no configurado / KISS manda / canal ocupado, -111 = no hay nada real que mandar.
  //   Con este campo, el configurador y la app pueden decir QUE PASA en vez de un
  //   numero. Es un campo de MAS: quien no lo conozca lo ignora y sigue igual.
  if (st != RADIOLIB_ERR_NONE) {
    beacon["why"] = (st == -110)   ? "sin fijacion GPS (un rastreador no baliza sin fix)"
                     : (st == -100) ? "no se puede balizar (configuracion o canal)"
                                    : "no hay posicion real que mandar";
  }
  sendJsonDoc(doc);
}

// APRS message started from the web panel: {"cmd":"msg","to":"EA2XXX-7",
// "text":"hola"}. Without "to" the node writes to the last heard station.
void ConfigProtocol::handleMessage(const JsonDocument &doc) {
  String to = doc["to"] | "";
  String text = doc["text"] | "";
  to.trim();
  text.trim();
  if (to.length() == 0) to = aprsLastHeardCall();
  if (to.length() == 0) {
    replyError("no station heard yet");
    return;
  }
  if (text.length() == 0) {
    replyError("empty message");
    return;
  }
  int16_t st = aprsSendMessage(cfg_, to.c_str(), text.c_str());
  JsonDocument out;
  const bool ok = (st == RADIOLIB_ERR_NONE);
  out["ok"] = ok;
  JsonObject m = out["msg"].to<JsonObject>();
  m["to"] = to;
  m["text"] = text;
  m["tx"] = ok;
  m["code"] = st;
  if (!ok) out["error"] = "message not sent";
  sendJsonDoc(out);
}

void ConfigProtocol::replyError(const char *err) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = err;
  sendJsonDoc(doc);
}

void ConfigProtocol::handleLine(const char *line) {
  if (line == nullptr || line[0] == '\0') return;

  // ★ DE DONDE VIENE ESTA ORDEN (2026-09-17). Manda en dos cosas:
  //   1) por donde sale la respuesta (ver protocolHostOut);
  //   2) el modo grabacion: `dfu` se RECHAZA por Bluetooth, porque nadie podria terminar la
  //      grabacion (el cargador UF2 se maneja copiando un fichero por el cable).
  const Origen origen = origen_;

  // Hybrid (operator decision): a line that does NOT start with '{' is a
  // NavaCLI-style token command -> human-readable text reply (cli.cpp).
  if (line[0] != '{') {
    String t = cliExecute(cfg_, line, cliOrigenDe(origen));
    if (t.length() > 0) sendLine(t);
    return;
  }

  JsonDocument doc;
  DeserializationError derr = deserializeJson(doc, line);
  if (derr) {
    replyError("bad json");
    return;
  }
  const char *cmd = doc["cmd"] | "";
  // ★★ EL MODO GRABACION NO SE MANDA POR BLUETOOTH (orden del operador, 2026-09-17) ★★
  //   POR QUE: `dfu` reinicia el nodo en el cargador UF2, y el cargador se maneja copiando un
  //   fichero en una unidad que solo existe con el cable puesto. Si la orden llega por
  //   Bluetooth, el nodo se reinicia, DEJA DE EXISTIR como nodo y ya no hay forma de completar
  //   nada por el aire: se queda fuera de juego hasta que alguien le enchufe un cable. Por eso
  //   se rechaza con un motivo claro en vez de obedecer. Por USB, igual que siempre.
  if (strcmp(cmd, "dfu") == 0 && origen == Origen::Ble) {
    replyError(
        "el modo grabacion necesita el cable USB: no se puede completar por Bluetooth "
        "(usa el cable)");
    return;
  }
  if (strcmp(cmd, "get") == 0) {
    replyGet();
  } else if (strcmp(cmd, "status") == 0) {
    replyStatus();
  } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "reset") == 0) {
    // Reinicio SUAVE, sin tocar la configuracion. "reset" es alias a proposito:
    // el operador lo mando creyendo que reiniciaba el nodo y perdio su
    // configuracion, asi que "reset" ya NO puede borrar nada. El borrado de
    // fabrica vive en "factory_reset" (siguiente rama). Mismo patron que el
    // verbo "reboot confirm" del CLI: avisar, vaciar el serial y resetear.
    protocolHostOut("REBOOT");
    // ★ El aviso sale por donde entro la orden (cable o Bluetooth) y se le da un respiro
    //   CORTO Y FINITO para que llegue antes del reinicio: por Bluetooth un aviso tarda unos
    //   milisegundos en salir de la cola. NO se espera a que el huesped lo lea.
    protocolFlushSalida();
    delay(100);
    NVIC_SystemReset();
  } else if (strcmp(cmd, "factory_reset") == 0) {
    // ★ FACTORY RESET CONSERVANDO EL ACCESO (2026-09-15, decision del operador).
    // POR QUE: esta rama borraba ABSOLUTAMENTE TODO, incluido el indicativo. El
    // aparato se quedaba sin identidad y sin forma de entrar: habia que empezar
    // de cero desde un ordenador, con el cable. El "factory_reset confirm" de la
    // consola (cli.cpp) ya lo hacia bien desde el principio, asi que aqui se
    // REUTILIZA ESE MISMO PATRON: se guardan las tres cosas que dan acceso,
    // se restaura la configuracion de fabrica y se devuelven las tres.
    //   - callsign    : identidad de la estacion (sin el, el nodo no transmite)
    //   - managers    : lista de operadores autorizados (ACL del control remoto)
    //   - remoteEnabled: si el control remoto esta aceptado
    // Lo demas SI se borra: es un reset de fabrica de verdad.
    // OJO: esto NO es el `wipe` de la consola (cli.cpp), que a proposito borra
    // TODO, incluido el indicativo, y ademas exige `wipe confirm` y ser USB.
    // NO "ARREGLAR" ESTO EN SENTIDO CONTRARIO: no es un descuido, es la norma.
    char keepCall[16], keepMgrs[64];
    bool keepRemote = cfg_.remoteEnabled;
    strncpy(keepCall, cfg_.callsign, sizeof(keepCall) - 1);
    keepCall[sizeof(keepCall) - 1] = 0;
    strncpy(keepMgrs, cfg_.managers, sizeof(keepMgrs) - 1);
    keepMgrs[sizeof(keepMgrs) - 1] = 0;
    configSetDefaults(cfg_);
    strncpy(cfg_.callsign, keepCall, sizeof(cfg_.callsign) - 1);
    cfg_.callsign[sizeof(cfg_.callsign) - 1] = 0;
    strncpy(cfg_.managers, keepMgrs, sizeof(cfg_.managers) - 1);
    cfg_.managers[sizeof(cfg_.managers) - 1] = 0;
    cfg_.remoteEnabled = keepRemote;
    bool p = storeSave(cfg_);
    radioSetMuted(cfg_.txDisabled);
    radioApplyPower(cfg_.powerDbm);
    replyOkWithConfig(p);
  } else if (strcmp(cmd, "radio") == 0) {
    handleRadio(doc["radio"].as<JsonObjectConst>());
  } else if (strcmp(cmd, "diag") == 0) {
    handleDiag(doc["diag"].as<JsonObjectConst>());
  } else if (strcmp(cmd, "beacon") == 0) {
    handleBeacon();
  } else if (strcmp(cmd, "msg") == 0) {
    handleMessage(doc);
  } else if (strcmp(cmd, "set") == 0) {
    if (!doc["config"].is<JsonObject>()) {
      replyError("missing config object");
      return;
    }
    String err;
    if (!configFromJson(cfg_, doc["config"].as<JsonObjectConst>(), err)) {
      replyError(err.c_str());
      return;
    }
    bool p = storeSave(cfg_);  // atomic save to internal flash (store.h)
    radioSetMuted(cfg_.txDisabled);
    radioApplyPower(cfg_.powerDbm);
    replyOkWithConfig(p);
  } else {
    replyError("unknown cmd");
  }
}
