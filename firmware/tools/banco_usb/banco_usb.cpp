/* banco_usb.cpp — BANCO DE PRUEBAS DEL USB DURANTE EL REPINTADO (T-Echo Project Butter II).
 *
 * QUE MIDE, SIN PLACA: reproduce milisegundo a milisegundo lo que hace el firmware cuando
 * un comando llega por el USB MIENTRAS LA PANTALLA DE TINTA ESTA PINTANDO, y dice CUANDO se
 * atiende, CUANTOS bytes se han leido durante el repintado y CUANTO ha tenido que esperar
 * el programa del ordenador para soltar su linea.
 *
 * COMO ESTA HECHO (y por que se puede creer):
 *   - Lo que se compila es el CODIGO DE VERDAD del firmware:
 *       * `../../src/usb_lector.cpp` -> el buzon de bytes y el troceado en lineas (nuevo).
 *       * `../../src/kiss.cpp`       -> la maquina de estados KISS (la regla del 0xC0).
 *     Los dos son C++ puro, asi que se compilan tal cual en el ordenador: lo que se mide
 *     aqui es EL MISMO FICHERO que corre en el nodo, no una copia reescrita.
 *   - La version ANTERIOR (`lector_antes.cpp`) es una TRANSCRIPCION del `feed()` que habia
 *     en `protocol.cpp` antes de este cambio (sacado de git), con el mismo contrato. Igual
 *     que `banco_boton` hizo con `button_antes.cpp`.
 *   - La version INGENUA (`-DBANCO_INGENUO`) es el CONTRA-EJEMPLO: el mismo codigo nuevo,
 *     pero EJECUTANDO desde el gancho del driver. Sirve para comprobar que el banco DETECTA
 *     la reentrada: si no la detectara, el "0" de la version buena no valdria nada.
 *
 * QUE MODELO DEL FIRMWARE HAY ALREDEDOR (los unicos numeros "de fuera" de la prueba):
 *   - FIFO del USB (CDC): 256 bytes. ES UN DATO REAL, no una suposicion: viene de
 *     `CFG_TUD_CDC_RX_BUFSIZE` en libraries/Adafruit_TinyUSB_Arduino/src/arduino/ports/
 *     nrf/tusb_config_nrf.h (framework-arduinoadafruitnrf52 del core que usa el proyecto).
 *   - El host entrega UN PAQUETE DE 64 BYTES POR MILISEGUNDO (trama USB de full-speed) y,
 *     si el FIFO esta lleno, ESPERA: asi funciona el USB, el endpoint contesta NAK y el
 *     ordenador reintenta. Eso es lo que se mide como "host bloqueado".
 *   - El bucle, igual que main.cpp: cabecera (`feed()` = leer + ejecutar), cuerpo (radio,
 *     sensores, botones: 20 ms), `displayRefresh()` (pinta y, dentro, el driver BOMBEA cada
 *     2 ms), cobro de lo encolado y cola de la vuelta (`handleButton()` + `delay(5)`).
 *   - Un repintado PARCIAL son 1.500 ms y un COMPLETO 2.500 ms: son los tiempos que declara
 *     el propio driver (epdEsperaPintado(350,1500) y (2000,4000), mas el encendido de
 *     tensiones y el trasvase del framebuffer).
 *
 * COMO COMPILAR Y EJECUTAR: `ejecuta_banco_usb.ps1` en esta misma carpeta (necesita un
 * compilador de C++ del ordenador: `zig c++`, `g++` o `clang++`).
 *
 * License: GPL-3.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kiss.h"

// ===========================================================================
//  RELOJ DE MENTIRA (el codigo real no llama a millis(): recibe la hora)
// ===========================================================================
static uint32_t gMs = 0;

// ===========================================================================
//  REGISTRO DE LO QUE PASA
// ===========================================================================
struct Linea {
  uint32_t ms;
  size_t n;
  bool entera;        // ¿es EXACTAMENTE el comando que se mando, sin cortes?
  char texto[512];    // copia recortada, solo para poder mirarla
};
static Linea gLineas[64];
static uint32_t gEjecuciones = 0;
static uint32_t gEjecucionesEnPintado = 0;
static uint32_t gLineasBasura = 0;   // lineas que NO eran el comando (binario mal interpretado)
static const char *gLineaEsperada = "";
static size_t gLineaEsperadaLen = 0;

// ===========================================================================
//  MODELO DEL PUERTO USB (FIFO del CDC + el host que escribe)
// ===========================================================================
static const size_t FIFO_CAP = 256;   // CFG_TUD_CDC_RX_BUFSIZE (dato real del core)
static const size_t EP_BYTES = 64;    // paquete del endpoint (full-speed)
static const uint32_t BUCLE_CUERPO_MS = 20;
static const uint32_t COLA_VUELTA_MS = 5;
static const uint32_t BOMBEO_MS = 2;

static uint8_t gFifo[FIFO_CAP];
static size_t gFifoN = 0;

static const uint8_t *gHostDatos = nullptr;
static size_t gHostTotal = 0, gHostRestante = 0;
static uint32_t gHostUltimoMs = 0xFFFFFFFFu;

static uint32_t gMsHostBloqueado = 0;
static uint32_t gMsHostTermino = 0;
static uint32_t gBytesLeidos = 0;
static uint32_t gBytesLeidosEnPintado = 0;
static bool gPintando = false;

// OJO: estas cuatro funciones NO son `static` a proposito: las usa tambien
// lector_antes.cpp (la version anterior), para que las dos versiones jueguen con el MISMO
// puerto y los MISMOS ganchos.
int bancoDisponible(void *) { return (int)gFifoN; }
int bancoLee(void *) {
  if (gFifoN == 0) return -1;
  const uint8_t b = gFifo[0];
  memmove(gFifo, gFifo + 1, --gFifoN);
  gBytesLeidos++;
  if (gPintando) gBytesLeidosEnPintado++;
  return b;
}

static void hostEmpieza(const void *datos, size_t n) {
  gHostDatos = (const uint8_t *)datos;
  gHostTotal = n;
  gHostRestante = n;
  gHostUltimoMs = 0xFFFFFFFFu;
}

static void hostTick() {
  if (gHostRestante == 0 || gHostDatos == nullptr) return;
  if (gHostUltimoMs == gMs) return;   // un paquete por milisegundo (una trama USB)
  gHostUltimoMs = gMs;
  if (gFifoN >= FIFO_CAP) {           // FIFO lleno: el USB retiene al host (NAK)
    gMsHostBloqueado++;
    return;
  }
  size_t n = FIFO_CAP - gFifoN;
  if (n > EP_BYTES) n = EP_BYTES;
  if (n > gHostRestante) n = gHostRestante;
  memcpy(gFifo + gFifoN, gHostDatos + (gHostTotal - gHostRestante), n);
  gFifoN += n;
  gHostRestante -= n;
  if (gHostRestante == 0) gMsHostTermino = gMs;
}

// ===========================================================================
//  LOS GANCHOS DEL PROTOCOLO (lo que en el firmware es protocol.cpp / tnc.cpp)
// ===========================================================================
static bool gKissActivo = false;
static uint32_t gKissFrames = 0;
static uint32_t gKissMsUltimo = 0;
static uint32_t gKissEnPintado = 0;   // tramas entregadas DENTRO del repintado (tiene que ser 0)
static uint8_t gKissUltimo[KISS_MAX_FRAME];
static size_t gKissUltimoLen = 0;

// Es `tncHandleUsbByte()` de tnc.cpp: con el TNC en KISS, el byte va a la maquina de
// estados KISS y NO se interpreta como texto. Tres lineas, calcadas del firmware.
bool bancoByteAlTnc(void *, uint8_t b) {
  if (!gKissActivo) return false;
  return kissFeed(b, gMs);
}
// Es la llegada de una linea completa (protocol.cpp: tncHandleLine + handleLine). Lo que
// importa aqui es CUANDO llega y SI LLEGA ENTERA.
void bancoLinea(void *, const char *s, size_t n) {
  if (gPintando) gEjecucionesEnPintado++;
  const bool esElComando = (n == gLineaEsperadaLen &&
                            n > 0 && memcmp(s, gLineaEsperada, n) == 0);
  if (gEjecuciones < 64) {
    Linea &l = gLineas[gEjecuciones];
    l.ms = gMs;
    l.n = n;
    l.entera = esElComando;
    const size_t c = (n < sizeof(l.texto) - 1) ? n : sizeof(l.texto) - 1;
    memcpy(l.texto, s, c);
    l.texto[c] = '\0';
  }
  if (!esElComando) gLineasBasura++;   // texto que NO era el comando: algo se malinterpreto
  gEjecuciones++;
}

// Es el manejador de trama KISS de tnc.cpp (aqui solo se apunta: el banco no transmite).
// OJO: en el firmware esto acaba transmitiendo por la radio, asi que entregarlo DENTRO del
// repintado seria tan malo como ejecutar un comando ahi: por eso se cuenta aparte.
static void bancoFrameKiss(const uint8_t *frame, size_t len) {
  gKissFrames++;
  gKissMsUltimo = gMs;
  if (gPintando) gKissEnPintado++;
  gKissUltimoLen = (len <= KISS_MAX_FRAME) ? len : KISS_MAX_FRAME;
  memcpy(gKissUltimo, frame, gKissUltimoLen);
}

// ===========================================================================
//  EL TRANSPORTE: el codigo nuevo (REAL) o el anterior (transcrito de git)
// ===========================================================================
#ifdef BANCO_NUEVO
#include "usb_lector.h"
static UsbLector gLector;
static UsbPuerto gPuerto = {nullptr, bancoDisponible, bancoLee};

static void transporteInit() { gLector.init(bancoByteAlTnc, bancoLinea, nullptr); }

// LA CABECERA DEL BUCLE: es `gProtocol.feed(Serial)` en main.cpp.
static void transporteFeed() {
  gLector.marcaEnPantalla(false);   // el bucle quita la marca del driver
  gLector.bombea(gPuerto, false);   // leer el puerto...
  gLector.atiende();                // ...y EJECUTAR (aqui si)
}

// EL GANCHO DEL DRIVER DE LA TINTA: es `bombeaEsperaPantalla()` en main.cpp.
static void transporteBombea() {
  gLector.marcaEnPantalla(true);    // estamos DENTRO del driver
#ifdef BANCO_INGENUO
  // ★★ CONTRA-EJEMPLO: esto es JUSTO lo que no hay que hacer (ejecutar desde el driver:
  //    pintar dentro de un pintado). Esta aqui para que se vea que el banco LO DETECTA.
  gLector.bombea(gPuerto, true);
  gLector.atiende();
#else
  gLector.bombea(gPuerto, true);    // LEER Y ENCOLAR, y nada mas
#endif
}

static void transporteFinPintado() { gLector.marcaEnPantalla(false); }

// Lo que hace el bucle JUSTO DESPUES de displayRefresh(): cobrar lo encolado.
static void transporteCobra() { gLector.atiende(); }

static uint32_t transporteEncoladoMax() { return (uint32_t)gLector.maxPendientes(); }
#else
// ------------------- LA VERSION ANTERIOR (transcrita de git) --------------
// En lector_antes.cpp. Mismo contrato, para que el banco sea el mismo en los dos casos.
void transporteInit();
void transporteFeed();
void transporteBombea();
void transporteFinPintado();
void transporteCobra();
uint32_t transporteEncoladoMax();
#endif

// ===========================================================================
//  GUIONES DE LA PRUEBA
// ===========================================================================
static const char *CMD_CORTO = "{\"cmd\":\"status\"}\n";
static const size_t CMD_CORTO_LEN = 17;   // strlen("{\"cmd\":\"status\"}\n")

// Un `set` del configurador: la configuracion ENTERA en una linea JSON. Medido en este
// proyecto: 55 campos, ~1320 caracteres (de ahi que el tope de linea subiera a 4096).
// Aqui se rellena hasta 1.400 para que sea el caso real.
static char gCmdLargo[1500];
static size_t gCmdLargoLen = 0;
static void preparaCmdLargo() {
  size_t n = 0;
  n += (size_t)snprintf(gCmdLargo + n, sizeof(gCmdLargo) - n,
                        "{\"cmd\":\"set\",\"config\":{\"callsign\":\"EA2OY-7\",\"data\":\"");
  while (n < 1380) gCmdLargo[n++] = (char)('A' + (n % 26));
  n += (size_t)snprintf(gCmdLargo + n, sizeof(gCmdLargo) - n, "\"}}\n");
  gCmdLargoLen = n;
}

// Una trama KISS DE VERDAD: FEND, comando 0x00, cuerpo AX.25 con un 0x0A (LF), un 0xC0 y un
// 0xDB DENTRO (los dos ultimos tienen que ir escapados), y FEND. El 0x0A es la trampa: si el
// parser se saltara la regla del 0xC0, ese byte cerraria una "linea de texto" falsa.
static uint8_t gKissTrama[128];
static size_t gKissTramaLen = 0;
static uint8_t gKissPayload[64];
static size_t gKissPayloadLen = 0;
static uint8_t gKissJunto[256];       // la trama y, DETRAS, una linea JSON (escenario 7)
static size_t gKissJuntoLen = 0;

static void preparaKiss() {
  gKissPayload[0] = 0x82;
  gKissPayload[1] = 0x9A;
  gKissPayload[2] = 0x0A;   // ★ un LF DENTRO de la trama binaria
  gKissPayload[3] = 0xC0;   // ★ un FEND DENTRO (va escapado)
  gKissPayload[4] = 0xDB;   // ★ un FESC DENTRO (va escapado)
  gKissPayload[5] = 0x03;
  gKissPayload[6] = 0xF0;
  for (size_t i = 7; i < 20; i++) gKissPayload[i] = (uint8_t)('a' + (i % 20));
  gKissPayloadLen = 20;

  size_t n = 0;
  gKissTrama[n++] = KISS_FEND;
  gKissTrama[n++] = 0x00;   // puerto 0, comando 0 (datos)
  for (size_t i = 0; i < gKissPayloadLen; i++) {
    if (gKissPayload[i] == KISS_FEND) {
      gKissTrama[n++] = KISS_FESC;
      gKissTrama[n++] = KISS_TFEND;
    } else if (gKissPayload[i] == KISS_FESC) {
      gKissTrama[n++] = KISS_FESC;
      gKissTrama[n++] = KISS_TFESC;
    } else {
      gKissTrama[n++] = gKissPayload[i];
    }
  }
  gKissTrama[n++] = KISS_FEND;
  gKissTramaLen = n;

  // La trama y, detras, una linea JSON: el MISMO golpe de escritura (escenario 7).
  memcpy(gKissJunto, gKissTrama, gKissTramaLen);
  memcpy(gKissJunto + gKissTramaLen, CMD_CORTO, CMD_CORTO_LEN);
  gKissJuntoLen = gKissTramaLen + CMD_CORTO_LEN;
}

struct Escenario {
  const char *nombre;
  const char *quePasa;
  const void *datos;
  size_t n;
  uint32_t enviaEnMs;
  uint32_t pintadoMs;      // repintado que YA esta en marcha cuando llega el comando
  bool pintaCadaVuelta;    // ¿el panel vuelve a estar sucio en cada vuelta? (trafico APRS)
  bool kiss;
  uint32_t finMs;
};

static Escenario gEscenarios[] = {
  {"1. Comando CORTO (17 B) que llega mientras el panel pinta un parcial (1,5 s)",
   "el caso del operador: un `status` pedido con la pantalla ocupada",
   CMD_CORTO, 17, 100, 1500, false, false, 12000},

  {"2. Comando LARGO (1,4 KB: el `set` del configurador) durante el repintado, con\n"
   "   trafico APRS: el panel vuelve a estar sucio en CADA vuelta (parcial de 1,5 s)",
   "aqui el FIFO de 256 B se queda corto: hacen falta VARIAS vueltas del bucle",
   nullptr, 0, 100, 1500, true, false, 30000},

  {"3. El mismo comando LARGO (1,4 KB) con el nodo en reposo (el panel no repinta)",
   "control: sin repintados de por medio",
   nullptr, 0, 100, 0, false, false, 30000},

  {"4. Comando CORTO durante un COMPLETO forzado (2,5 s)",
   "el repintado mas largo que hay (tope de parciales o cambio de rotacion en caliente)",
   CMD_CORTO, 17, 100, 2500, false, false, 12000},

  {"5. TRAMA KISS (con un 0x0A, un 0xC0 y un 0xDB DENTRO) durante el repintado",
   "la regla del 0xC0: el binario se CUENTA y no se interpreta, y llega intacto",
   nullptr, 0, 100, 1500, false, true, 12000},

  {"6. En modo KISS, una linea JSON normal durante el repintado (sin trama abierta)",
   "la convivencia por bytes: el texto tiene que seguir funcionando con KISS puesto",
   CMD_CORTO, 17, 100, 1500, false, true, 12000},

  {"7. Trama KISS y, DETRAS, una linea JSON en el MISMO golpe de escritura",
   "esto NO es del cambio: la maquina KISS reabre trama en el FEND de cierre, asi que el\n"
   "   texto de detras se lo come. Tiene que salir IGUAL antes y despues (no se toca)",
   nullptr, 0, 100, 1500, false, true, 12000},
};
static const int kNumEscenarios = (int)(sizeof(gEscenarios) / sizeof(gEscenarios[0]));

// ===========================================================================
//  SIMULACION (la misma maquina de fases para las tres versiones)
// ===========================================================================
static void simula(const Escenario &e) {
  gMs = 0;
  gFifoN = 0;
  gHostDatos = nullptr;
  gHostRestante = 0;
  gHostUltimoMs = 0xFFFFFFFFu;
  gMsHostBloqueado = 0;
  gMsHostTermino = 0;
  gBytesLeidos = 0;
  gBytesLeidosEnPintado = 0;
  gEjecuciones = 0;
  gEjecucionesEnPintado = 0;
  gLineasBasura = 0;
  gKissFrames = 0;
  gKissMsUltimo = 0;
  gKissEnPintado = 0;
  gKissUltimoLen = 0;
  gKissActivo = e.kiss;
  gPintando = false;
  // La linea que tiene que llegar al parser va SIN el LF final (el parser lo quita).
  const char *esperada = e.kiss ? CMD_CORTO : (const char *)e.datos;
  size_t esperadaLen = e.kiss ? CMD_CORTO_LEN : e.n;
  if (esperadaLen > 0 && esperada[esperadaLen - 1] == '\n') esperadaLen--;
  gLineaEsperada = esperada;
  gLineaEsperadaLen = esperadaLen;

  kissReset();
  kissSetFrameHandler(bancoFrameKiss);
  transporteInit();

  // Fases de la vuelta del bucle, igual que main.cpp:
  //   LIBRE -> (feed) -> CUERPO -> [PINTADO] -> (cobra) -> COLA -> LIBRE ...
  enum Fase { LIBRE, CUERPO, PINTADO, COLA };
  Fase fase = LIBRE;
  uint32_t finFase = 0;          // ms en que se acaba la fase actual
  uint32_t proxBombeo = 0;

  if (e.pintadoMs > 0) {         // el panel ya estaba pintando cuando empieza la prueba
    fase = PINTADO;
    finFase = e.pintadoMs;
    proxBombeo = BOMBEO_MS;
    gPintando = true;
  }

  for (gMs = 0; gMs <= e.finMs; gMs++) {
    hostTick();
    if (gMs == e.enviaEnMs) hostEmpieza(e.datos, e.n);

    if (fase == PINTADO) {
      if (gMs >= proxBombeo) {                 // el driver BOMBEA cada 2 ms
        proxBombeo = gMs + BOMBEO_MS;
        transporteBombea();
      }
      if (gMs >= finFase) {
        gPintando = false;
        transporteFinPintado();                // displayRefresh() ha vuelto
        transporteCobra();                     // el bucle cobra lo encolado
        fase = COLA;
        finFase = gMs + COLA_VUELTA_MS;
      }
    } else if (fase == COLA) {
      if (gMs >= finFase) {
        fase = LIBRE;
        finFase = gMs;
      }
    } else if (fase == CUERPO) {
      if (gMs >= finFase) {
        if (e.pintaCadaVuelta) {
          fase = PINTADO;
          gPintando = true;
          finFase = gMs + e.pintadoMs;
          proxBombeo = gMs + BOMBEO_MS;
        } else {
          transporteCobra();                   // displayRefresh() vuelve sin pintar
          fase = COLA;
          finFase = gMs + COLA_VUELTA_MS;
        }
      }
    } else {  // LIBRE
      if (gMs >= finFase) {
        transporteFeed();                      // ★ LA CABECERA DEL BUCLE
        fase = CUERPO;
        finFase = gMs + BUCLE_CUERPO_MS;
      }
    }
  }
}

// ===========================================================================
//  SALIDA
// ===========================================================================
static void imprimeEscenario(const Escenario &e) {
  printf("%s\n   (%s)\n", e.nombre, e.quePasa);
  printf("   el host manda %u bytes en t=%u ms\n", (unsigned)e.n, (unsigned)e.enviaEnMs);

  if (gEjecuciones == 0) {
    printf("   >>> NINGUNA LINEA ATENDIDA\n");
  }
  for (uint32_t i = 0; i < gEjecuciones && i < 4; i++) {
    const Linea &l = gLineas[i];
    printf("   linea atendida: t=%u ms  %u bytes  -> %s\n", (unsigned)l.ms, (unsigned)l.n,
           l.entera ? "ENTERA" : (l.n < gLineaEsperadaLen ? "CORTADA (un trozo)" : "RARA"));
  }
  if (gEjecuciones > 4) printf("   ...y %u lineas mas\n", (unsigned)(gEjecuciones - 4));

  if (e.kiss) {
    const bool intacta = (gKissUltimoLen == gKissPayloadLen &&
                          memcmp(gKissUltimo, gKissPayload, gKissPayloadLen) == 0);
    if (gKissFrames == 0) {
      printf("   KISS: NO ha llegado ninguna trama\n");
    } else {
      printf("   KISS: tramas entregadas=%u  cuerpo=%u/%u bytes -> %s\n",
             (unsigned)gKissFrames, (unsigned)gKissUltimoLen, (unsigned)gKissPayloadLen,
             intacta ? "INTACTA" : "ROTA");
      printf("   KISS: entregada al manejador en t=%u ms\n", (unsigned)gKissMsUltimo);
    }
  }
  if (gLineasBasura > 0) {
    printf("   >>> %u lineas que NO eran el comando (algo se malinterpreto)\n",
           (unsigned)gLineasBasura);
  }

  printf("   --- medidas -------------------------------------------------\n");
  printf("   bytes leidos del puerto DURANTE el repintado : %u\n",
         (unsigned)gBytesLeidosEnPintado);
  printf("   maximo encolado de una vez (anillo)         : %u\n",
         (unsigned)transporteEncoladoMax());
  printf("   el host acaba de escribir en t              : %u ms\n", (unsigned)gMsHostTermino);
  printf("   el host BLOQUEADO esperando al nodo         : %u ms\n",
         (unsigned)gMsHostBloqueado);
  printf("   comandos EJECUTADOS DENTRO del repintado    : %u   <-- tiene que ser 0\n",
         (unsigned)gEjecucionesEnPintado);
  printf("   tramas KISS ENTREGADAS dentro del repintado : %u   <-- tiene que ser 0\n",
         (unsigned)gKissEnPintado);
  printf("\n");
}

int main(void) {
#ifndef BANCO_NUEVO
  printf("====== BANCO DEL USB - CODIGO ANTERIOR (el feed() de git, sin bombeo) ======\n");
  printf("el puerto se lee SOLO en la cabecera del bucle; durante el repintado, NADA\n");
#elif defined(BANCO_INGENUO)
  printf("====== BANCO DEL USB - CONTRA-EJEMPLO (ejecutando desde el driver) =========\n");
  printf("ESTE NO ES EL FIRMWARE: sirve para comprobar que el banco DETECTA la reentrada\n");
#else
  printf("======= BANCO DEL USB - CODIGO NUEVO (T-Echo Project Butter II) ===========\n");
  printf("el driver LEE Y ENCOLA durante el repintado; el bucle EJECUTA al acabar\n");
#endif
  printf("FIFO del CDC = %u bytes (CFG_TUD_CDC_RX_BUFSIZE) | paquete USB = %u B por ms\n",
         (unsigned)FIFO_CAP, (unsigned)EP_BYTES);
  printf("vuelta del bucle: cuerpo %u ms + cola %u ms | el driver bombea cada %u ms\n\n",
         (unsigned)BUCLE_CUERPO_MS, (unsigned)COLA_VUELTA_MS, (unsigned)BOMBEO_MS);

  preparaCmdLargo();
  preparaKiss();
  gEscenarios[1].datos = gCmdLargo;
  gEscenarios[1].n = gCmdLargoLen;
  gEscenarios[2].datos = gCmdLargo;
  gEscenarios[2].n = gCmdLargoLen;
  gEscenarios[4].datos = gKissTrama;
  gEscenarios[4].n = gKissTramaLen;
  gEscenarios[6].datos = gKissJunto;
  gEscenarios[6].n = gKissJuntoLen;

  for (int i = 0; i < kNumEscenarios; i++) {
    simula(gEscenarios[i]);
    imprimeEscenario(gEscenarios[i]);
  }
  return 0;
}
