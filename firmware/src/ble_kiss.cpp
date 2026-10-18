// ble_kiss.cpp — ver ble_kiss.h (que ofrece y por que estuvo apagado).
// License: GPL-3.0

#include "ble_kiss.h"

#include <bluefruit.h>

#include "flog.h"
#include "kiss.h"
#include "protocol.h"
#include "tnc.h"

namespace {

// ★ UUID del "BLE KISS TNC": NO INVENTAR. Son los que buscan APRSdroid y los TNC Mobilinkd, y
//   los mismos que usa el tracker de CA2RXU (src/ble_utils.cpp, juego "KISS").
const char *const kUuidKissSvc = "00000001-ba2a-46c9-ae49-01b0961f68bb";
const char *const kUuidKissRx = "00000002-ba2a-46c9-ae49-01b0961f68bb";  // movil -> nodo
const char *const kUuidKissTx = "00000003-ba2a-46c9-ae49-01b0961f68bb";  // nodo -> movil

// Servicio de CONFIGURACION (2026-10-18, app Android F4LEK): lineas de texto, el MISMO
// protocolo que el cable (JSON `{"cmd":...}` y CLI), con el mismo receptor de lineas
// (bleHostFeed -> UsbLector). Propio de este firmware: UUID inventados aqui, no los de
// ningun otro. Mismos permisos que el KISS: con PIN, nadie sin emparejar lo toca.
const char *const kUuidCfgSvc = "f4e40001-7e5c-4b8a-9c1d-2a4f4c454b00";
const char *const kUuidCfgRx = "f4e40002-7e5c-4b8a-9c1d-2a4f4c454b00";  // movil -> nodo
const char *const kUuidCfgTx = "f4e40003-7e5c-4b8a-9c1d-2a4f4c454b00";  // nodo -> movil

constexpr int8_t kTxPowerDbm = 4;
constexpr uint16_t kMaxCar = 244;  // carga maxima de una caracteristica (MTU 247 - 3)

DigiConfig *gCfg = nullptr;

BLEService gKissSvc(kUuidKissSvc);
BLECharacteristic gKissRx(kUuidKissRx);
BLECharacteristic gKissTx(kUuidKissTx);
BLEService gCfgSvc(kUuidCfgSvc);
BLECharacteristic gCfgRx(kUuidCfgRx);
BLECharacteristic gCfgTx(kUuidCfgTx);

bool gStackUp = false;    // Bluefruit.begin() hecho (el SoftDevice esta arriba)
bool gAnunciando = false; // el ajuste esta encendido y se anuncia / se acepta un movil
bool gPin = false;        // el enlace exige PIN (cifrado con MITM)
const char *gFallo = "";  // por que no arranco (vacio = arranco o no se pidio)
char gNombre[30] = "";
volatile uint16_t gConn = BLE_CONN_HANDLE_INVALID;

// Bytes KISS del movil. Los escribe la tarea del stack (callback) y los lee el bucle: un
// productor y un consumidor, asi que basta con un anillo con indices volatiles.
constexpr size_t kAnillo = 2048;  // potencia de 2
uint8_t gAnillo[kAnillo];
volatile size_t gCab = 0, gCola = 0;
volatile uint32_t gPerdidosIn = 0;

// Bytes de configuracion del movil: mismo esquema de anillo que el KISS.
constexpr size_t kAnilloCfg = 1024;  // potencia de 2
uint8_t gAnilloCfg[kAnilloCfg];
volatile size_t gCabCfg = 0, gColaCfg = 0;
volatile uint32_t gPerdidosCfg = 0;
volatile bool gOlvidaCfg = false;  // el movil se fue: tirar su linea a medias (en el bucle)

KissState gKiss;
uint32_t gTramasIn = 0, gTramasOut = 0, gFallosOut = 0;

// --- La ficha del SoftDevice (MBR 0x1000 + 0x2000; offsets de nrf_sdm.h) --------------------
// Antes de llamar a Bluefruit.begin() se comprueba que hay un SoftDevice grabado y que acaba
// justo donde empieza este firmware: si no, `Bluefruit.begin()` desmontaria el USB y no lo
// devolveria nunca. Asi el nodo se queda sin Bluetooth pero entero.
constexpr uint32_t kSdInfo = 0x3000;
constexpr uint32_t kSdMagic = 0x51B1E5DB;
extern "C" uint32_t __isr_vector[];  // el inicio de esta aplicacion (gcc_startup_nrf52840.S)

uint32_t sdMagic() { return *(const volatile uint32_t *)(kSdInfo + 0x04); }
uint32_t sdFin() { return *(const volatile uint32_t *)(kSdInfo + 0x08); }
uint16_t sdFwid() { return *(const volatile uint16_t *)(kSdInfo + 0x0C); }

bool softdeviceValido() {
  return sdMagic() == kSdMagic && sdFin() == (uint32_t)__isr_vector;
}

// --- Callbacks del stack (tarea del stack, NO el bucle: aqui no se toca radio ni flash) ----
void alEscribirKiss(uint16_t, BLECharacteristic *, uint8_t *datos, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    const size_t sig = (gCab + 1) & (kAnillo - 1);
    if (sig == gCola) {
      gPerdidosIn += n - i;
      return;
    }
    gAnillo[gCab] = datos[i];
    gCab = sig;
  }
}

void alEscribirCfg(uint16_t, BLECharacteristic *, uint8_t *datos, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    const size_t sig = (gCabCfg + 1) & (kAnilloCfg - 1);
    if (sig == gColaCfg) {
      gPerdidosCfg += n - i;
      return;
    }
    gAnilloCfg[gCabCfg] = datos[i];
    gCabCfg = sig;
  }
}

void alConectar(uint16_t conn) { gConn = conn; }

void alDesconectar(uint16_t, uint8_t) {
  gConn = BLE_CONN_HANDLE_INVALID;
  gOlvidaCfg = true;
}

// Respuestas del protocolo a una orden llegada por el servicio de configuracion. Trozos de
// como mucho kMaxCar (notify() corta en silencio lo que pase de su maxLen). La espera la pone
// notify() (un hueco de la cola del SoftDevice, BLE_GENERIC_TIMEOUT): finita.
size_t salidaParaProtocolo(const uint8_t *datos, size_t n, uint32_t) {
  if (datos == nullptr || n == 0) return 0;  // vaciado antes de reiniciar: notify() ya espera
  if (!gStackUp || gConn == BLE_CONN_HANDLE_INVALID || !gCfgTx.notifyEnabled(gConn)) return 0;
  BLEConnection *c = Bluefruit.Connection(gConn);
  if (c == nullptr) return 0;
  const uint16_t mtu = c->getMtu() - 3;
  const uint16_t trozo = mtu < kMaxCar ? mtu : kMaxCar;
  size_t off = 0;
  while (off < n) {
    const uint16_t k = (uint16_t)((n - off) < trozo ? (n - off) : trozo);
    if (!gCfgTx.notify(gConn, datos + off, k)) break;
    off += k;
  }
  return off;
}

// --- Lo que manda el movil ---------------------------------------------------------------
void tramaDelMovil(const uint8_t *frame, size_t len) {
  gTramasIn++;
  tncSendHostFrame(frame, len);  // mismas reglas de TX (mute, CAD) que el cable
}

void estadoParaProtocolo(JsonObject &d) {
  d["on"] = gAnunciando;
  d["up"] = gStackUp;
  d["conn"] = gConn != BLE_CONN_HANDLE_INVALID;
  d["name"] = gNombre;
  d["pin"] = gPin;
  d["err"] = gFallo;
}

bool pinValido(const char *p) {
  if (p == nullptr || strlen(p) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
  }
  return true;
}

void anunciar(bool si) {
  if (si) {
    Bluefruit.Advertising.restartOnDisconnect(true);
    if (!Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.start(0);
  } else {
    Bluefruit.Advertising.restartOnDisconnect(false);
    if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
    if (gConn != BLE_CONN_HANDLE_INVALID) Bluefruit.disconnect(gConn);
  }
  gAnunciando = si;
}

bool arrancaStack() {
  if (!softdeviceValido()) {
    gFallo = "SoftDevice ausente o de otra version";
    flogLine("BLE no arranca: SoftDevice magic 0x%08lX fin 0x%05lX (app 0x%05lX) fwid 0x%04X",
             (unsigned long)sdMagic(), (unsigned long)sdFin(),
             (unsigned long)(uint32_t)__isr_vector, (unsigned)sdFwid());
    return false;
  }

  Bluefruit.autoConnLed(false);
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);  // MTU 247: una trama APRS en 1-2 avisos
  if (!Bluefruit.begin()) {
    // sd_ble_enable() sin RAM suficiente: la RAM de la app empieza demasiado abajo (.ld).
    gFallo = "Bluefruit.begin() fallo";
    flogLine("BLE no arranca: Bluefruit.begin() fallo");
    return false;
  }
  gStackUp = true;

  snprintf(gNombre, sizeof(gNombre), "T-Echo %s", gCfg->callsign);
  Bluefruit.setTxPower(kTxPowerDbm);
  Bluefruit.setName(gNombre);
  Bluefruit.Periph.setConnectCallback(alConectar);
  Bluefruit.Periph.setDisconnectCallback(alDesconectar);

  // Con un PIN de 6 cifras el movil lo pide al emparejar y nadie sin emparejar puede leer ni
  // escribir. Sin PIN valido el enlace queda abierto.
  gPin = pinValido(gCfg->blePin) && Bluefruit.Security.setPIN(gCfg->blePin);
  const SecureMode_t modo = gPin ? SECMODE_ENC_WITH_MITM : SECMODE_OPEN;

  gKissSvc.begin();
  gKissTx.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
  gKissTx.setPermission(modo, SECMODE_NO_ACCESS);
  gKissTx.setMaxLen(kMaxCar);
  gKissTx.begin();
  gKissRx.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  gKissRx.setPermission(SECMODE_NO_ACCESS, modo);
  gKissRx.setMaxLen(kMaxCar);
  gKissRx.setWriteCallback(alEscribirKiss, false);  // directo: solo copia en el anillo
  gKissRx.begin();

  gCfgSvc.begin();
  gCfgTx.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
  gCfgTx.setPermission(modo, SECMODE_NO_ACCESS);
  gCfgTx.setMaxLen(kMaxCar);
  gCfgTx.begin();
  gCfgRx.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  gCfgRx.setPermission(SECMODE_NO_ACCESS, modo);
  gCfgRx.setMaxLen(kMaxCar);
  gCfgRx.setWriteCallback(alEscribirCfg, false);
  gCfgRx.begin();

  // Anuncio: flags + servicio KISS (128 bits). El nombre va en la respuesta al escaneo.
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(gKissSvc);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.setInterval(32, 244);  // 20 ms rapido / 152,5 ms despacio
  Bluefruit.Advertising.setFastTimeout(30);

  protocolEstadoBle(estadoParaProtocolo);
  protocolSalidaBle(salidaParaProtocolo, true);
  flogLine("BLE arriba: \"%s\" fwid 0x%04X pin=%s", gNombre, (unsigned)sdFwid(),
           gPin ? "si" : "no");
  return true;
}

}  // namespace

void bleBindConfig(DigiConfig *cfg) { gCfg = cfg; }

void bleLinkInit() {
  kissStateInit(&gKiss);
  kissStateSetHandler(&gKiss, tramaDelMovil);
  if (gCfg == nullptr || !gCfg->bleEnabled) return;
  if (arrancaStack()) anunciar(true);
}

void bleLoop() {
  if (gCfg == nullptr) return;

  // El ajuste se puede cambiar en marcha (menu o configurador). El SoftDevice se arranca una
  // sola vez: apagarlo y volverlo a arrancar no lo soporta Bluefruit, asi que "apagar" es
  // dejar de anunciar y echar al movil.
  if (gCfg->bleEnabled != gAnunciando) {
    if (gCfg->bleEnabled && !gStackUp && gFallo[0] == '\0') arrancaStack();
    if (gStackUp) anunciar(gCfg->bleEnabled);
  }
  if (!gStackUp) return;

  const uint32_t ahora = millis();
  while (gCola != gCab) {
    const uint8_t b = gAnillo[gCola];
    gCola = (gCola + 1) & (kAnillo - 1);
    kissStateFeed(&gKiss, b, ahora);
  }
  kissStatePoll(&gKiss, ahora);

  // Configuracion: al MISMO receptor de lineas que el cable. Lo ejecuta el bucle
  // (ConfigProtocol::atiende), nunca la tarea del stack.
  if (gOlvidaCfg) {
    gOlvidaCfg = false;
    gColaCfg = gCabCfg;
    configProtocolOlvidaLineaBle();
  }
  uint8_t trozo[64];
  size_t n = 0;
  while (gColaCfg != gCabCfg) {
    trozo[n++] = gAnilloCfg[gColaCfg];
    gColaCfg = (gColaCfg + 1) & (kAnilloCfg - 1);
    if (n == sizeof(trozo)) {
      bleHostFeed(trozo, n);
      n = 0;
    }
  }
  if (n > 0) bleHostFeed(trozo, n);
}

void bleOutputFrameText(const char *textFrame) {
  if (!gStackUp || gConn == BLE_CONN_HANDLE_INVALID || textFrame == nullptr) return;
  if (!gKissTx.notifyEnabled(gConn)) return;

  uint8_t out[(2 * (AX25_MAX_INFO + 80)) + 4];
  const size_t len = tncBuildKissFrame(textFrame, out, sizeof(out));
  if (len == 0) return;

  BLEConnection *c = Bluefruit.Connection(gConn);
  if (c == nullptr) return;
  const uint16_t mtu = c->getMtu() - 3;
  const uint16_t trozo = mtu < kMaxCar ? mtu : kMaxCar;
  for (size_t off = 0; off < len; off += trozo) {
    const uint16_t n = (uint16_t)((len - off) < trozo ? (len - off) : trozo);
    if (!gKissTx.notify(gConn, out + off, n)) {
      gFallosOut++;
      return;
    }
  }
  gTramasOut++;
}

void bleShutdown() {
  if (!gStackUp) return;
  anunciar(false);
  delay(100);  // que el movil se entere de la desconexion
  sd_softdevice_disable();
  gStackUp = false;
}

bool bleSoftDeviceIsp() { return gStackUp; }

String bleResumen() {
  char s[240];
  snprintf(s, sizeof(s),
           "BLE %s | stack %s | %s | nombre \"%s\" | pin %s | fwid 0x%04X | "
           "KISS in %lu out %lu fallos %lu perdidos %lu cfg-perdidos %lu%s%s",
           gCfg && gCfg->bleEnabled ? "ON" : "OFF", gStackUp ? "arriba" : "abajo",
           gConn != BLE_CONN_HANDLE_INVALID ? "movil conectado" : "sin movil", gNombre,
           gPin ? "si" : "no", (unsigned)sdFwid(), (unsigned long)gTramasIn,
           (unsigned long)gTramasOut, (unsigned long)gFallosOut,
           (unsigned long)gPerdidosIn, (unsigned long)gPerdidosCfg, gFallo[0] ? " | " : "", gFallo);
  return String(s);
}
