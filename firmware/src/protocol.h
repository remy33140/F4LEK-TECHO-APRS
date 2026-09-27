// protocol.h — WebSerial/USB-CDC config protocol v1
// Line-based JSON over USB-CDC 115200 8N1, LF terminated.
// Unlike CA2RXU (fire-and-forget) this protocol ALWAYS replies (ACK + read-back).
// The same port also carries the CLI (lines not starting with '{') and the TNC
// bridge: a 0xC0 byte starts a binary KISS frame and is diverted to
// tncHandleUsbByte() before any text handling, so KISS, JSON and the CLI coexist
// and none of them can lock the operator out.
// Full spec: docs/protocol_config_v1.md
// License: GPL-3.0

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "aprs.h"
#include "config.h"
#include "radio.h"
#include "store.h"
#include "usb_lector.h"

/* ================= LATIDO DEL USB (diagnostico, 2026-09-13) =================
   PARA QUE: el operador reporta que "tras un rato, el nodo deja de escuchar el
   USB, como si se muriera, pero el nodo sigue vivo". No se sabe cuando ni por que.
   Aqui se apunta un LATIDO POR SEGUNDO EN RAM (NUNCA en la flash: escribir en el
   registro es sospechoso de ser LA CAUSA, asi que usarlo de diario falsearia la
   prueba). De cada segundo se guardan los bytes recibidos, el HUECO MAYOR del
   bucle (eso caza los atascos del NVMC) y si ese segundo se quedo sin nada.
   Se consulta POR RADIO (?USB?), porque si el USB se muere no se puede preguntar
   por el USB. */
// 300 muestras = 5 minutos de historia, que sobra para ver QUE paso. La muestra se
// guarda COMPACTA (campos ordenados de mayor a menor para que el compilador no meta
// relleno, y los bytes en 16 bits): asi son 3 bytes por muestra en vez de 8, porque
// el nodo tiene 243 KB de RAM y no se puede gastar 5 KB en un diagnostico.
constexpr int kUsbDiagN = 300;
struct UsbDiag {
  uint16_t bytes;        // bytes de ese segundo (tope 65535)
  uint8_t mayorHuecoMs;  // hueco mayor del bucle en ese segundo (tope 255)
  uint8_t muerto;        // 1 = ese segundo no llego nada
};
extern UsbDiag gUsbDiag[kUsbDiagN];
extern int gUsbDiagIdx;
extern int gUsbDiagTotal;
extern uint32_t gUsbUltimoAlSanoMs;
extern uint32_t gUsbBytes;
extern uint32_t gUsbLineas;
void usbDiagTick(uint32_t now);           // llamar desde el bucle
int usbDiagResumen(char *out, size_t n);  // texto para la consulta ?USB?

/* ★ RESUMEN DEL BOMBEO DEL USB (T-Echo Project Butter II, 2026-09-16): lineas atendidas,
   bytes leidos DENTRO de las esperas del driver de la tinta, maximo que ha llegado a estar
   encolado y comandos ejecutados dentro de un repintado (esto ultimo tiene que ser 0).
   Lo usa el comando de taller `usb` del CLI, que es una sola palabra de escribir. */
int usbBombeoResumen(char *out, size_t n);

/* ★★ LA SALIDA DEL PROTOCOLO VA POR DONDE VINO EL COMANDO (2026-09-17, Bluetooth) ★★

   POR QUE: el Bluetooth es un segundo puerto serie del nodo, asi que una respuesta tiene que
   salir por el mismo sitio por el que entro su orden. Si saliera siempre por el USB, la app
   conectada por Bluetooth no veria NUNCA una respuesta (nodo mudo); y si saliera siempre por
   el Bluetooth, el configurador web dejaria de contestar. Por eso el protocolo apunta el
   ORIGEN de la linea que esta atendiendo y manda por ahi todo lo que produce.

   Son las DOS unicas puertas de salida del nodo, y las dos pasan por aqui:
     - `protocolHostOut()`   -> una linea completa (respuesta JSON/CLI, "REBOOT"...)
     - `protocolHostWrite()` -> ASCII sin partir (el volcado del registro de viaje)
   Quien escribe cuando NO es una respuesta (el diagnostico en flujo) usa las mismas puertas,
   y asi no se cuelan lineas por el puerto equivocado. */
void protocolHostOut(const char *linea);
void protocolHostWrite(const char *texto, size_t n);
void protocolFlushSalida();   // deja salir lo encolado antes de un reinicio (espera finita)

/* ★ LA PUERTA DEL BLUETOOTH SE REGISTRA, NO SE INCLUYE (2026-09-17) ★
   POR QUE ASI Y NO CON UN `#include "ble_kiss.h"` AQUI: el enlace Bluetooth incluye ESTA
   cabecera (necesita olvidar la linea a medias del huesped), asi que incluirla al reves seria
   un ciclo de cabeceras. Se rompe con una funcion registrada, que ademas deja el protocolo
   sin saber nada del SoftDevice: solo sabe que hay una segunda salida de bytes. El registro lo
   hace `bleLinkInit()` (ble_kiss.cpp), una vez, al arrancar. */
typedef size_t (*ProtocolSalidaFn)(const uint8_t *datos, size_t n, uint32_t esperaMs);
void protocolSalidaBle(ProtocolSalidaFn escribir, bool activa);

/* ★ Y LO MISMO PARA EL ESTADO (2026-09-17): el enlace registra una funcion que rellena el
   objeto JSON `status.ble` del comando `status`. Asi la app y el operador pueden comprobar
   DESDE EL PROPIO NODO si el Bluetooth esta anunciando, si hay huesped, el MTU y si el PIN se
   esta pidiendo, sin que esta cabecera tenga que conocer al enlace (mismo motivo que arriba).
   `canal` lo rellena el protocolo: dice por donde ha entrado ESTE comando. */
typedef void (*ProtocolEstadoFn)(JsonObject &destino);
void protocolEstadoBle(ProtocolEstadoFn rellenar);

// Origen de la ultima linea atendida (Origen::Usb si todavia no ha llegado ninguna).
Origen configProtocolOrigen();

/* ★★ POR DONDE ENTRAN LOS BYTES DEL BLUETOOTH (2026-09-17) ★★

   El enlace Bluetooth llama a `configProtocolEmpujaBle()` con cada trozo que le escribe el
   huesped. Los bytes NO se trocean aqui: van al anillo del MISMO receptor de lineas que usa
   el cable, y el unico sitio que decide que es KISS, que es texto y donde acaba una linea
   sigue siendo `UsbLector::meteByte()`. El bucle los cobra con `atiende()`.

   Devuelve false si el anillo esta lleno (entonces se pierde el trozo: se prefiere eso a
   corromper la linea que se estaba formando). */
bool configProtocolEmpujaBle(const uint8_t *datos, size_t n);

// Tira la linea del Bluetooth a medias. La llama el enlace al desconectar el huesped: si una
// escritura se corto (el movil se alejo a mitad de un JSON), sus restos no pueden quedarse ahi
// para envenenar la primera linea de la conexion siguiente.
void configProtocolOlvidaLineaBle();

// Bytes del Bluetooth que no cupieron en el anillo (diagnostico).
uint32_t bleAnilloPerdidos();

class ConfigProtocol {
 public:
  explicit ConfigProtocol(DigiConfig &cfg) : cfg_(cfg) {
    // El lector nos devuelve cada linea completa, con su origen. Se engancha una sola vez, y
    // el MISMO par de ganchos atiende a los dos transportes: el USB y el Bluetooth comparten
    // receptor de lineas y despacho (no hay un segundo parser en ninguna parte).
    lector_.init(&hookTnc, &hookLinea, this);
  }

  // ★★ LAS DOS PUERTAS DEL USB, Y LA DIFERENCIA ENTRE ELLAS ES TODO EL ASUNTO ★★
  //
  // `feed()` (BUCLE PRINCIPAL): lee el puerto Y EJECUTA lo que venga. Es la unica puerta que
  // ejecuta: quien manda una baliza, cambia la configuracion o reinicia el nodo es el bucle.
  //
  // `bombea()` (DRIVER DE LA TINTA): LEE Y ENCOLA, y nada mas. Es lo que llama el driver en
  // sus esperas de 1,5-3 s (gancho `displaySetPumpBoton`, ver main.cpp), para que el puerto
  // no se quede sin mirar durante el repintado. **No puede ejecutar un comando**: hacerlo
  // seria pintar dentro de un pintado o escribir la flash en medio de una transaccion con el
  // panel. Mismo patron que el buzon de gestos del boton (Project Butter).
  void feed(Stream &s);
  void bombea(Stream &s, bool enEsperaDePantalla = true);

  // ★ COBRA LO ENCOLADO (solo desde el bucle). Se llama justo despues de `displayRefresh()`
  //   para que un comando que llego mientras el panel pintaba se atienda en cuanto el panel
  //   queda libre, sin esperar a la vuelta siguiente del bucle.
  void atiende() { lector_.atiende(); }

  // ★ MARCA DE "ESTOY DENTRO DEL DRIVER DE LA PANTALLA" (red de seguridad, ver usb_lector.h).
  //   La pone el gancho de la pantalla y la quita el bucle en cuanto `displayRefresh()`
  //   vuelve. Si un comando se ejecutara con la marca puesta, `status.usb.dentro` lo diria.
  void marcaEnPantalla(bool dentro) { lector_.marcaEnPantalla(dentro); }

  // Contadores del bombeo, para el diagnostico (`status.usb`) y para el banco de pruebas.
  const UsbLector &lector() const { return lector_; }

  // ★ BYTES DEL BLUETOOTH AL MISMO RECEPTOR DE LINEAS (ver configProtocolEmpujaBle).
  bool empujaBle(const uint8_t *datos, size_t n) { return lector_.empujaBle(datos, n); }
  // Tira la linea del Bluetooth a medias (al desconectar el huesped).
  void olvidaLineaBle() { lector_.olvidaLineaBle(); }

 private:
  void handleLine(const char *line);
  void replyGet();
  void replyStatus();
  void replyOkWithConfig(bool persisted);
  void replyError(const char *err);
  void handleRadio(const JsonVariantConst &body);
  void handleDiag(const JsonVariantConst &body);
  void handleBeacon();
  void handleMessage(const JsonDocument &doc);
  void sendLine(const String &s);

  // Ganchos del lector (estaticos: el lector es C++ puro y no sabe de esta clase).
  static bool hookTnc(void *ctx, uint8_t b);
  static void hookLinea(void *ctx, const char *linea, size_t n, Origen origen);
  void lineaRecibida(const char *linea, size_t n, Origen origen);

  DigiConfig &cfg_;

  // De donde vino la linea que se esta atendiendo: decide por donde sale su respuesta (ver
  // protocolHostOut en la cabecera). Se apunta ANTES de despachar, porque el despacho ya
  // contesta, y se queda puesta: el diagnostico en flujo que venga detras usa la misma.
  Origen origen_ = Origen::Usb;

  // ★ AQUI VIVIA `String lineBuf_` (y su tope kMaxLine). El acumulador de linea y el anillo
  //   de bytes estan ahora en `UsbLector`, que es el MISMO objeto que bombea el driver: asi
  //   el bucle y la pantalla comparten un solo acumulador y un solo sitio donde se parten
  //   las lineas (no hay dos parsers leyendo el mismo puerto, que seria el desastre).
  UsbLector lector_;
};

// Engancha el objeto del protocolo (lo llama main.cpp una vez, al arrancar). Hace falta porque
// los bytes del Bluetooth entran por una funcion suelta y el protocolo es un objeto de main.
// Va DECLARADO AQUI ABAJO, despues de la clase: antes no existe el nombre `ConfigProtocol`.
void configProtocolBind(ConfigProtocol *p);

