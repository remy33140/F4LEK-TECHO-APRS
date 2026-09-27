// usb_lector.h — LECTURA DEL PUERTO USB: buzon de bytes + troceado en lineas.
//
// ★★ T-ECHO PROJECT BUTTER II (2026-09-16) ★★
//
// POR QUE EXISTE ESTE FICHERO:
// El parser del USB se alimentaba SOLO desde el bucle (protocol.cpp, feed()), y el bucle se
// pasa 1,5-3 s dentro del driver de la tinta esperando al panel. En ese rato el puerto no lo
// leia nadie, y el FIFO del CDC son 256 bytes (`CFG_TUD_CDC_RX_BUFSIZE`, ver
// libraries/Adafruit_TinyUSB_Arduino/src/arduino/ports/nrf/tusb_config_nrf.h). Consecuencia
// medida en el modelo del banco (tools/banco_usb): una linea larga —el `set` del
// configurador son ~1.4 KB— NO entra en una vuelta del bucle; hace falta ir vaciando el FIFO
// vuelta a vuelta, y cada vuelta puede levar otro repintado de 1,5-2,5 s por delante.
//
// LO QUE HACE, Y LO QUE NO (esto es lo importante):
//   * `bombea()`  -> LEE del puerto y ENCOLA los bytes en un anillo. **NO ejecuta NADA.**
//                    Es lo unico que llama el driver de la tinta desde sus esperas (el
//                    gancho de la pantalla, ver displaySetPumpBoton).
//   * `atiende()` -> SACA bytes del anillo y los mete por el UNICO sitio que los interpreta
//                    (`meteByte()`: la regla del 0xC0 del KISS y el troceado en lineas). Solo
//                    lo llama el BUCLE PRINCIPAL. La ejecucion de un comando nunca ocurre
//                    dentro del driver: no se puede pintar dentro de un pintado ni escribir
//                    la flash en medio de una transaccion con el panel.
//
// ES EL MISMO PATRON QUE EL DEL BOTON (Project Butter, 2026-09-15): buzon + cola + ejecucion
// en el bucle. Alli el buzon guarda FLANCOS con marca de tiempo; aqui, BYTES del puerto.
//
// ★★ DOS TRANSPORTES, UN SOLO PARSER (2026-09-17, Bluetooth) ★★
//
// El Bluetooth (ble_kiss.cpp) tenia su propio receptor de lineas y por eso NO podia hablar el
// protocolo de la app (JSON/CLI): solo entendia KISS binario, y ademas KISS solo si el
// selector `tncProtocol` estaba en KISS, asi que con el selector apagado (el caso normal) la
// app no podia mandar NADA. La solucion NO es un segundo parser: es que los bytes del
// Bluetooth entren por **este mismo** `meteByte()`, que es el unico sitio del firmware donde
// se decide que es KISS, que es texto y donde acaba una linea.
//
// Para eso hay dos cosas nuevas, y solo dos:
//   1) cada byte lleva su ORIGEN (`Origen::Usb` / `Origen::Ble`), para que la respuesta se
//      mande por donde vino el comando (una respuesta por linea, la regla de siempre);
//   2) hay UN ACUMULADOR DE LINEA POR TRANSPORTE. Es obligatorio que sean dos: si el USB
//      dejara una linea a medias (`set` del configurador, ~1.4 KB) y el Bluetooth escribiera
//      en el mismo acumulador, las dos lineas se mezclarian y saldrian dos comandos rotos.
//      El TROCEADO sigue siendo el mismo codigo para los dos.
//
// LO UNICO QUE NO ES IGUAL EN LOS DOS SENTIDOS: el KISS. Solo los bytes del USB pueden
// pertenecer a una trama KISS (el TNC es una sola cosa y su sitio es el cable; ver
// tncHandleUsbByte). Los bytes del Bluetooth van SIEMPRE a linea, asi que **el Bluetooth no
// depende del selector del TNC** y encender el Bluetooth no puede silenciar las balizas.
//
// ES C++ PURO (ni Arduino ni String): el mismo fichero se compila en el firmware y en el
// banco de pruebas del ordenador (tools/banco_usb), asi que lo que se mide alli es ESTE
// codigo, no una copia reescrita para la prueba.
//
// License: GPL-3.0

#pragma once

#include <stddef.h>
#include <stdint.h>

// De donde vienen los bytes: decide a quien va la respuesta. Dos transportes y nada mas.
enum class Origen : uint8_t {
  Usb = 0,  // el cable (USB CDC): protocolo de lineas + KISS del TNC
  Ble = 1,  // Bluetooth LE (Nordic UART Service): protocolo de lineas, nunca KISS
};

// El puerto, visto por el lector: dos funciones y un contexto. En el firmware es el
// `Serial` del core (USB CDC) a traves de dos thunks de protocol.cpp; en el banco, el
// puerto simulado.
struct UsbPuerto {
  void *ctx;
  int (*disponible)(void *ctx);  // bytes que hay ahora mismo (0 = nada)
  int (*lee)(void *ctx);         // siguiente byte (0..255), o -1 si no hay
};

// Gancho de bytes del TNC: es `tncHandleUsbByte()`. Devuelve true cuando el byte pertenece a
// una trama KISS (entonces NO entra en la linea de texto). Es la regla del 0xC0: en cuanto el
// TNC esta en KISS, el binario se cuenta y no se interpreta. Se le pasa al lector en cada
// llamada y SOLO para los bytes del USB (los del Bluetooth van siempre a linea).
typedef bool (*UsbByteFn)(void *ctx, uint8_t b);

// Gancho de linea completa: el despacho JSON/CLI de protocol.cpp. La cadena va terminada en
// cero y `n` es su longitud (sin el CR/LF). Lleva el ORIGEN porque el mismo despacho atiende a
// los dos transportes y la respuesta tiene que salir por donde entro el comando.
typedef void (*UsbLineaFn)(void *ctx, const char *linea, size_t n, Origen origen);

class UsbLector {
 public:
  // Anillo de bytes. 4096 = el tope de una linea (kMaxLinea): asi, durante un repintado
  // entero, cabe holgadamente el `set` mas largo que puede mandar el configurador.
  static constexpr size_t kCap = 4096;

  // Tope de una linea de texto DEL CABLE. Es el `kMaxLine` que vivia en protocol.h, con su
  // historia:
  // ★ 1024 -> 4096 (2026-09-16): el configurador manda la configuracion ENTERA en una sola
  //   linea JSON, y con los cuatro perfiles de uso se pasa de 1024 caracteres. Al pasarse,
  //   los caracteres sobrantes se tiraban en silencio y el nodo contestaba "bad json" al
  //   intentar leer un JSON cortado. Medido: 55 campos, ~1320 caracteres. El margen de 4096
  //   cubre cualquier configuracion con holgura.
  static constexpr size_t kMaxLinea = 4096;

  // Tope de una linea DEL BLUETOOTH. Es mas corto A PROPOSITO y no es un descuido:
  //   - 1024 B cubren de sobra el `set` completo de la configuracion (~1.4 KB no cabe, pero el
  //     configurador manda por USB, y por Bluetooth ese comando se parte en varios trozos);
  //   - y sobre todo: el nodo tiene 243 KB de RAM y 144 KB utiles (el SoftDevice se queda el
  //     resto). Dos acumuladores de 4096 serian 8 KB solo en eso. Con 1024 se queda en 5 KB.
  // Si algun dia la app manda lineas mas largas por el aire, este es el numero que hay que
  // subir, y se vera: la linea se corta y el JSON falla con "bad json".
  static constexpr size_t kMaxLineaBle = 1024;

  // Engancha el despacho del protocolo (se llama una vez, al construir el protocolo).
  // El mismo par de ganchos atiende a los DOS transportes: `linea` recibe el origen de cada
  // linea, y el gancho del TNC solo se llama para los bytes del USB.
  void init(UsbByteFn aTnc, UsbLineaFn aLinea, void *ctx);

  // ★ LEE Y ENCOLA. Nunca ejecuta. `enEsperaDePantalla` solo marca el CONTADOR de
  //   diagnostico (bytes leidos dentro de las esperas del driver de la tinta); no cambia
  //   nada de lo que se hace.
  size_t bombea(const UsbPuerto &p, bool enEsperaDePantalla = false);

  // ★ EJECUTA lo encolado (solo desde el bucle principal). Devuelve los bytes atendidos.
  size_t atiende();

  // ★★ BYTES QUE NO VIENEN DEL PUERTO (Bluetooth, 2026-09-17) ★★
  //   Los mete el callback de escritura del Nordic UART Service. Entran al MISMO anillo y
  //   los trocea el MISMO `meteByte()`, pero marcados con su origen para que la respuesta
  //   salga por el Bluetooth. Devuelve false si el anillo esta lleno (el llamante lo cuenta
  //   como perdida: preferimos decir que se perdio a corromper una linea).
  //   NO ejecuta nada: quien ejecuta es `atiende()`, en el bucle.
  bool empujaBle(const uint8_t *datos, size_t n);

  // Tira la linea del Bluetooth a medias. Se llama al desconectar: si una escritura se corto
  // (el movil se alejo a mitad de un JSON), sus restos no pueden quedarse ahi para
  // envenenar la primera linea de la conexion siguiente.
  void olvidaLineaBle();

  // Bytes que se quedaron fuera por anillo lleno (diagnostico).
  uint32_t perdidosBle() const { return perdidosBle_; }

  /* ★★ RED DE SEGURIDAD DE LA REENTRADA (no es un camino normal) ★★
     El gancho de la pantalla pone la marca mientras el driver esta dentro de sus esperas y
     el BUCLE la quita en cuanto `displayRefresh()` ha vuelto (ver main.cpp). Si alguien
     ejecuta un comando con la marca puesta, es que esta ejecutando DENTRO del pintado:
     `ejecutadasEnBombeo()` sube y se ve en `status.usb.dentro` / comando `usb`.
     Tiene que ser 0 SIEMPRE. */
  void marcaEnPantalla(bool dentro) { enPantalla_ = dentro; }

  // --- Contadores de diagnostico (los publica `status.usb`) --------------------
  uint32_t leidos() const { return leidos_; }              // bytes sacados del puerto
  uint32_t bombeadosEnEspera() const { return bombeadosEspera_; }  // ... dentro de un repintado
  uint32_t lineas() const { return lineas_; }              // lineas entregadas al despacho
  uint32_t tirados() const { return tirados_; }            // sobra de una linea > kMaxLinea
  size_t pendientes() const { return n_; }                 // bytes esperando en el anillo
  size_t maxPendientes() const { return maxPend_; }        // maximo historico (cuanto se encolo)
  uint32_t ejecutadasEnBombeo() const { return ejecutadasEnBombeo_; }  // red de seguridad: 0

 private:
  // EL UNICO sitio que interpreta bytes. `aTnc` es el gancho del KISS y llega por parametro
  // porque SOLO se le ofrece el byte cuando viene del USB (ver la cabecera).
  void meteByte(uint8_t b, Origen origen, UsbByteFn aTnc);

  uint8_t anillo_[kCap];
  size_t cab_ = 0;   // de donde se saca
  size_t cola_ = 0;  // donde se mete
  size_t n_ = 0;     // bytes dentro
  uint8_t origen_[kCap];  // origen de cada byte del anillo (paralelo a anillo_)

  // Un acumulador de linea POR TRANSPORTE (ver la cabecera: mezclarlos rompe las lineas).
  // Cada uno con su tope: el del cable es largo (la configuracion entera llega en una linea),
  // el del Bluetooth es corto (ver kMaxLineaBle y el porque del ahorro de RAM).
  char lineaUsb_[kMaxLinea + 1];
  size_t nUsb_ = 0;
  char lineaBle_[kMaxLineaBle + 1];
  size_t nBle_ = 0;

  UsbByteFn aTnc_ = nullptr;
  UsbLineaFn aLinea_ = nullptr;
  void *ctx_ = nullptr;

  bool enPantalla_ = false;        // marca del gancho de la pantalla (ver arriba)
  uint32_t leidos_ = 0;
  uint32_t bombeadosEspera_ = 0;
  uint32_t lineas_ = 0;
  uint32_t tirados_ = 0;
  uint32_t ejecutadasEnBombeo_ = 0;
  uint32_t perdidosBle_ = 0;
  size_t maxPend_ = 0;
};
