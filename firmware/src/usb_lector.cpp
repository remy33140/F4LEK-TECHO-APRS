// usb_lector.cpp — buzon de bytes del puerto USB + troceado en lineas.
// Ver usb_lector.h (el porque y el contrato completo). License: GPL-3.0

#include "usb_lector.h"

void UsbLector::init(UsbByteFn aTnc, UsbLineaFn aLinea, void *ctx) {
  aTnc_ = aTnc;
  aLinea_ = aLinea;
  ctx_ = ctx;
  cab_ = cola_ = n_ = 0;
  nUsb_ = 0;
  nBle_ = 0;
  lineaUsb_[0] = '\0';
  lineaBle_[0] = '\0';
  enPantalla_ = false;
  // Los contadores tambien empiezan de cero (el nodo enciende con ellos a 0, y el banco de
  // pruebas llama a init() entre escenario y escenario para que cada numero sea del suyo).
  leidos_ = 0;
  bombeadosEspera_ = 0;
  lineas_ = 0;
  tirados_ = 0;
  ejecutadasEnBombeo_ = 0;
  perdidosBle_ = 0;
  maxPend_ = 0;
}

/* ★★ LEE Y ENCOLA (lo llama el driver de la tinta desde sus esperas) ★★
   Lo unico que hace es sacar bytes del puerto y dejarlos en el anillo, EN ORDEN. Ni
   interpreta, ni parte lineas, ni ejecuta: eso es `atiende()`, y `atiende()` solo lo llama
   el bucle. Es lo que hace que leer dentro del repintado no pueda provocar reentrada.

   SI EL ANILLO SE LLENA, SE DEJA DE LEER: los bytes se quedan en el FIFO del CDC (256 bytes)
   y el USB los retiene (NAK). No se pierde ni uno; simplemente esperan a que el bucle vacie
   el anillo. Preferimos eso a tirar bytes del operador. */
size_t UsbLector::bombea(const UsbPuerto &p, bool enEsperaDePantalla) {
  if (p.disponible == nullptr || p.lee == nullptr) return 0;

  size_t leidosAhora = 0;
  while (n_ < kCap && p.disponible(p.ctx) > 0) {
    const int b = p.lee(p.ctx);
    if (b < 0) break;  // se acabo lo que habia: no se espera aqui
    anillo_[cola_] = (uint8_t)b;
    origen_[cola_] = (uint8_t)Origen::Usb;
    cola_ = (cola_ + 1) % kCap;
    n_++;
    leidos_++;
    leidosAhora++;
    if (enEsperaDePantalla) bombeadosEspera_++;
    if (n_ > maxPend_) maxPend_ = n_;
  }
  return leidosAhora;
}

/* ★★ BYTES DEL BLUETOOTH (2026-09-17) ★★
   Mismo anillo y misma cola que el USB, marcados con su origen. El llamante es el callback
   de escritura del Nordic UART Service, que corre en la tarea del SoftDevice: se copian los
   bytes y se vuelve, sin ejecutar nada.

   SI NO CABEN SE PIERDEN, y se cuentan: el anillo es el tope de una linea (4096) y el
   Bluetooth no puede retener como el USB (aqui no hay NAK detras). El que llama
   (`bleLinkRead`) lo mira con `perdidosBle()` y lo deja escrito en el registro, porque una
   linea cortada por la mitad se ve luego como un "bad json" sin explicacion. */
bool UsbLector::empujaBle(const uint8_t *datos, size_t n) {
  if (datos == nullptr || n == 0) return true;
  if (n_ + n > kCap) {
    perdidosBle_ += (uint32_t)n;
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    anillo_[cola_] = datos[i];
    origen_[cola_] = (uint8_t)Origen::Ble;
    cola_ = (cola_ + 1) % kCap;
    n_++;
  }
  if (n_ > maxPend_) maxPend_ = n_;
  return true;
}

void UsbLector::olvidaLineaBle() {
  nBle_ = 0;
  lineaBle_[0] = '\0';
}

/* ★★ EJECUTA LO ENCOLADO (solo desde el bucle principal) ★★
   Saca los bytes del anillo en el mismo orden en que llegaron y se los da a `meteByte()`,
   que es el unico sitio que los interpreta. Aqui es donde una linea completa puede acabar
   ejecutando un comando: por eso esto NO se puede llamar desde el driver de la pantalla. */
size_t UsbLector::atiende() {
  // Red de seguridad, no un camino normal: si la marca del gancho de la pantalla sigue
  // puesta, es que se esta ejecutando DENTRO del repintado. Tiene que quedarse en 0
  // SIEMPRE (sale en `status.usb.dentro` y en el comando `usb`).
  if (enPantalla_) ejecutadasEnBombeo_++;

  size_t atendidos = 0;
  while (n_ > 0) {
    const uint8_t b = anillo_[cab_];
    const Origen origen = (Origen)origen_[cab_];
    cab_ = (cab_ + 1) % kCap;
    n_--;
    meteByte(b, origen, aTnc_);
    atendidos++;
  }
  return atendidos;
}

/* EL UNICO SITIO QUE INTERPRETA BYTES. Orden EXACTO del feed() de antes (protocol.cpp):
     1) el byte se le ofrece al TNC (KISS) ANTES que nada: si lo coge, la linea de texto a
        medias muere con la trama binaria (`nUsb_ = 0`, igual que el `lineBuf_ = ""` de
        antes). ESA es la regla del 0xC0, y sigue en el mismo sitio y en el mismo orden.
     2) el CR se ignora (tolerancia CRLF),
     3) el LF cierra la linea y la entrega al despacho,
     4) lo demas se acumula, con el mismo tope de 4096 caracteres por linea.

   ★ SOLO EL USB TIENE TRAMA BINARIA. Al Bluetooth no se le ofrece el gancho del TNC
     (`aTnc == nullptr` en su llamada): el TNC es del cable, y esto es lo que hace que
     encender el Bluetooth no pueda silenciar las balizas ni depender del selector
     `tncProtocol`. Sus bytes van siempre a linea. */
void UsbLector::meteByte(uint8_t b, Origen origen, UsbByteFn aTnc) {
  if (aTnc != nullptr && aTnc(ctx_, b)) {
    nUsb_ = 0;  // una linea a medias muere con la trama binaria
    return;
  }

  // El acumulador que toca, segun por donde haya entrado el byte (ver usb_lector.h), y su
  // tope: el del cable es largo y el del Bluetooth corto (kMaxLineaBle, por RAM).
  const bool esBle = (origen == Origen::Ble);
  char *linea = esBle ? lineaBle_ : lineaUsb_;
  size_t &nLinea = esBle ? nBle_ : nUsb_;
  const size_t tope = esBle ? kMaxLineaBle : kMaxLinea;

  const char c = (char)b;
  if (c == '\r') return;
  if (c == '\n') {
    if (nLinea > 0) {
      linea[nLinea] = '\0';
      lineas_++;
      if (aLinea_ != nullptr) aLinea_(ctx_, linea, nLinea, origen);
    }
    nLinea = 0;
    return;
  }
  if (nLinea < tope) {
    linea[nLinea++] = c;
  } else {
    tirados_++;  // sobra de una linea larguisima: se tira, como antes
  }
}
