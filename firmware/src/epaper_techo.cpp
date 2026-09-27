// epaper_techo.cpp - Pantalla de tinta electronica del LilyGO T-Echo / T-Echo Plus.
//
// QUE ES: el driver de la pantalla GDEH0154D67 (controlador SSD1681, 200x200) que llevan
// el T-Echo y el T-Echo Plus, integrado en el firmware completo (radio, GPS, sensores,
// config, registro, USB).
//
// ============================================================================
//  ESTA VERSION ES UNA REESCRITURA DELIBERADAMENTE MINIMA (2026-09-14)
//
//  El fichero anterior llevaba 1386 lineas con TODOS los experimentos que se
//  hicieron para averiguar por que la pantalla no pintaba: bit-bang, pruebas de
//  pines, "recetas" A/B/C/D/E, lectura del registro de estado 0x2F, ciclos de
//  corriente... Todo eso ya dio su respuesta y NO TIENE NADA QUE HACER en el
//  producto final: sobraba codigo, tocaba pines a mano con el periferico ya
//  configurado y era la causa mas probable del cuelgue del arranque.
//
//  Aqui queda SOLO el camino que esta demostrado que pinta en esta unidad:
//  el mismo que el banco de pruebas src/hello_techo.cpp, que ya escribio un
//  "hello world" en este panel.
// ============================================================================
//
// TRES COSAS QUE HAY QUE ENTENDER DE ESTA PANTALLA (si no, se falla):
//   1. Refresco completo = ~2 SEGUNDOS (parpadea en negro y blanco). Por eso NO se
//      puede repintar a cada pulsacion como en la OLED: se dibuja en memoria y se
//      manda al panel SOLO cuando algo cambia de verdad.
//   2. Si se hacen muchos refrescos parciales seguidos la imagen se ensucia
//      (fantasmas); el parcial queda DESACTIVADO a proposito hasta validar el completo.
//   3. El controlador se duerme (0x10 0x01) y conserva la imagen: la tinta electronica
//      es bistable, o sea que lo que se ve puede ser de un refresco viejo.
//
// ----------------------------------------------------------------------------
//  QUE HABIA MAL EN LA VERSION ANTERIOR, EN UNA LINEA CADA COSA
// ----------------------------------------------------------------------------
//  (a) nrfx_spim_xfer() CON MANEJADOR NULL ESPERA SIN TOPE. Su implementacion
//      (nrfx_spim.c, linea 598) es literalmente
//          while (!nrf_spim_event_check(p_spim, NRF_SPIM_EVENT_END)){}
//      Si el periferico no arranca (deshabilitado, sin reloj, con el pin robado),
//      ESA ESPERA NO TERMINA NUNCA y el firmware se queda mudo: el puerto USB
//      existe, pero no imprime ni obedece. Es exactamente el sintoma que teniamos.
//      -> AQUI NO SE USA nrfx_spim_xfer: se programan los registros del periferico
//         y se sondea EVENT_END **CON TOPE DE TIEMPO**. Si vence, se ABORTA la
//         transferencia (TASKS_STOP + disable) y se sigue vivo.
//
//  (b) LA BATERIA SE MIDE EN EL MISMO PIN QUE EL RELOJ DE LA PANTALLA.
//      src/sensors.cpp hace analogRead(31) cada 2 s y el SCK del panel es P0.31.
//      analogRead() reconfigura ese pin como entrada ANALOGICA, o sea que DESENGANCHA
//      el SCK del periferico SPIM2. A partir de ahi el periferico no puede dar reloj:
//      la transferencia se queda esperando su evento y (con nrfx) cuelga el nodo.
//      -> AQUI se reconfigura el periferico (pines + PSEL) ANTES DE CADA COMANDO.
//         Cuesta microsegundos y deja el driver a prueba de que alguien le toque los pines.
//
//  (c) epdLeerEstado() / epdPruebaPines() / bit-bang ponian los pines EN MODO GPIO A MANO
//      con el periferico SPIM2 ya inicializado. Eso deja el periferico y los pines en un
//      estado que no es el que espera el controlador.
//      -> ELIMINADAS. No queda ni una linea de bit-bang ni de prueba de pines.
//
//  (d) Cuatro inicializaciones del periferico (displayInit, displayInitTrasRadio,
//      displayArrancaPantalla...) con nrfx_spim_uninit() + nrfx_spim_init() cada vez.
//      El valor que devolvia (0x0BAD0000, que es NRFX_ERROR_BASE_NUM con los bits
//      bajos del enum perdidos) era sintoma de ese ir y venir de estado.
//      -> UNA SOLA vez, idempotente, y NO se hace uninit nunca.
//
//  (e) P1.11 (PIN_EPD_PWR) se empujaba a 3,3 V. El firmware de fabrica NO lo toca
//      (esta comentado en su epaper_init) y Meshtastic lo deja en bajo. Empujarlo
//      puede meter corriente por donde no toca.
//      -> NO SE TOCA. Se deja como entrada.
//
// PINES Y SECUENCIA: sacados del firmware de cfr34k (que corre en esta placa) y de la
// tabla oficial de LilyGO. Ver docs/HARDWARE_TECHO.md.
//
// License: GPL-3.0

#include "display.h"

#include <Arduino.h>
#include <math.h>       // isfinite(): comprobacion de la posicion a guardar
#include <nrfx_spim.h>
#include <stdio.h>
#include <string.h>     // strcmp/strncpy/strchr: el fichero ya los usaba sin incluirlo

#include "epd_font5x7.h"
#include "flog.h"
#include "gps.h"
#include "haptic.h"
#include "pins_board.h"
#include "power.h"
#include "radio.h"
#include "aprs.h"
#include "sota.h"
#include "cli.h"
#include "diag.h"       // diagTrazaTaller() / diagTrazaArranque(): las trazas del USB
#include "sensors.h"
#include "store.h"
#include "tnc.h"
#include "tracker.h"
#include <RadioLib.h>

namespace {

// ---------------------------------------------------------------- la pantalla
constexpr int EPD_W = 200;
constexpr int EPD_H = 200;
constexpr int EPD_STRIDE = EPD_W / 8;              // 25 bytes por fila
constexpr int EPD_BUFSZ = EPD_STRIDE * EPD_H;      // 5000 bytes

// Pines (ver pins_techo.h). El numero de Arduino ya es el de GPIO.
constexpr int PIN_SCK = PIN_EPD_SCK;    // P0.31
constexpr int PIN_MOSI = PIN_EPD_MOSI;  // P0.29 (SDI)
constexpr int PIN_CS = PIN_EPD_CS;      // P0.30
constexpr int PIN_DC = PIN_EPD_DC;      // P0.28
constexpr int PIN_RST = PIN_EPD_RST;    // P0.02
constexpr int PIN_BUSY = PIN_EPD_BUSY;  // P0.03

// El framebuffer: 1 bit por punto. Bit a 1 = BLANCO (el panel es "1 = blanco"),
// bit a 0 = NEGRO.
uint8_t gBuf[EPD_BUFSZ];

// ★★ EL PLANO ANTERIOR: LA CLAVE DEL REFRESCO PARCIAL (Paso 1, 2026-09-15) ★★
//
// El SSD1681 tiene DOS memorias de imagen: la "actual" (comando 0x24) y la "anterior"
// (comando 0x26). Al refrescar, el controlador LAS COMPARA y solo mueve los pixeles que
// han cambiado. Mandando las dos memorias iguales (lo que se hacia antes) la comparacion
// sale "nada ha cambiado" y el panel hace el refresco completo con su parpadeo de ~2 s.
//
// Mandando la imagen VIEJA en 0x26 y la NUEVA en 0x24, el controlador solo toca lo que
// cambia: eso es el refresco parcial, y por eso NO parpadea.
//
// `gPrevValido` dice si `gBufPrev` contiene de verdad lo que hay pintado en el panel. Al
// arrancar NO se sabe (la tinta es bistable y puede haber cualquier cosa de un refresco
// viejo), asi que el primer refresco de cada arranque es COMPLETO a proposito.
uint8_t gBufPrev[EPD_BUFSZ];
bool gPrevValido = false;

// ★ REFRESCO COMPLETO FORZADO (quita los fantasmas del parcial).
//   `kForzarCompleto` lo pide quien sabe que la imagen va a quedar sucia (por ejemplo,
//   volver de la pantalla de prueba o de la lista de escenas).
bool gForzarCompleto = false;

bool gReady = false;      // el panel ha recibido su secuencia de arranque
bool gDirty = false;      // hay algo dibujado sin mandar
bool gAsleep = false;     // el panel esta en deep sleep
bool gSpiListo = false;   // el periferico SPIM2 esta configurado en nuestros pines

// ------------------------------------------------------------------- SPI
// ★★ SPIM2, CON INSTANCIA PROPIA DE nrfx, Y NUNCA SPIM3 (2026-09-14) ★★
//
// En este core el objeto global `SPI` de Arduino vive en **SPIM3**
// (libraries/SPI/SPI.cpp: `#define _SPI_DEV NRF_SPIM3`, y la variante del T-Echo
// declara SPI_INTERFACES_COUNT 1, asi que `SPI1` ni existe). La radio usa ese `SPI`.
// SPIM2 es OTRO periferico, esta libre y su bloque de hardware NO es el mismo que
// TWIM0/TWIM1 (esos son SPIM0/SPIM1), asi que no choca con el I2C de los sensores.
//    -> La pantalla se queda con SPIM2 y la radio con SPIM3. No se pelean.
//
// SPIM0 queda PROHIBIDO: su bloque de hardware es el mismo que TWIM0 y habilitarlo
// colgo el nodo (ya probado, ver el historial del proyecto).
// P1.06. La pantalla del T-Echo NO lee nada (es solo escritura), pero el periferico
// pide un pin de MISO de todas formas. Se configura y no se usa.
constexpr int EPD_MISO_PIN = 38;

// Tope de seguridad por transferencia. A 4 MHz, 1 byte tarda 2 us, asi que 5000 bytes
// tardan ~10 ms. 60 ms es margen de sobra y evita cualquier espera eterna. Ademas es
// CORTO a proposito: aunque TODAS las transferencias fallaran, el refresco entero
// costaria menos de un segundo y el nodo no se quedaria nunca mudo.
constexpr uint32_t EPD_XFER_TIMEOUT_MS = 60;

// Diagnostico (lo enseña el comando "epd" del USB).
uint32_t gErrTimeout = 0;     // transferencias abortadas por tope de tiempo (0 = bien)
uint32_t gErrSpi = 0;         // errores varios del periferico
uint32_t gMsUltimo = 0;       // lo que tardo el ultimo refresco (completo o parcial)
uint32_t gMsUltimoCompleto = 0;  // el ultimo COMPLETO (para comparar)
uint32_t gMsUltimoParcial = 0;   // el ultimo PARCIAL (0 = todavia no ha habido ninguno)
uint32_t gMsCompletoMs = 0;      // millis() en que EMPEZO el ultimo refresco COMPLETO
bool gHuboCompleto = false;      // ¿ha habido algun completo? (ver epdTocaCompleto)
uint32_t gBusyTimeouts = 0;      // veces que BUSY no solto dentro del tope (0 = BUSY informa)
uint32_t gBusyAvisos = 0;        // refrescos en los que BUSY estaba ALTO (el panel informa)
uint32_t gNCompletos = 0;     // refrescos completos hechos
uint32_t gNParciales = 0;     // refrescos parciales hechos
uint32_t gNParcialesSeguidos = 0;  // parciales desde el ultimo completo (fantasmas)
uint32_t gCmdActual = 0;      // comando que se esta mandando ahora (para el diagnostico)
uint32_t gCmdTimeout = 0;     // el PRIMER comando que se atasco (carta blanca del fallo)
uint32_t gFaseTimeout = 0;    // 1 = se atasco el BYTE de comando; 2 = los DATOS
uint32_t gErrSeguidos = 0;    // transferencias falladas desde la ultima que salio bien
uint32_t gErrSeguidosMax = 0; // el peor tramo seguido
uint32_t gFaseActual = 0;     // que parte de la transferencia se esta mandando
uint32_t gTxVistoEndtx = 0;   // transferencias que terminaron por EVENTS_ENDTX
uint32_t gTxVistoEnd = 0;     // ... por EVENTS_END
uint32_t gTxVistoAmount = 0;  // ... por TXD.AMOUNT == 0 (red de seguridad)
uint32_t gBytesBitBang = 0;   // bytes que ha movido el bit-bang (prueba de que trabaja)

inline void epdCs(bool activo) { digitalWrite(PIN_CS, activo ? LOW : HIGH); }
inline void epdDc(bool datos)  { digitalWrite(PIN_DC, datos ? HIGH : LOW); }

// ============================================================================
//  ★★ DOS TRANSPORTES, Y EL QUE SE USA ES EL BIT-BANG (2026-09-14) ★★
//
//  El camino del periferico SPIM2 esta AQUI AL LADO, entero y con diagnostico, pero
//  NO se usa para pintar. Motivo, medido en esta placa con el comando "epdsonda":
//
//      SPIM2->ENABLE = 1, PSEL.SCK = P0.31, PSEL.MOSI = P0.29, FREQ = 4 MHz
//      (o sea: la configuracion esta bien y se deja escribir)
//
//      pero al arrancar una transferencia de 5000 bytes:
//        TASKS_START ............ no produce EVENTS_STARTED
//        TASKS_STOP / SUSPEND ... no producen EVENTS_STOPPED
//        TXD.AMOUNT ............. se queda clavado en 1 y no baja
//        EVENTS_ENDTX / END ..... no suben nunca
//
//  Un periferico que NO OBEDECE A SUS PROPIAS TAREAS esta muerto: no es cuestion de
//  eventos ni de esperas, es que no arranca. (Y es justo el blocaje que describia el
//  proyecto: el periferico de la pantalla se queda tomado/apagado en esta integracion).
//
//  ★ Y ESTO ES LO IMPORTANTE: el bit-bang SI funciona en esta placa. El encargo lo da
//    por descartado ("con PSEL apuntando a un pin lo gobierna el periferico y
//    digitalWrite() no hace nada"), pero esa conclusion se saco cuando el bit-bang
//    todavia estaba mal por otras dos razones que se descubrieron DESPUES:
//      - mandaba un byte de mas en 0x11, 0x44 y 0x45 (LEN() cuenta el comando)
//      - el framebuffer iba con el orden de bits equivocado
//    Con las dos cosas corregidas, el banco de pruebas pinto el "hello world" con
//    bit-bang (su comando "tag 0"), y el pin no lo gobierna SPIM2 porque SPIM2 esta
//    muerto: nadie mueve esos pines.
//
//  Se deja seleccionable en caliente (gTransporte) para poder comparar los dos desde el
//  USB con el comando "epdtransporte 0|1".
// ============================================================================

// 0 = bit-bang por GPIO (el que se usa), 1 = periferico SPIM2
int gTransporte = 0;

// ------------------------------------------------------------------- bit-bang
// Medio periodo del reloj en microsegundos. 1 us => ~500 kHz: TODAVIA MUY SOBRADO para el
// SSD1681 (aguanta hasta ~10 MHz), y ya es el doble de rapido que antes. La ganancia se nota
// al pulsar un boton o moverse por el menu, donde el trasvasaje de los dos planos (10.000
// bytes) dominaba el tiempo (~0,8 s). Con la escritura combinada de SCK+MOSI de abajo, el
// trasvase baja a ~0,1-0,2 s. Se deja 1 us a proposito (no menos) por integridad de señal.
constexpr uint32_t EPD_BIT_US = 1;

// Escribe en el puerto 0 o 1 segun el pin. Los numeros de Arduino de esta placa son
// directos: N = P0.N y 32+N = P1.N (lo dice variants/techo/variant.h).
inline void epdGpioEscribe(int pin, bool alto) {
  NRF_GPIO_Type *p = (pin < 32) ? NRF_P0 : NRF_P1;
  const uint32_t b = 1u << (pin & 31);
  if (alto) p->OUTSET = b; else p->OUTCLR = b;
}

// ★★ RECONFIGURACION POR "LEER-MODIFICAR-ESCRIBIR", QUE ES LO QUE HACE FALTA ★★
//
// El primer intento reconfiguraba los pines con `nrf_gpio_cfg()` / `pinMode()`, que
// ESCRIBEN TODO el registro PIN_CNF. Eso tiene un efecto lateral grave en este firmware:
// entre que se borra la configuracion vieja y se escribe la nueva, el pin se queda un
// instante como ENTRADA, y con el reloj de la pantalla flotando en mitad de una
// transferencia se pierden flancos (o se cuelga la conversacion con el panel).
//
// Aqui solo se tocan los bits que hacen falta (DIR, INPUT, DRIVE) y, sobre todo, no se
// pasa nunca por un estado en el que el pin no este gobernado. Ademas se deja el buffer
// de entrada CONECTADO en los pines que empujamos, para poder LEERLOS: es lo que permite
// comprobar en el propio chip que el bit-bang esta moviendo de verdad el pin.
inline void epdPinSalidaFuerte(int pin) {
  NRF_GPIO_Type *g = (pin < 32) ? NRF_P0 : NRF_P1;
  const uint32_t b = pin & 31;
  // DIR=salida (bit0), INPUT=conectado (bit1), PULL=ninguna (bits2-3),
  // DRIVE=H0H1 alta corriente (bits8-10), SENSE=desconectado (bits16-17)
  g->PIN_CNF[b] = (1u << 0) | (1u << 1) | (3u << 8);
}

// Saca un byte, MSB primero, con el flanco de subida en medio (el panel lee ahi).
// ★ ACELERACION (2026-09-15): SCK (P0.31) y MOSI (P0.29) estan los DOS en el puerto 0,
//   asi que se escriben con un solo acceso a OUTSET/OUTCLR combinando las mascaras. Antes
//   cada bit hacia 3 llamadas de GPIO + 3 delayMicroseconds; ahora 4 escrituras de registro
//   y UNA espera corta por bit. Es lo que hace que cambiar de escena o moverse por el menu
//   responda notablemente mas rapido (el trasvase de los dos planos dominaba el refresco).
//   El `delayMicroseconds(EPD_BIT_US)` (1 us) mantiene un ritmo ~500 kHz, de sobra dentro
//   del limite del SSD1681 y sin arriesgar la integridad de la señal.
#define EPD_MSK_SCK  (1u << 31)
#define EPD_MSK_MOSI (1u << 29)
inline void epdBitBangByte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    const uint32_t m = (b & (1u << i)) ? EPD_MSK_MOSI : 0u;
    NRF_P0->OUTCLR = EPD_MSK_SCK | EPD_MSK_MOSI;   // sck y mosi bajos (filo de bajada)
    NRF_P0->OUTSET = m;                             // pongo el dato en MOSI
    NRF_P0->OUTSET = EPD_MSK_SCK;                    // subo SCK (filo de subida: se lee)
    delayMicroseconds(EPD_BIT_US);
    NRF_P0->OUTCLR = EPD_MSK_SCK;                    // bajo SCK
  }
}

// Deja los pines de la pantalla como SALIDAS DE ALTA CORRIENTE, que es como los pone
// GxEPD2 y el propio core ("el SPI a 8 MHz necesita salidas de alta corriente").
//
// ★★★ AQUI ESTABA EL FALLO QUE FALTABA, Y ES SUTIL (2026-09-14) ★★★
//
// **UN PERIFERICO CON `PSEL` APUNTANDO A UN PIN SE QUEDA ESE PIN, AUNQUE ESTE
// DESHABILITADO.** No basta con `nrf_spim_disable()`: mientras `PSEL.SCK` sea 0x1F
// (P0.31), el GPIO **no** puede gobernar ese pin y todas las lecturas dan 0. Es
// exactamente la trampa que el proyecto ya tenia documentada ("un pin con PSEL
// apuntandole lo gobierna el periferico, no el GPIO") pero que aqui se colaba por otra
// puerta: este firmware llama a `epdSpiConfigura()` (que apunta PSEL a P0.31/P0.29) al
// arrancar, y despues el bit-bang intentaba mandar en esos mismos pines. No mandaba.
//
// Medido en esta unidad, y por eso se sabe:
//     con PSEL apuntando a P0.31 -> "SCK=0/00"  (parecia sujeto a masa)
//     con PSEL desconectado      -> "SCK=0/01"  (se gobierna perfectamente)
//
// Por eso, en modo bit-bang, **se APAGA y se DESENGANCHA el periferico** antes de tocar
// los pines. Es lo que hace el banco de pruebas que pinta (nunca inicializa SPIM2 cuando
// va por bit-bang) y es la diferencia que lo explica todo.
#define EPD_PSEL_DESCONECTADO 0x80000000u

// ★★ EL RELOJ SE DEJA EN BAJO (MODO 0) ★★
// En modo 0 el reloj reposa en BAJO y el panel lee en el flanco de SUBIDA: arrancando en
// alto, el primer flanco que ve el panel es de bajada y pierde el primer bit de cada byte.
// El banco de pruebas pone `SCK=0` explicitamente antes de empezar; aqui se hace igual.
void epdPinesBitBang() {
  // 1) el periferico fuera: deshabilitado Y con PSEL desconectado.
  NRF_SPIM2->ENABLE = 0;
  NRF_SPIM2->PSEL.SCK = EPD_PSEL_DESCONECTADO;
  NRF_SPIM2->PSEL.MOSI = EPD_PSEL_DESCONECTADO;
  NRF_SPIM2->PSEL.MISO = EPD_PSEL_DESCONECTADO;

  // 2) y ahora si, los pines son nuestros.
  epdPinSalidaFuerte(PIN_CS);
  epdPinSalidaFuerte(PIN_DC);
  epdPinSalidaFuerte(PIN_SCK);
  epdPinSalidaFuerte(PIN_MOSI);
  nrf_gpio_pin_set(PIN_CS);      // en reposo el CS esta ALTO
  nrf_gpio_pin_set(PIN_DC);
  nrf_gpio_pin_clear(PIN_SCK);   // modo 0: el reloj en reposo, BAJO
  nrf_gpio_pin_clear(PIN_MOSI);
  gErrSeguidos = 0;
}

// Deja el reset como entrada con subida (es como lo deja el firmware de fabrica tras
// soltarlo) y BUSY como entrada.
void epdPinesReposo() {
  pinMode(PIN_RST, INPUT_PULLUP);
  pinMode(PIN_BUSY, INPUT);
}

// ------------------------------------------------------------------- SPIM2 (sin usar)
// Configura el periferico SPIM2 en nuestros pines. Se deja por si algun dia se
// averigua por que sus tareas no arrancan en esta integracion (ver la nota de arriba).
void epdSpiConfigura() {
  // ★★ UN SOSPECHOSO MAS, Y ES FUERTE (2026-09-14): EL SAADC ★★
  //
  // src/sensors.cpp mide la bateria con `analogRead(31)`, o sea el canal AIN7, que es
  // **P0.31: el SCK DE LA PANTALLA**. Y en el T-Echo eso es un error, porque su bateria
  // esta en P0.04 (lo dicen variants/techo/variant.h y src/pins_techo.h, y el propio
  // firmware lo confirma: `bat` contesta 0.00V en vez de la tension real).
  //
  // El driver del ADC (analogRead_internal) deja CH[0].PSELP apuntando a ese pin y ASI SE
  // QUEDA (leido en el registro: `CH0.PSELP=8` = AIN7) aunque el ADC quede deshabilitado.
  // Con la entrada analogica enganchada a un pin que ADEMAS empujamos como salida digital,
  // el pin no puede subir (medido: PIN_CNF correcto, OUT a 1, y el pin clavado en 0), y
  // sin reloj no hay conversacion posible con el panel.
  //
  // Aqui se suelta ese canal antes de configurar los pines de la pantalla, antes de cada
  // comando. No se rompe nada nuevo: la lectura de bateria de esta placa ya estaba mal.
  NRF_SAADC->ENABLE = 0;
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_NC;
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELP_PSELP_NC;

  nrf_gpio_cfg(PIN_SCK,
               NRF_GPIO_PIN_DIR_OUTPUT,
               NRF_GPIO_PIN_INPUT_CONNECT,
               NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_H0H1,
               NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_pin_clear(PIN_SCK);

  nrf_gpio_cfg(PIN_MOSI,
               NRF_GPIO_PIN_DIR_OUTPUT,
               NRF_GPIO_PIN_INPUT_DISCONNECT,
               NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_H0H1,
               NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_pin_clear(PIN_MOSI);

  NRF_SPIM2->PSEL.SCK = PIN_SCK;
  NRF_SPIM2->PSEL.MOSI = PIN_MOSI;
  NRF_SPIM2->PSEL.MISO = EPD_MISO_PIN;

  if (!gSpiListo) {
    nrfx_spim_config_t cfg = {};
    cfg.sck_pin      = (uint32_t)PIN_SCK;
    cfg.mosi_pin     = (uint32_t)PIN_MOSI;
    cfg.miso_pin     = (uint32_t)EPD_MISO_PIN;
    cfg.ss_pin       = NRFX_SPIM_PIN_NOT_USED;   // el CS lo gobierno yo a mano
    cfg.frequency    = NRF_SPIM_FREQ_4M;
    cfg.mode         = NRF_SPIM_MODE_0;
    cfg.bit_order    = NRF_SPIM_BIT_ORDER_MSB_FIRST;
    cfg.orc          = 0xFF;
    cfg.irq_priority = 3;
    static nrfx_spim_t spim = NRFX_SPIM_INSTANCE(2);
    // El valor de retorno de nrfx_spim_init NO es fiable en este core (devuelve
    // 0x0BAD0000, que es la BASE de los codigos de nrfx y no un error real). Lo que
    // manda es que los registros queden bien, y eso se comprueba justo debajo.
    if (nrfx_spim_init(&spim, &cfg, NULL, NULL) != NRFX_SUCCESS) gErrSpi++;
    nrf_spim_enable(NRF_SPIM2);
    gSpiListo = true;
  }

  if (NRF_SPIM2->PSEL.SCK != (uint32_t)PIN_SCK ||
      NRF_SPIM2->PSEL.MOSI != (uint32_t)PIN_MOSI ||
      (NRF_SPIM2->ENABLE & 1u) == 0) {
    gErrSpi++;
    NRF_SPIM2->PSEL.SCK = PIN_SCK;
    NRF_SPIM2->PSEL.MOSI = PIN_MOSI;
    nrf_spim_enable(NRF_SPIM2);
  }
}

// ★★ T-ECHO PROJECT BUTTER: EL GANCHO DEL BOTON (2026-09-15) ★★
//
// El refresco de este panel BLOQUEA: desde que se manda 0x20 hasta que la tinta se ha
// movido pasan 0,35-2 s de espera, y antes eso era `delay()` a secas. En ese rato la
// maquina de gestos del boton no corria, asi que un toque que caia al principio de un
// refresco se resolvia (y se atendia) un refresco ENTERO mas tarde. Con el gancho, esas
// esperas se aprovechan para bombear el boton: los flancos ya los coge la interrupcion
// (button.cpp), pero aqui ademas se resuelven los plazos vencidos y el gesto queda
// ENCOLADO, listo para que el bucle lo ejecute en cuanto el panel quede libre.
//
// ★ El gancho NO pinta y NO ejecuta acciones (lo pone main.cpp): solo encola. Entrar a
//   pintar desde dentro de un pintado seria un lio; ejecutar una baliza, tambien.
//   Se declara AQUI ARRIBA, antes de la primera funcion del driver que lo usa.
//   ★ `displaySetPumpBoton()`, que es quien lo pone, se define FUERA del espacio de
//     nombres anonimo (mas abajo, junto a las sondas de taller).
static void (*gPumpBoton)(void) = nullptr;
inline void ePDBombea() { if (gPumpBoton) gPumpBoton(); }

// ★ TRANSFERENCIA POR EL PERIFERICO, CON TOPE DE TIEMPO. NO se usa para pintar (ver la
//   nota de arriba), pero se deja entera: si algun dia se arregla, basta con poner
//   gTransporte = 1. El tope es lo que impide que el firmware se quede mudo.
bool epdXferSpim(const uint8_t *tx, size_t n) {
  if (n == 0) return true;
  if (tx == nullptr) return false;
  const uint32_t dir = (uint32_t)(uintptr_t)tx;
  if (dir < 0x20000000u || dir >= 0x20040000u) { gErrSpi++; return false; }  // no es RAM

  NRF_SPIM2->TXD.PTR = dir;
  NRF_SPIM2->TXD.MAXCNT = (uint32_t)n;
  NRF_SPIM2->RXD.PTR = 0;
  NRF_SPIM2->RXD.MAXCNT = 0;
  NRF_SPIM2->EVENTS_END = 0;
  NRF_SPIM2->EVENTS_ENDTX = 0;
  NRF_SPIM2->EVENTS_STARTED = 0;
  NRF_SPIM2->TASKS_START = 1;

  const uint32_t t0 = millis();
  for (;;) {
    if (NRF_SPIM2->EVENTS_ENDTX) { gTxVistoEndtx++; break; }
    if (NRF_SPIM2->EVENTS_END)   { gTxVistoEnd++;   break; }
    if (NRF_SPIM2->TXD.AMOUNT == 0) { gTxVistoAmount++; break; }
    if (millis() - t0 > EPD_XFER_TIMEOUT_MS) {
      NRF_SPIM2->TASKS_STOP = 1;
      NRF_SPIM2->EVENTS_END = 0;
      NRF_SPIM2->EVENTS_ENDTX = 0;
      NRF_SPIM2->EVENTS_STOPPED = 0;
      nrf_spim_disable(NRF_SPIM2);
      nrf_spim_enable(NRF_SPIM2);
      gErrTimeout++;
      if (gCmdTimeout == 0) { gCmdTimeout = gCmdActual; gFaseTimeout = gFaseActual; }
      gErrSeguidos++;
      if (gErrSeguidos > gErrSeguidosMax) gErrSeguidosMax = gErrSeguidos;
      return false;
    }
  }
  NRF_SPIM2->EVENTS_END = 0;
  NRF_SPIM2->EVENTS_ENDTX = 0;
  gErrSeguidos = 0;
  return true;
}

// La transferencia que se usa de verdad: elige el transporte.
bool epdXfer(const uint8_t *tx, size_t n) {
  if (n == 0) return true;
  if (tx == nullptr) return false;
  if (gTransporte == 1) return epdXferSpim(tx, n);
  for (size_t i = 0; i < n; i++) epdBitBangByte(tx[i]);
  gBytesBitBang += (uint32_t)n;
  gErrSeguidos = 0;
  return true;
}

// Comando con sus datos, con el CS bajo todo el rato (igual que el firmware de fabrica:
// su send_command() lo deja bajo y lo sube al terminar).
void epdCmdData(uint8_t cmd, const uint8_t *datos, size_t n) {
  // ★ T-ECHO PROJECT BUTTER: entre comando y comando se bombea el boton. El trasvase
  //   de un plano (5.000 bytes por bit-bang) son ~80-125 ms de bucle cerrado: es la
  //   ventana ciega mas larga que queda, y asi se parte en trozos mas cortos.
  ePDBombea();
  gCmdActual = cmd;
  // En modo bit-bang NO hace falta reconfigurar nada antes de cada comando: nadie le
  // quita los pines al GPIO (SPIM2 esta desenganchado) y el ADC de la bateria ya no usa
  // P0.31 (ver sensors.cpp). En modo SPIM2 si se reengancha, por si acaso.
  if (gTransporte == 1) epdSpiConfigura();
  epdCs(true);
  epdDc(false);
  gFaseActual = 1;
  epdXfer(&cmd, 1);
  if (n) {
    epdDc(true);
    gFaseActual = 2;
    epdXfer(datos, n);
  }
  epdCs(false);
}

void epdCmd(uint8_t cmd) { epdCmdData(cmd, nullptr, 0); }

void epdCmd1(uint8_t cmd, uint8_t d) { epdCmdData(cmd, &d, 1); }

void epdCmd2(uint8_t cmd, uint8_t d1, uint8_t d2) {
  const uint8_t d[2] = {d1, d2};
  epdCmdData(cmd, d, 2);
}

void epdCmd3(uint8_t cmd, uint8_t d1, uint8_t d2, uint8_t d3) {
  const uint8_t d[3] = {d1, d2, d3};
  epdCmdData(cmd, d, 3);
}

void epdCmd4(uint8_t cmd, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4) {
  const uint8_t d[4] = {d1, d2, d3, d4};
  epdCmdData(cmd, d, 4);
}

// El framebuffer entero (5000 bytes) en una sola conversacion con el panel: CS bajo,
// el comando, y los datos.
void epdCmdBuf(uint8_t cmd, const uint8_t *buf, size_t n) {
  gCmdActual = cmd;
  if (gTransporte == 1) epdSpiConfigura();
  epdCs(true);
  epdDc(false);
  gFaseActual = 1;
  epdXfer(&cmd, 1);
  epdDc(true);
  gFaseActual = 2;
  epdXfer(buf, n);
  epdCs(false);
}

// ------------------------------------------------------------------ arranque
// El reset, COPIADO del firmware que funciona (cfr34k):
//   RST a 0 (reset activo) -> 20 ms -> RST a 1 -> se SUELTA (entrada con pull-up).
// Sueltan el pin a proposito: el panel tiene que ver un flanco de subida limpio.
void epdReset() {
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(20);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_RST, INPUT_PULLUP);   // se suelta
  delay(20);
}

// ---------------------------------------------------------------------------
//  ★★ PASO 1: REFRESCO PARCIAL — LAS DOS FORMAS DE REFRESCAR (2026-09-15) ★★
//
//  Patron copiado del firmware de referencia que funciona en esta placa
//  (firm_ref_techo/t-echo-lora-aprs-main/src/epaper.c, FULL_UPDATE_SEQUENCE y
//  PARTIAL_UPDATE_SEQUENCE). La secuencia de comandos es LA MISMA en los dos casos y solo
//  cambian dos valores:
//
//    COMPLETO  ->  0x3C = 0x05 (borde)  y  0x22 = 0xF7 (secuencia de refresco)  ~2,7 s, parpadea
//    PARCIAL   ->  0x3C = 0x80 (borde)  y  0x22 = 0xFF (secuencia de refresco)  ~0,4 s, NO parpadea
//
//  Y lo que de verdad hace que el parcial no parpadee no son esos dos bytes: es mandar los
//  DOS PLANOS distintos (0x26 = lo que habia pintado, 0x24 = lo que se quiere pintar). El
//  controlador compara y solo mueve los pixeles que cambian. Mandando los dos planos
//  iguales (lo que se hacia antes) el controlador cree que no hay nada que hacer y se
//  comporta como un completo.
//
//  `kMaxParcialesSeguidos` y `kMaxMsSinCompleto` fuerzan un COMPLETO de vez en cuando para
//  quitar los fantasmas que deja el parcial. El de referencia lo hace cada 60 minutos
//  (main.c, `m_epaper_force_full_refresh`); aqui ademas se pone un tope por numero de
//  parciales, que es lo que protege cuando la pantalla se repinta muy seguido.
//  720 parciales es una red de seguridad pensada para el peor caso: si algo que se dibuja
//  cambia cada pocos segundos (por ejemplo un contador de trafico en la escena de radio), el
//  refresco puede caer cada 5 s, y 720 son 60 minutos a ese ritmo. El repintado de refresco
//  (`kRepintadoMaxMs`) va aparte y no depende de este contador.
// ---------------------------------------------------------------------------
constexpr uint8_t kOndaBordeCompleto = 0x05;
constexpr uint8_t kOndaBordeParcial  = 0x80;
constexpr uint8_t kSecuenciaCompleto = 0xF7;
constexpr uint8_t kSecuenciaParcial  = 0xFF;

constexpr uint32_t kMaxParcialesSeguidos = 720;        // tope por numero de parciales
constexpr uint32_t kMaxMsSinCompleto = 60UL * 60UL * 1000UL;   // 60 min (como el de referencia)

// ---------------------------------------------------------------------------
//  ★★ P.6: LA ESPERA PREVIA DE BUSY, BAJADA DE 2.000 ms A 300 ms (2026-09-15) ★★
//
//  Medido en hardware: el parcial pintaba y NO parpadeaba (confirmado a ojo por el
//  operador), pero tardaba 3.217 ms. La cuenta salia clavada:
//
//      2.000 ms (espera previa agotando su tope) + ~1.200 ms (secuencia del parcial)
//
//  O sea: **BUSY esta en ALTO justo antes de empezar** (el panel sale de su sueno profundo
//  con el pin arriba) y la espera previa se pasaba 2 segundos enteros esperando a que
//  bajara, sin que bajara. No hay ningun refresco solapado que evitar, y esos 2 s hacian
//  inviable el carrusel.
//
//  SE QUEDA UNA ESPERA, PERO CORTA: sigue habiendo red de seguridad (nunca una espera sin
//  salida) y si algun dia el panel contesta de verdad, se aprovecha.
//  ★★ VELOCIDAD (2026-09-21): los 300 ms de esta espera se han quitado, y aqui vivia la
//     constante que los ponia (`kEsperaPreviaBusyMs`). El motivo, medido en el codigo: en
//     ESTA unidad BUSY no informa NUNCA, asi que la espera no esperaba a nada: vencia el
//     tope y seguia. Se pagaban 300 ms en CADA refresco para nada. Ahora se sondea con un
//     tope corto (ver `kSondeoBusyRapidoMs` en epdRefresca): si BUSY informa, se aprovecha;
//     si no, se pierden milisegundos en vez de 300.
// ---------------------------------------------------------------------------

// Espera a que el panel suelte BUSY, CON TOPE. En ESTA unidad BUSY no informa, asi que
// esto NO es la espera principal: sirve para dos cosas:
//   (a) si el panel contesta, se aprovecha y se termina antes;
//   (b) si BUSY se queda clavado, se sale por el tope y el nodo sigue vivo (nunca se
//       vuelve al bucle sin salida de la version anterior).
bool epdEsperaBusy(uint32_t topeMs) {
  const uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) == HIGH) {
    if ((millis() - t0) > topeMs) {
      gBusyTimeouts++;
      return false;
    }
    ePDBombea();   // ★ el boton se sigue mirando: este tope puede ser de 300-1500 ms
    delay(2);
  }
  return true;
}

// Tiempo que se espera al panel DESPUES de mandar 0x20 (a pintar).
//   * si BUSY se mueve: se espera a que baje, con tope (y si vence el tope, se sigue).
//   * si BUSY NO se mueve (lo medido en esta unidad): se espera a ciegas un tiempo prudente.
// En los dos casos el `delay()` final es lo que garantiza que el panel haya terminado antes
// de mandarlo a dormir (0x10 0x01): dormirlo a mitad de un refresco dejaria la imagen a
// medias. Es tiempo de la pantalla, no del nodo: la radio y el GPS siguen en el bucle.
//
// ★ AQUI ESTABA LA MAYOR PARTE DE LA LATENCIA DE LOS TOQUES (T-Echo Project Butter,
//   2026-09-15): este `delay(minimoMs - gastado)` es UN TIRON DE 350 ms (parcial) o
//   2.000 ms (completo) sin mirar el boton. Ahora se espera lo mismo, pero en trozos de
//   2 ms y bombeando el boton en cada trozo: el panel tarda igual y el toque no se
//   queda esperando a que acabe.
void epdEsperaPintado(uint32_t minimoMs, uint32_t topeBusyMs) {
  const uint32_t t0 = millis();
  const bool busyInformo = epdEsperaBusy(topeBusyMs);
  // ★ Si `epdEsperaBusy()` devuelve false es que encontro BUSY en ALTO (el panel estaba
  //   trabajando) y lo vio bajar: eso es la prueba DIRECTA de que en esta unidad BUSY
  //   informa. Si devuelve true, BUSY ya estaba bajo al mirar. Se cuentan las dos cosas.
  if (!busyInformo) gBusyAvisos++;
  while ((uint32_t)(millis() - t0) < minimoMs) {
    ePDBombea();
    delay(2);
  }
}

// La secuencia del SSD1681, byte a byte la de GxEPD2_154_D67 / cfr34k.
// OJO AL DETALLE QUE COSTO DOS DIAS: en su tabla LEN() CUENTA EL BYTE DE COMANDO.
//   LEN(1) = comando y nada mas      -> epdCmd()
//   LEN(2) = comando + 1 dato        -> epdCmd1()
//   LEN(3) = comando + 2 datos       -> epdCmd2()
//   LEN(4) = comando + 3 datos       -> epdCmd3()
// Mandar un byte de mas deja la ventana de RAM mal configurada y el panel no arranca.
//
// ★ El PARCIAL usa esta MISMA secuencia y solo cambia la onda de borde (0x3C), que es
//   exactamente lo que hace el de referencia: alli las dos tablas son identicas salvo
//   0x3C y 0x22. Tambien necesita su reset hardware, y lo tiene (epdReset()).
void epdInitPanel(bool parcial) {
  epdReset();

  epdCmd(0x12);                       // soft reset
  delay(10);
  epdCmd3(0x01, 0xC7, 0x00, 0x00);    // driver output control
  epdCmd1(0x3C, parcial ? kOndaBordeParcial : kOndaBordeCompleto);  // border waveform
  epdCmd1(0x18, 0x80);                // sensor de temperatura interno
  epdCmd1(0x11, 0x03);                // entry mode: x e y incrementan
  epdCmd2(0x44, 0x00, (EPD_W - 1) / 8);                     // RAM x: 0..24
  epdCmd4(0x45, 0x00, 0x00, (EPD_H - 1) % 256, (EPD_H - 1) / 256);  // RAM y: 0..199
  epdCmd1(0x4E, 0x00);                // contador X = 0
  epdCmd2(0x4F, 0x00, 0x00);          // contador Y = 0
}

// El cuerpo comun de los dos refrescos: los dos planos y el "a pintar".
//   `bufAnterior` = lo que hay pintado; con nullptr se manda `bufActual` en los dos planos
//   (que es lo que se hacia en el completo de la version anterior).
void epdRefresca(bool parcial, const uint8_t *bufAnterior, const uint8_t *bufActual) {
  const uint32_t t0 = millis();

  // ★★ VUELTA ATRAS DEL 2026-09-21 (¡y esto hay que leerlo!) ★★
  //   Aqui se probo a bajar esta espera de 300 ms a 12 ms, con el argumento de que en esta
  //   unidad BUSY no informa y por tanto la espera "no esperaba a nada".
  //   RESULTADO MEDIDO EN LA PLACA DEL OPERADOR: con el firmware nuevo LA PANTALLA SE QUEDO
  //   EN NEGRO y EL TACTIL DEJO DE RESPONDER. El tactil solo se atiende DURANTE los refrescos
  //   (el driver bombea el boton en sus esperas, ver ePDBombea), asi que si el refresco se
  //   atasca, el tactil se queda mudo con el. Se sospecha que estas esperas SON la red que
  //   evita empezar un refresco encima de otro cuando BUSY no informa.
  //   O SEA: la deduccion "BUSY no informa, luego no hace falta esperar" ERA FALSA. La espera
  //   sirve aunque BUSY no diga nada. NO SE VUELVE A TOCAR ESTO SIN MEDIRLO EN UNA PLACA.
  constexpr uint32_t kEsperaPreviaBusyMs = 300;
  epdEsperaBusy(kEsperaPreviaBusyMs);

  epdInitPanel(parcial);

  // (1) ENCENDER LAS TENSIONES DEL PANEL. Copiado de `GxEPD2::_PowerOn()`: `0x22 = 0xE0` y
  //     luego `0x20`. Sin esto el controlador acepta los datos pero el panel no tiene con
  //     que mover la tinta.
  //
  //     OJO: el parcial del firmware de referencia NO manda este 0xE0 (tiene un 0x22 0xB9
  //     comentado), o sea que el de referencia asume que las tensiones ya estan puestas por
  //     el refresco anterior. Aqui SI se manda en los dos casos a proposito: entre refresco y
  //     refresco el panel se manda a dormir (0x10 0x01), y no consta que al despertar
  //     conserve las tensiones. Es un byte de mas en el camino; si el parcial no saliera
  //     limpio, esta es la PRIMERA cosa que hay que probar a quitar.
  epdCmd1(0x22, 0xE0);
  epdCmd(0x20);
  // 200 ms de espera a que el panel levante las tensiones. Se bombea el boton mientras.
  // ★ T-ECHO PROJECT BUTTER: era un `delay(200)` a secas, o sea 200 ms mas de ventana
  //   ciega para los toques (y esta espera la paga CADA refresco).
  // ★★ VUELTA ATRAS DEL 2026-09-21 ★★ Aqui se probo a convertir esto en un sondeo de BUSY
  //   con tope corto (40 ms) y LA PANTALLA SE QUEDO EN NEGRO en la placa del operador: las
  //   tensiones NO estaban puestas cuando llegaba la orden de refrescar. El tope corto era
  //   una suposicion, no una medida. Se vuelve a los 200 ms de siempre y NO SE TOCA SIN
  //   MEDIRLO EN UNA PLACA (con esto ajustable desde el cable, cuando se haga).
  constexpr uint32_t kEsperaTensionesMs = 200;
  epdEsperaBusy(kEsperaTensionesMs);

  // (2) LOS DOS PLANOS, EN EL ORDEN DEL DE REFERENCIA: 0x26 (anterior) y despues 0x24
  //     (actual). ★ ESTE ORDEN ES EL QUE IMPORTA: antes se mandaba la misma imagen en los
  //     dos, que es justo lo que impide que el controlador detecte los cambios.
  const uint32_t fallosAntes = gErrTimeout + gErrSpi;
  const uint32_t amountAntes = gTxVistoAmount;
  const uint32_t bytesAntes = gBytesBitBang;
  epdCmdBuf(0x26, bufAnterior ? bufAnterior : bufActual, EPD_BUFSZ);
  epdCmdBuf(0x24, bufActual, EPD_BUFSZ);
  const uint32_t fallosAhora = (gErrTimeout + gErrSpi) - fallosAntes;

  // ★ TRAZA DE TALLER (2026-09-16): va agrupada en el MODO DIAGNOSTICO y se calla en modo
  //   TNC. Antes salia SIEMPRE, una por cada repintado (~1,5 s), y eso llenaba la consola
  //   del configurador y ensuciaba el puerto cuando el nodo trabaja de TNC. Ver diag.h.
  if (diagTrazaTaller()) {
    Serial.printf("PANTALLA: refresco %s: fallos=%lu amount0=%lu bytesBitBang=%lu transporte=%d "
                  "PSEL.SCK=0x%08lX CS=%d DC=%d SCK=%d MOSI=%d BUSY=%d\r\n",
                  parcial ? "PARCIAL" : "COMPLETO",
                  (unsigned long)fallosAhora,
                  (unsigned long)(gTxVistoAmount - amountAntes),
                  (unsigned long)(gBytesBitBang - bytesAntes), gTransporte,
                  (unsigned long)NRF_SPIM2->PSEL.SCK,
                  (int)digitalRead(PIN_CS), (int)digitalRead(PIN_DC),
                  (int)digitalRead(PIN_SCK), (int)digitalRead(PIN_MOSI),
                (int)digitalRead(PIN_BUSY));
  }

  // (3) ¡A PINTAR! La unica diferencia de comandos entre completo y parcial.
  if (parcial) {
    epdCmd1(0x22, kSecuenciaParcial);
    epdCmd(0x20);
    epdEsperaPintado(350, 1500);   // parcial: ~0,4 s
  } else {
    epdCmd1(0x22, kSecuenciaCompleto);
    epdCmd(0x20);
    epdEsperaPintado(2000, 4000);  // completo: ~1,9-2,7 s
  }

  epdCmd1(0x10, 0x01);   // deep sleep: la imagen se queda (tinta bistable)
  gAsleep = true;

  const uint32_t ms = millis() - t0;
  gMsUltimo = ms;
  if (parcial) {
    gMsUltimoParcial = ms;
    gNParciales++;
    gNParcialesSeguidos++;
  } else {
    gMsUltimoCompleto = ms;
    gNCompletos++;
    gNParcialesSeguidos = 0;
    gMsCompletoMs = t0;   // cuando EMPEZO este completo (millis() al entrar en epdRefresca)
    gHuboCompleto = true;
  }

  // (4) El plano de ahora pasa a ser el "anterior" para el proximo refresco. Solo si los
  //     datos han viajado bien: si hubo transferencias abortadas, lo que hay en el panel NO
  //     es lo que dice `gBuf`, y hay que volver a pintar en COMPLETO para no arrastrar el
  //     error a todos los parciales siguientes.
  if (fallosAhora == 0) {
    memcpy(gBufPrev, gBuf, EPD_BUFSZ);
    gPrevValido = true;
  } else {
    gPrevValido = false;
  }
}

// Refresco COMPLETO con lo que haya en gBuf.
//
// ★ NO SE ESPERA AL PIN BUSY A CIEGAS. Esta medido en ESTA unidad: BUSY no se mueve NUNCA
//   en los refrescos completos (0 altos en 39.565 muestras, con digitalRead, con el registro
//   IN y tambien con resistencia de subida) y la pantalla se refresca igual. Por eso la
//   espera es por TIEMPO (con el tope de BUSY solo como red de seguridad si algun dia
//   contestara). Un refresco completo real dura ~2 s.
void epdFullRefresh() {
  // ★ OJO: aqui NO se comprueba `gSpiListo`. Esa bandera solo se pone en modo SPIM2, y en
  //   modo bit-bang se quedaba en false: el refresco salia por la puerta de atras sin
  //   mandar ni un byte (sintoma: "bytesBitBang=0" y "ultimoRefresco=0ms"). El estado que
  //   de verdad importa es que los pines esten preparados, y de eso se encarga
  //   `epdPinesBitBang()` / `epdSpiConfigura()`, que ya se han llamado antes.
  //
  //   El completo manda la MISMA imagen en los dos planos (`bufAnterior = nullptr`): asi el
  //   controlador no tiene nada que comparar y hace el barrido entero, que es lo que quita
  //   los fantasmas. Es tambien el refresco del arranque, cuando todavia no se sabe que hay
  //   pintado.
  epdRefresca(false, nullptr, gBuf);
  gForzarCompleto = false;
}

// ★★ REFRESCO PARCIAL: manda el plano VIEJO y el NUEVO y el controlador solo mueve lo que
//    cambia. Si no hay un plano anterior fiable (primer refresco tras el arranque, o un
//    refresco anterior que salio mal), cae al completo sin pensarlo.
void epdPartialRefresh() {
  if (!gPrevValido) { epdFullRefresh(); return; }
  epdRefresca(true, gBufPrev, gBuf);
}

// ¿Toca forzar un completo para quitar fantasmas? Dos motivos: demasiados parciales
// seguidos, o demasiado tiempo sin un completo.
//
// ★ OJO CON LA CUENTA DEL TIEMPO: `gMsCompletoMs` es un `millis()` ABSOLUTO, que puede
//   valer 0 de verdad si el completo ocurre en el primer milisegundo tras el arranque. Por
//   eso el "¿ha habido algun completo?" se pregunta con `gHuboCompleto` y no comparando
//   `gMsCompletoMs` con 0: si no, el tope de tiempo podia no dispararse nunca o dispararse
//   de golpe por una resta contra un cero que no era "sin datos".
bool epdTocaCompleto() {
  if (gForzarCompleto) return true;
  if (!gPrevValido) return true;
  if (gNParcialesSeguidos >= kMaxParcialesSeguidos) return true;
  if (gNParcialesSeguidos > 0 && gHuboCompleto &&
      (millis() - gMsCompletoMs) > kMaxMsSinCompleto) return true;
  return false;
}

// Manda a la pantalla lo dibujado, si hace falta. Elige completo o parcial.
void epdFlush() {
  if (!gReady || !gDirty) return;
  gDirty = false;
  if (epdTocaCompleto()) {
    if (diagTrazaTaller()) {
      Serial.printf("PANTALLA: completo forzado (motivo: %s)\r\n",
                    gForzarCompleto ? "a peticion"
                    : (!gPrevValido ? "no hay plano anterior fiable"
                       : (gNParcialesSeguidos >= kMaxParcialesSeguidos ? "tope de parciales"
                          : "tope de tiempo")));
    }
    epdFullRefresh();
  } else {
    epdPartialRefresh();
  }
}

// ------------------------------------------------------------------ dibujo
// ★★ LA ORIENTACION (2026-09-14) — CONFIRMADA A OJO POR EL OPERADOR ★★
//
// El contenido se dibuja en coordenadas "logicas" (x a la derecha, y hacia abajo, como en
// cualquier pantalla) y aqui se traduce a la trama que entiende el panel.
//
// ★★ QUE SIGNIFICA CADA NUMERO, Y POR QUE ES ASI ★★
//
// El operador confirmo a ojo que la posicion de pie es **la 1**, y pidio que esa fuera la de
// fabrica. **El numero NO se ha renumerado a proposito**, y el motivo es importante:
//
//   La configuracion vive en la FLASH del nodo. Si se cambiara el significado de los
//   numeros, las unidades ya grabadas que tuvieran guardado un valor **se pondrian torcidas
//   solas** al actualizar el firmware (un `1` guardado pasaria de "de pie" a "girada 90").
//   Eso es una migracion silenciosa y es justo el tipo de trampa que hay que evitar.
//
// Asi que el significado se queda como esta (**1 = de pie, la de fabrica**) y la trampa se
// evita **diciendolo claro en la interfaz**: en el configurador web la opcion se llama
// "1 - de pie (por defecto)" y las otras "2 - girada 90", "3 - boca abajo (180)",
// "0 - girada 270". Ensenar el numero sin decir que significa es lo que confunde.
//
// La tabla de abajo traduce el numero a la transformacion de la trama. Las cuatro son
// rotaciones puras (ninguna espeja) y los `bitidx` del primer pixel salen distintos en las
// cuatro, comprobado en hardware:  R0=1206  R1=38208  R2=38391  R3=1791
int gRotacion = 1;   // 1 = DE PIE (posicion natural / de fabrica)
// La ultima rotacion que se llego a PINTAR. Sirve para dos cosas: (a) no repintar si la
// configuracion no ha cambiado, y (b) detectar el cambio en caliente cuando el usuario
// guarda `epdRotation` desde el configurador web o con `set epdRotation N`.
int gRotacionAplicada = -1;
// ★ PRUEBA DE QUE EL VALOR LLEGA AL MAPA DE PIXELES (2026-09-14).
// No basta con enseñar la etiqueta ni la variable global: hay que demostrar que el numero
// que se USA dentro de `px()` cambia de verdad. Aqui se guarda, para las CUATRO primeras
// rotaciones que se pinten, el valor con el que se calculo el primer pixel. Si salieran
// dos iguales, el bug estaria aqui y no en la formula.
uint32_t gBitIdxR[4] = {0, 0, 0, 0};
bool gBitIdxHecho[4] = {false, false, false, false};

inline void px(int x, int y, bool negro) {
  if (x < 0 || y < 0 || x >= EPD_W || y >= EPD_H) return;
  int xr = x, yr = y;
  switch (gRotacion) {
    case 0:  break;                                           // 0 = girada 270
    case 2:  xr = EPD_W - 1 - x; yr = EPD_H - 1 - y; break;   // 2 = boca abajo (180)
    case 3:  xr = EPD_W - 1 - y; yr = x;             break;   // 3 = girada 90
    default: xr = y;             yr = EPD_H - 1 - x; break;   // 1 = DE PIE (natural)
  }
  // El bit 7 es el primero de cada byte.
  const uint32_t bitidx = (uint32_t)yr * EPD_W + (uint32_t)xr;
  // ★ Aqui esta la prueba de que el valor llega: se guarda el bitidx del PRIMER pixel que
  //   se pinta con cada rotacion. Si dos rotaciones distintas guardaran el mismo numero,
  //   el fallo estaria en este punto y no en la formula.
  {
    const int r = (gRotacion >= 0 && gRotacion <= 3) ? gRotacion : 0;
    if (!gBitIdxHecho[r]) { gBitIdxHecho[r] = true; gBitIdxR[r] = bitidx; }
  }
  const int idx = (int)(bitidx >> 3);
  const uint8_t bit = (uint8_t)(0x80u >> (bitidx & 7u));
  if (negro) gBuf[idx] &= (uint8_t)~bit;   // 0 = negro
  else       gBuf[idx] |= bit;             // 1 = blanco
}

void clearBuf(bool blanco) {
  memset(gBuf, blanco ? 0xFF : 0x00, sizeof(gBuf));
}

void hLine(int x0, int x1, int y, int grosor = 1) {
  for (int g = 0; g < grosor; g++)
    for (int x = x0; x <= x1; x++) px(x, y + g, true);
}

// Un caracter de 5x7, al tamano que se pida (escala 2 = 10 puntos de ancho, legible
// de sobra en 200x200).
int drawChar(int x, int y, char c, int escala) {
  const uint8_t *g = EpdFont5x7::glyph(c);
  for (int col = 0; col < 5; col++) {
    const uint8_t bits = g[col];
    for (int fila = 0; fila < 7; fila++) {
      if (!(bits & (1u << fila))) continue;
      const int px0 = x + col * escala;
      const int py0 = y + fila * escala;
      for (int dy = 0; dy < escala; dy++)
        for (int dx = 0; dx < escala; dx++) px(px0 + dx, py0 + dy, true);
    }
  }
  return 6 * escala;   // 5 columnas + 1 de separacion
}

// Texto. Devuelve el ancho que ha ocupado.
int drawText(int x, int y, const char *s, int escala = 2) {
  int cx = x;
  for (const char *p = s; *p; p++) {
    if (*p == '\n') { y += 8 * escala; cx = x; continue; }
    cx += drawChar(cx, y, *p, escala);
  }
  return cx - x;
}

// Texto INVERSO (borra, deja blanco): para leer sobre una pastilla rellena de negro
// (el "TX" de la cabecera, el aviso de transmision). Es la misma fuente pero escribiendo
// pixeles BLANCOS en vez de negros.
int drawCharInv(int x, int y, char c, int escala) {
  const uint8_t *g = EpdFont5x7::glyph(c);
  for (int col = 0; col < 5; col++) {
    const uint8_t bits = g[col];
    for (int fila = 0; fila < 7; fila++) {
      if (!(bits & (1u << fila))) continue;
      const int px0 = x + col * escala;
      const int py0 = y + fila * escala;
      for (int dy = 0; dy < escala; dy++)
        for (int dx = 0; dx < escala; dx++) px(px0 + dx, py0 + dy, false);
    }
  }
  return 6 * escala;
}

int drawTextInv(int x, int y, const char *s, int escala = 2) {
  int cx = x;
  for (const char *p = s; *p; p++) {
    if (*p == '\n') { y += 8 * escala; cx = x; continue; }
    cx += drawCharInv(cx, y, *p, escala);
  }
  return cx - x;
}

// Ancho que va a ocupar el texto: el avance por caracter es 6*escala, igual que en
// drawText (antes esto no contaba la escala y el texto "centrado" salia descuadrado).
int textWidth(const char *s, int escala = 2) {
  if (!s) return 0;
  int anchoMax = 0, ancho = 0;
  for (const char *p = s; *p; p++) {
    if (*p == '\n') {
      if (ancho > anchoMax) anchoMax = ancho;
      ancho = 0;
      continue;
    }
    ancho += 6 * escala;
  }
  if (ancho > anchoMax) anchoMax = ancho;
  // La ultima columna de separacion no se "ve": se descuenta para que el centrado
  // quede de verdad centrado.
  return (anchoMax > 0) ? anchoMax - escala : 0;
}

// Texto centrado en la pantalla.
void drawTextCenter(int y, const char *s, int escala = 2) {
  int x = (EPD_W - textWidth(s, escala)) / 2;
  if (x < 0) x = 0;
  drawText(x, y, s, escala);
}

// Rectangulo RELLENO: para marcas de esquina. Se usa en la prueba de orientacion.
void relleno(int x0, int y0, int w, int h) {
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) px(x, y, true);
}

// Grueso vertical / horizontal: para dibujar una letra grande a mano.
void barraV(int x, int y0, int y1, int grosor) {
  for (int g = 0; g < grosor; g++)
    for (int y = y0; y <= y1; y++) px(x + g, y, true);
}
void barraH(int x0, int x1, int y, int grosor) {
  for (int g = 0; g < grosor; g++)
    for (int x = x0; x <= x1; x++) px(x, y + g, true);
}

// ★★ IMAGEN DE PRUEBA DE ORIENTACION (2026-09-14) ★★
//
// SE QUEDA EN LAS HERRAMIENTAS DE TALLER A PROPOSITO. Esta imagen ahorro horas: con texto
// centrado y simetrico, dos rotaciones distintas parecen la misma y las descripciones
// ("girado", "boca abajo") se vuelven ambiguas — que es exactamente lo que paso durante
// toda una tarde. Una "F" es **totalmente asimetrica**: se distingue sin ninguna duda en
// cual de las cuatro orientaciones esta, y ademas se sabe si esta ESPEJADA (cosa que una
// rotacion no puede provocar). Si algun dia hay que volver a ajustar la orientacion, se
// lanza con `epdrot N` y se mira la F.
//
// Referencia de como se ve la F **de pie** en la pantalla:
//     - el palo vertical va por la IZQUIERDA
//     - las dos barras salen hacia la DERECHA
//     - la barra de ARRIBA es la mas larga
// Y ademas hay dos marcas que no se pueden confundir:
//     - un CUADRO NEGRO RELLENO en la esquina superior izquierda
//     - una BARRA NEGRA a lo largo del borde inferior
// Si el cuadro no esta arriba a la izquierda, la imagen esta girada.
void dibujaPruebaOrientacion() {
  clearBuf(true);
  // Cuadro negro relleno en la esquina SUPERIOR IZQUIERDA (referencia inequivoca).
  relleno(8, 8, 44, 44);
  // Barra a lo largo del borde INFERIOR.
  barraH(8, EPD_W - 9, EPD_H - 16, 8);
  // La "F" grande, en el centro-derecha.
  const int fx = 80, fy = 50, alto = 110, grosor = 14;
  barraV(fx, fy, fy + alto, grosor);                 // palo vertical
  barraH(fx, fx + 84, fy, grosor);                   // barra de arriba (larga)
  barraH(fx, fx + 56, fy + 46, grosor);              // barra de en medio (corta)
  // El numero de rotacion, para no depender de la etiqueta: si esto sale legible, ya se
  // sabe que rotacion esta puesta.
  char b[8];
  snprintf(b, sizeof(b), "R%d", gRotacion);
  drawText(8, EPD_H - 48, b, 2);
}

// ★ Dibuja en `gBuf` lo que toque AHORA (el aviso reciente si lo hay, o la escena del
//   carrusel) y deja `gDirty` puesto. Es el UNICO sitio donde se decide que se ve.
//   Lo usan `displayRefresh()` (el bucle normal) y la prueba del carrusel del comando
//   `epdparcial` (herramienta de taller): asi lo que se prueba es exactamente lo mismo que
//   se vera luego en el carrusel automatico del Paso 2, y no una copia que puede divergir.
//   Se define al final del fichero, junto a `pintaEstado/Radio/Ultimo`, que son de este
//   mismo espacio de nombres anonimo.
void dibujaEscena();

// Voltaje de bateria que se dibuja y que entra en la huella del contenido. Se define mas
// abajo (junto a `huellaContenido()`), pero `pintaEstado()` lo usa antes.
uint16_t bateriaMv();

// Saca la huella del contenido y sus ingredientes (se define junto a `huellaContenido()`).
void epdHuellaTexto(char *out, size_t n);

}  // namespace

// Sonda cruda del periferico SPI (herramienta de taller, comando "epdsonda" del USB).
// Se declara AQUI, en el espacio de nombres global, y no dentro del anonimo: si se declara
// dentro, el enlazador busca una version interna que no existe y no compila.
// OJO: `dibujaPruebaOrientacion()` NO se declara aqui. Ya esta definida mas arriba, dentro
// del espacio de nombres anonimo, y declararla tambien fuera crea una segunda version con
// enlazado externo: el compilador no sabe cual usar y da "call is ambiguous".
void epdSonda(char *out, size_t n);
void epdSondaPines(char *out, size_t n);
void epdVolcadoPines(char *out, size_t n);

// Gancho del boton para las esperas del driver (T-Echo Project Butter). La variable
// `gPumpBoton` vive arriba, dentro del espacio de nombres anonimo (el driver la usa en
// sus esperas); la funcion que la pone tiene que estar AQUI FUERA, en el espacio global,
// o el enlazador busca una version interna que no existe (es el mismo caso que la sonda
// de arriba). Ver la nota del gancho, junto a epdXferSpim().
void displaySetPumpBoton(void (*fn)(void)) { gPumpBoton = fn; }

// ============================================================================
//  Interfaz de pantalla del firmware (las mismas funciones que display.h)
// ============================================================================

// Estado que nos va dando el resto del firmware, para poder pintarlo.
namespace {
DigiConfig *gCfg = nullptr;
SensorReadings gSens{};
uint32_t gRx = 0, gTx = 0, gDg = 0;
char gLinea1[40] = "";      // "ultima recibida" o un aviso
char gLinea2[40] = "";
uint32_t gLineaMs = 0;
// ★★ CUANTO DURA UN AVISO: 1,5 S, Y EN UN SOLO REFRESCO (2026-09-15) ★★
//
// ANTES esto eran 8000 ms y el aviso SUSTITUIA a la escena entera (`dibujaEscena` pintaba
// solo el aviso). Eso obligaba a DOS refrescos de tinta por cada RX/TX (cada uno ~1,5 s):
//   1) llega el RX  -> el aviso entra en la huella -> refresco A (solo el aviso)
//   2) a los 8 s    -> el aviso caduca (la linea pasa a vacia) -> la huella cambia ->
//                      refresco B (vuelve la escena)
// Resultado medido por el operador: "un solo RX ocupa la pantalla ~3 segundos" y, con
// trafico, el panel pintando casi todo el rato (parecia que el nodo "se quedaba tonto").
//
// AHORA: el aviso se dibuja ENCIMA de la escena (que se sigue pintando debajo) y dura
// 1500 ms. Sigue habiendo DOS refrescos --uno cuando aparece y otro cuando se va, porque en
// tinta un pixel no se borra solo-- pero deja de haber un estado intermedio en el que la
// pantalla se queda SOLO con el aviso: el segundo refresco ya es "la escena, limpia".
// Ver el detalle y la cuenta exacta de refrescos en `dibujaEscena()`.
constexpr uint32_t kAvisoMs = 1500;
// Los avisos que NO son de trafico (bateria baja al arrancar, "Solo sin cable USB", textos
// del menu) se quedan como estaban: 8 s. Son pocos y hay que poder LEERLOS.
constexpr uint32_t kLineaMs = 8000;
uint32_t gUltimoPintado = 0;
uint32_t gUltimoToqueMs = 0;     // ultima vez que se pulso un boton (para aplazar el repintado)
constexpr uint32_t kDebounceToqueMs = 400;  // si tocas antes de esto, no repinta (se encola)
// ★★ T-ECHO PROJECT BUTTER (2026-09-15): EL APLAZAMIENTO ES SOLO PARA EL TACTIL ★★
// `gUltimoToqueAgrupaMs` es "se esta tocando seguido, agrupa el repintado". Solo lo
// escriben el TACTIL CAPACITIVO (menuNavigate / displayNextScene(true)), que es el unico
// que puede ir rapido: no tiene ventana de doble toque, asi que se pueden encadenar
// toques cada ~150 ms y no tiene sentido pagar 1,5 s de panel por cada uno.
// El BOTON FISICO ya no lo escribe: su toque corto solo existe cuando han pasado 600 ms
// desde que solto (la ventana del doble), asi que nunca llega en rafaga y aplazarle el
// repintado 400 ms era LATENCIA PURA (medida: 400 ms de nada antes de empezar a pintar).
// ★ VUELTA ATRAS DEL 2026-09-21: de 250 a 400 ms otra vez. Se habia bajado a 250 ms dando
//   por hecho que el refresco iba a ser mas rapido (se le habian quitado dos esperas), pero
//   esas esperas RESULTARON NECESARIAS y se han restaurado: sin ellas la pantalla se queda
//   en negro. Asi que la ventana vuelve a su valor, que es el que estaba medido y probado.
constexpr uint32_t kAgrupaToquesMs = 400;   // solo tactil: espera a que dejes de tocar
uint32_t gUltimoToqueAgrupaMs = 0;
constexpr uint8_t kNumEscenas = 6;   // Inicio, SOTA, Mensajes, Estaciones, Ultimos RX, Ultimos TX
uint8_t gEscena = 0;

// ===========================================================================
//  ★★ "FIJAR COORDS" EN LA PANTALLA DE TINTA (2026-09-15) ★★
// ===========================================================================
// QUE HACE: el operador elige "Fijar coords" en el menu y el nodo hace TODO el
// trabajo: enciende el GPS, espera a que fije, deja que la posicion se asiente
// (varias lecturas seguidas), guarda la posicion como posicion FIJA del aparato y
// vuelve a apagar el GPS. Es la via para dejar un repetidor publicado en el mapa
// sin escribir numeros a mano ni depender del configurador web.
//
// ★ LA SESION NO SE PROGRAMA AQUI: la lleva el RASTREADOR
//   (trackerSetCoordsStart/Tick, ver tracker.h y tracker.cpp), que es quien manda
//   sobre el GPS. Esta pantalla solo 1) da la orden, 2) ENSENA lo que esta pasando
//   y 3) guarda la posicion cuando el rastreador la da por buena
//   (displaySaveCoords, mas abajo). En la OLED ese mismo camino ya funcionaba.
//
// ★ POR QUE HACE FALTA UNA PANTALLA PROPIA Y NO UN AVISO: un aviso de los de
//   `pintaAviso()` caduca a los 8 s y la captura dura MINUTOS (fijar + 20
//   lecturas). Ademas el bucle principal (main.cpp) ya manda el progreso
//   ("GPS 3/20") por displayPopup() en CADA muestra, y en tinta eso serian ~20
//   refrescos de 1,5 s (30 segundos de panel pintando) mas un texto que se pisa a
//   si mismo. Por eso:
//     - el progreso se pinta como PANTALLA COMPLETA (no como aviso encima), y
//     - solo CAMBIA en los escalones que se ven (0, 25, 50, 75, 100 %) porque el
//       repintado va por la huella del contenido: si el texto no cambia, no se
//       manda nada al panel,
//     - y los avisos "GPS n/N" del bucle se descartan mientras dura la sesion
//       (ver displayPopup), que es lo unico que quedaba por atar.
//
// ★ COMO SE SALE: la pantalla NO se cierra sola NUNCA por tiempo.
//   - Mientras BUSCA o ASIENTA (la captura puede tardar lo que necesite): un toque
//     de boton CANCELA la sesion, apaga el GPS si lo encendio ella y vuelve al
//     menu. Era la condicion que puso el operador: "ya que puede tardar lo que
//     tarde, que el usuario pueda salir de ahi con un boton".
//   - Cuando ya hay resultado (guardado o error), el primer toque quita la
//     pantalla y vuelve al menu/carrusel. El panel es bistable, asi que el
//     resultado se queda a la vista hasta que el operador quiera.
//
// ★ EL GPS LO ENCIENDE Y LO APAGA LA SESION (2026-09-15), no este fichero: ver
//   trackerSetCoordsStart() y el guardia de gpsManage() en tracker.cpp. Por eso
//   aqui ya NO hay ninguna comprobacion previa del tipo "activa GPS en repetidor":
//   en modo repetidor la captura funciona igual, sin tocar nada.
enum {
  COORDS_OCULTA = 0,   // no hay sesion (o ya se ha cerrado la pantalla)
  COORDS_BUSCANDO,
  COORDS_ASENTANDO,
  COORDS_GUARDADO,
  COORDS_FALLO,        // no se pudo guardar en la configuracion
};
uint8_t gCoordsPantalla = COORDS_OCULTA;
// Ultima posicion GUARDADA por la sesion (solo para poder ensenarla al terminar:
// el operador tiene que ver lo que se ha guardado, no un "vale" a secas).
double gCoordsLat = 0.0, gCoordsLon = 0.0;
// millis() en que se lanzo la sesion: sirve para NO confundir el toque con el que
// el operador acaba de elegir "Fijar coords" en el menu con un toque de cancelar.
uint32_t gCoordsInicioMs = 0;

void coordsPantallaInicia() {
  gCoordsPantalla = COORDS_BUSCANDO;
  gCoordsInicioMs = millis();
  gDirty = true;
}
bool coordsPantallaActiva() { return gCoordsPantalla != COORDS_OCULTA; }
void coordsPantallaCierra() {
  if (gCoordsPantalla == COORDS_OCULTA) return;
  gCoordsPantalla = COORDS_OCULTA;
  gDirty = true;
}

// ---------------------------------------------------------------------------
//  HISTORIAL RECIENTE RX / TX (2026-09-15): la OLED tenia escenas de "ULTIMOS RX" y
//  "ULTIMOS TX" listando varias entradas. La tinta las necesita con su PROPIO registro
//  (las listas de la OLED no se compilan aqui), asi que se mantiene un anillo pequeno que
//  alimentan los hooks displayNoteRx/Digi/Tx. No se borra entre escenas.
// ---------------------------------------------------------------------------
struct RxLog { char call[12]; float rssi; float snr; uint32_t ms; };
struct TxLog { char what[20]; uint32_t ms; };
constexpr int kRxLogMax = 6;
constexpr int kTxLogMax = 6;
RxLog gRxLog[kRxLogMax]; int gRxLogN = 0; int gRxLogHead = 0;
TxLog gTxLog[kTxLogMax]; int gTxLogN = 0; int gTxLogHead = 0;

void rxLogPush(const char *call, float rssi, float snr) {
  RxLog &e = gRxLog[gRxLogHead];
  snprintf(e.call, sizeof(e.call), call ? "%s" : "?", call ? call : "?");
  e.rssi = rssi; e.snr = snr; e.ms = millis();
  gRxLogHead = (gRxLogHead + 1) % kRxLogMax;
  if (gRxLogN < kRxLogMax) gRxLogN++;
}
void txLogPush(const char *what) {
  TxLog &e = gTxLog[gTxLogHead];
  snprintf(e.what, sizeof(e.what), "%s", what ? what : "");
  e.ms = millis();
  gTxLogHead = (gTxLogHead + 1) % kTxLogMax;
  if (gTxLogN < kTxLogMax) gTxLogN++;
}

// ---------------------------------------------------------------------------
//  MENSAJES APRS RECIBIDOS (2026-09-25, a peticion del operador): igual de
//  anillo que el RX/TX de arriba, pero guardando el CUERPO del mensaje, no
//  solo la metadata de la radio. Antes de esto un mensaje solo se veia unos
//  segundos en el aviso emergente (displayPopup, recortado a 40 caracteres) y
//  se perdia en cuanto llegaba el siguiente evento; ahora queda en su propia
//  escena del carrusel (pintaMensajes, escena 2). Lo alimenta
//  displayNoteMsg(), llamado desde aprs.cpp (handleRemoteMessage) justo donde
//  ya se llamaba a displayPopup() con el mismo mensaje.
// ---------------------------------------------------------------------------
struct MsgLog { char call[12]; char text[100]; uint32_t ms; };
constexpr int kMsgLogMax = 5;
MsgLog gMsgLog[kMsgLogMax]; int gMsgLogN = 0; int gMsgLogHead = 0;
// ★ ESTADO DEL ULTIMO SPOT SOTA (2026-09-27): lo ensenia la escena SOTA ("APRS status").
//   Cinco estados en pantalla, pedidos asi por el operador:
//     Sending  -- enviado, esperando (tambien con el acuse ya recibido: el acuse solo
//                 dice que la pasarela tiene el mensaje, no que el spot este publicado);
//     Spotted / Dupe / Error -- lo que CONTESTA APRS2SOTA ("Spotted: ...", "Dupe: ...",
//                 "Error: ..." o "Sorry: ...", ver Aprs2Sota_Info.php en sotaspots.co.uk);
//     Not sent -- la radio no pudo emitir, o se agotaron los reintentos sin acuse.
//   Un mensaje rechazado (REJ) cuenta como Error. Lo escriben sotaEnviarSpot(),
//   displayNoteMsgEnd() y displayNoteMsg(). Solo RAM: tras un reinicio no hay spot.
enum SpotEstado : uint8_t { SPOT_NINGUNO, SPOT_ENVIANDO, SPOT_NO_ENVIADO,
                            SPOT_SPOTTED, SPOT_DUPE, SPOT_ERROR };
SpotEstado gSpot = SPOT_NINGUNO;
constexpr const char *kSotaPasarela = "APRS2SOTA";

// La respuesta de la pasarela, por su primera palabra. SPOT_NINGUNO = no es una de
// las conocidas (la pasarela puede mandar varios mensajes: los de informacion no
// deben borrar un "Spotted" ya recibido).
SpotEstado spotDeRespuesta(const char *t) {
  if (!t) return SPOT_NINGUNO;
  if (!strncasecmp(t, "Spotted", 7)) return SPOT_SPOTTED;
  if (!strncasecmp(t, "Dupe", 4)) return SPOT_DUPE;
  if (!strncasecmp(t, "Error", 5) || !strncasecmp(t, "Sorry", 5)) return SPOT_ERROR;
  return SPOT_NINGUNO;
}

// Mensajes recibidos desde el arranque (el anillo de arriba solo guarda los 5 ultimos).
// Lo ensenia la escena de inicio.
uint32_t gMsgTotal = 0;

void msgLogPush(const char *call, const char *text) {
  MsgLog &e = gMsgLog[gMsgLogHead];
  snprintf(e.call, sizeof(e.call), "%s", (call && call[0]) ? call : "?");
  // ★ 2026-09-25 (arreglo, primer flasheo del operador): recorta desde el
  //   primer "{" (numero de mensaje APRS101, ver aprsSendMessage) y quita
  //   espacios sobrantes en los dos extremos -- TODO CON PUNTEROS, sin
  //   String. La version anterior usaba String aqui y en
  //   dibujaTextoEnvuelto(): con un mensaje largo ("...blabla double ligne
  //   triple ligne") el operador solo veia la PRIMERA linea en pantalla. La
  //   sospecha, coherente con lo que se ve en el resto del fichero: String
  //   tira de malloc/free en cada substring()/trim(), y en este nodo (BLE +
  //   radio + GPS + framebuffers a la vez) el hueco de RAM libre puede no
  //   dar para eso -- String falla EN SILENCIO (se queda vacia) en vez de
  //   avisar. rxLogPush/txLogPush, un poco mas arriba, ya evitaban String
  //   por esto mismo; esta funcion y dibujaTextoEnvuelto() eran las UNICAS
  //   dos de todo el fichero que no lo hacian.
  const char *src = text ? text : "";
  const char *brace = strchr(src, '{');
  size_t n = brace ? (size_t)(brace - src) : strlen(src);
  while (n > 0 && src[n - 1] == ' ') n--;          // espacios al final
  const char *start = src;
  while (n > 0 && *start == ' ') { start++; n--; }  // espacios al principio
  if (n >= sizeof(e.text)) n = sizeof(e.text) - 1;
  memcpy(e.text, start, n);
  e.text[n] = '\0';
  e.ms = millis();
  gMsgLogHead = (gMsgLogHead + 1) % kMsgLogMax;
  if (gMsgLogN < kMsgLogMax) gMsgLogN++;
  gMsgTotal++;
}

// ---------------------------------------------------------------------------
//  ★★ PASO 2: EL CARRUSEL AUTOMATICO (2026-09-15) ★★
//
//  Con el refresco parcial ya hecho (1,5 s y SIN parpadeo), cambiar de pantalla solo de vez
//  en cuando es comodo. Antes NO lo era: cada cambio era un parpadeo de 2 s.
//
//  ★ POR QUE NO SE COPIA EL NUMERO DE LA OLED: alli el auto-avance es de 4 SEGUNDOS
//  (`display.cpp`, kSceneAutoMs), porque su pantalla se redibuja instantaneamente. Aqui cada
//  cambio se PAGA en el panel: 1,5 s de parcial y, cada cierto numero, un completo de 3,1 s
//  para limpiar fantasmas (el HANDOVER pide forzar completos de vez en cuando). Ademas, el
//  firmware de referencia del T-Echo refresca cada 15 s porque **la pantalla comparte
//  alimentacion con el GPS** (`periph_pwr.c`): refrescar muy seguido obliga a tener el GPS
//  encendido. Por eso aqui el carrusel va LENTO a proposito: 45 s.
//
//  ★ NO SE REPINTA SI NO HA CAMBIADO NADA: de eso se encarga la huella del contenido
//  (`huellaContenido()`, ver mas abajo) y el limite `kRepintadoMaxMs`. El carrusel solo
//  PIDE el cambio de escena; el que decide pintar es el mismo camino de siempre.
//
//  ★ PAUSA AL TOCAR EL BOTON (mismo patron que la OLED, adaptado a esta pantalla): si acabas
//  de elegir una pantalla con el boton, el carrusel se calla 2,5 MINUTOS para que no te
//  cambie la pantalla mientras la estas leyendo. Cada pulsacion renueva la pausa. El numero
//  de la OLED (15 s) es demasiado corto para una pantalla que tarda 1,5 s en cambiar.
//
//  ★ SE APAGA CON EL AJUSTE QUE YA EXISTE (`sceneAutoAdvance`): hasta ahora ese ajuste no
//  hacia NADA en la pantalla de tinta electronica (es el campo de la OLED), asi que la web
//  ensenaba un interruptor inerte. Con esto pasa a mandar de verdad.
// ---------------------------------------------------------------------------
constexpr uint32_t kEscenaAutoMs = 45000;         // 45 s por escena
constexpr uint32_t kPausaTrasBotonMs = 150000;    // 2,5 min sin carrusel tras una pulsacion
uint32_t gUltimoCambioEscenaMs = 0;
uint32_t gCarruselPausadoHasta = 0;
bool gAutoAvance = false;      // lo pone la config (sceneAutoAdvance) en cada refresco
}  // namespace

void displayBindConfig(DigiConfig *cfg) {
  gCfg = cfg;
  // ★ La rotacion viene de la CONFIGURACION PERSISTENTE (config.h: `epdRotation`), no de una
  //   constante: el panel puede ir montado con distinta orientacion segun la unidad, y
  //   entonces la elige el usuario desde el configurador web o con `set epdRotation N`.
  if (gCfg) {
    const uint8_t r = gCfg->epdRotation;
    gRotacion = (r <= 3) ? (int)r : 0;
    gRotacionAplicada = -1;   // fuerza a repintar con la rotacion nueva
  }
}

// Indicativo del operador SOLO (sin el SSID, p.ej. "N0CALL" y no "N0CALL-3"). Peticion del
// operador (2026-09-15): en la pantalla se muestra su indicativo, sin el sufijo numerico.
char gCallSinSSID[16];
const char *callSinSSID() {
  const char *raw = (gCfg && gCfg->callsign[0]) ? gCfg->callsign : "NOCALL";
  int n = 0;
  for (; raw[n] && raw[n] != '-' && n < (int)sizeof(gCallSinSSID) - 1; n++)
    gCallSinSSID[n] = raw[n];
  gCallSinSSID[n] = '\0';
  return gCallSinSSID;
}

// ---------------------------------------------------------------------------
//  SPLASH DE ARRANQUE (2026-09-15) — portado de la OLED y adaptado a la 200x200:
//  logo de antena + indicativo + nombre del sistema + version + modo, y una BARRA DE
//  PROGRESO abajo que se va llenando mientras dura (~6 s). Es la "animacion" de la
//  OLED: en tinta solo se puede redibujar cada ~1,5 s (el tiempo del panel), asi que
//  la barra avanza en unos pocos saltos, suficiente para que se vea crecer.
//  `pct` va de 0 a 100.
void dibujaSplash(int pct) {
  clearBuf(true);
  // Marca del firmware arriba (asustada que el splash aluda al firmware, no a un garabato).
  drawTextCenter(40, APP_NAME_STR, 1);   // platformio.ini (-DAPP_NAME_STR)
  hLine(50, EPD_W - 50, 56, 1);

  // El indicativo, EN GRANDE y CENTRADO: SOLO el indicativo del operador, sin el SSID
  // (callSinSSID). Cada persona instalara el firmware y pondra el suyo.
  const char *call = callSinSSID();
  drawTextCenter(72, call, 3);

  char b[36];
  snprintf(b, sizeof(b), "v%s  %s", APP_VERSION_STR, __DATE__);
  drawTextCenter(112, b, 1);
  {
    uint8_t m = (uint8_t)(gCfg ? gCfg->mode : 0);
    const char *mn = (m == 1) ? "Tracker" : ((m == 2) ? "Digi+Tracker" : "Digipeater");
    snprintf(b, sizeof(b), "Mode %d: %s", (int)m, mn);
    drawTextCenter(132, b, 1);
  }

  // ★★ EL MENSAJE DE DORMIDO, TAMBIEN EN EL ARRANQUE (peticion del operador, 2026-09-16) ★★
  //   El mismo texto y el mismo tamaño que la pantalla que se queda fija al mandar el nodo
  //   a dormir (`displaySleepScene`: `sleepMsg` en escala 2), puesto ENTRE la linea del modo
  //   y la barra de progreso, centrado en ese hueco:
  //     - la linea del modo va en y=132 y una letra de escala 1 mide 7 px -> acaba en 139;
  //     - la barra de progreso empieza en y=174;
  //     - luego el hueco util es 140..173, y su centro cae en 157;
  //     - una linea de escala 2 mide 14 px (7*2), asi que su parte de ARRIBA va en 157-7=150.
  //   Y 150 es, casualidad util, la MISMA y que usa la pantalla de dormido: el mensaje sale
  //   en el mismo sitio de la pantalla en las dos, asi que se reconoce de un vistazo.
  //   Si no hay mensaje configurado (vacio), no se pinta nada y el splash queda como estaba.
  //   Se dibuja con `drawTextCenter`, que ya descuenta la ultima columna de separacion: el
  //   centrado es de verdad. Con letras de 12 px de ancho salen ~16 caracteres; mas largo se
  //   recorta por los lados, igual que en la pantalla de dormido.
  if (gCfg && gCfg->sleepMsg[0]) {
    drawTextCenter(150, gCfg->sleepMsg, 2);
  }

  // Barra de progreso: enmarcada y rellena segun pct (0..100). Es la animacion.
  const int by = 174, bw = 150, bh = 8;
  barraH(25, 25 + bw, by, 1); barraH(25, 25 + bw, by + bh, 1);
  barraV(25, by, by + bh, 1); barraV(25 + bw, by, by + bh, 1);
  if (pct > 0) relleno(25 + 1, by + 1, (bw - 2) * pct / 100, bh - 2);
}

// ---------------------------------------------------------------------------
//  ENSAYO VISUAL DE ARRANQUE: el SPLASH con su barra animada (~6 s) y despues el
//  estado del nodo.
//
//  El panel se enciende desde aqui a proposito, cuando el nodo YA lleva 6 segundos
//  andando: asi el USB esta enumerado y, pase lo que pase con la pantalla, se puede
//  seguir hablando con el nodo y volver a grabarlo por software ("dfu confirm").
//  El banco de pruebas hacia lo mismo y por eso nunca se quedo mudo.
// ---------------------------------------------------------------------------
void displayArrancaPantalla() {
  // ★ TRAZA DE ARRANQUE (herramienta de taller). Este texto sale por el USB en cuanto el
  //   nodo lleva 6 s andando, asi que SI se ve al abrir el puerto.
  //   ★ Va con `diagTrazaArranque()` (2026-09-16): se sigue viendo siempre (el modo
  //   diagnostico esta apagado al arrancar, asi que no puede depender de el), pero NO se
  //   le cuela a un programa host: en modo TNC el puerto es suyo. Ver diag.h.
  if (diagTrazaArranque()) {
    Serial.printf("PANTALLA: displayArrancaPantalla() entra (gReady=%d transporte=%d)\r\n",
                  gReady ? 1 : 0, gTransporte);
    Serial.flush();
  }
  if (gReady) return;

  // Corriente: el MOSFET de periferia y el regulador de 3,3 V. La variante ya los
  // enciende en initVariant(), pero se reafirman aqui (no cuesta nada y deja claro
  // de que depende la pantalla).
  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_PWR_EN, HIGH);
  pinMode(PIN_REG_EN, OUTPUT);
  digitalWrite(PIN_REG_EN, HIGH);
  // P1.11 NO SE TOCA (ver la nota (e) de la cabecera).
  delay(50);

  // CS y DC como salidas (en reposo, altos) y BUSY como entrada sin pull.
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_BUSY, INPUT);

  // El bit-bang pone los pines a mano: no depende de ningun periferico (ver la nota
  // larga del principio). ★ ORDEN IMPORTANTE: si algun dia se quiere ENSEÑAR como esta
  // SPIM2, hay que mirarlo ANTES de dejarselo al GPIO, porque `epdPinesBitBang()`
  // desconecta a proposito el PSEL del periferico.
  if (gTransporte == 1) {
    epdSpiConfigura();
    if (diagTrazaArranque()) {
      Serial.printf("PANTALLA: transporte=SPIM2 PSEL.SCK=P0.%d PSEL.MOSI=P0.%d ENABLE=%u\r\n",
                    (int)(NRF_SPIM2->PSEL.SCK & 0x1F), (int)(NRF_SPIM2->PSEL.MOSI & 0x1F),
                    (unsigned)(NRF_SPIM2->ENABLE & 1u));
    }
  } else {
    epdPinesBitBang();
    if (diagTrazaArranque()) {
      Serial.println("PANTALLA: transporte=bit-bang (SPIM2 APAGADO y desenganchado de los pines)");
    }
  }
  epdPinesReposo();

  // (1) SPLASH con su barra de progreso animada (~6 s en total). El primer refresco es
  //     COMPLETO (aun no hay plano anterior) y los siguientes PARCIALES; como entre paso y
  //     paso solo cambia la barra, el panel solo mueve la barra (poco fantasma). El tiempo
  //     que tarda el panel en escribir (~1,5 s por parcial) marca el ritmo, asi que con 4
  //     pasos queda en torno a 6 s.
  gReady = true;        // a partir de aqui epdFlush ya manda de verdad
  displayBacklightKick();   // luz encendida durante el splash
  {
    const int pasos[] = {0, 34, 67, 100};
    for (int i = 0; i < 4; i++) {
      dibujaSplash(pasos[i]);
      gDirty = true;
      epdFlush();       // el primer refuerzo es COMPLETO, los siguientes parciales
    }
  }
  displayBacklightTick(millis());  // que apague cuando toque (splash ya terminado)

  // (2) Despues del splash, se deja la escena de Estado lista, que es la que se ve casi
  //     siempre. El carrusel la pinta en su primera pasada (ya no hay texto de prueba:
  //     "PANTALLA OK / T-Echo Plus" era de cuando se verificaba que la pantalla pintaba,
  //     y esta demostrado que funciona).
  gEscena = 0;
  gDirty = true;
  gUltimoPintado = 0;     // fuerza a que pinte Estado de inmediato
  gAsleep = false;

  if (diagTrazaArranque()) {
    Serial.printf("PANTALLA: arrancada. ultimo refresco %lums, timeouts=%lu\r\n",
                  (unsigned long)gMsUltimo, (unsigned long)gErrTimeout);
  }
}

// Sustitutos vacios: la parte de alimentacion de la pantalla va toda en
// displayArrancaPantalla(), para que el USB este vivo antes de tocar el panel.
void displayInit() {}
void displayInitTrasRadio() {}

// Diagnostico de la pantalla en una linea, a peticion (comando "epd" del USB).
// Existe porque los mensajes del arranque no se ven: cuando se abre el puerto, el
// nRF52 se reinicia y el arranque ya ha pasado.
void displayDiagTexto(char *out, size_t n) {
  if (!out || !n) return;
  // Si la primera letra que trae el buffer es 'S', el que llama quiere la SONDA del
  // periferico (herramienta de taller), no el resumen. Ver cli.cpp ("epdsonda").
  if (out[0] == 'S') { epdSonda(out, n); return; }
  // 'T' = cambiar de transporte (ver cli.cpp, "epdtrans"): T0 = bit-bang, T1 = SPIM2.
  if (out[0] == 'T') { gTransporte = (out[1] == '1') ? 1 : 0; return; }
  // 'P' = sonda de PINES: ¿el bit-bang puede gobernar de verdad SCK/MOSI/CS/DC/RST?
  if (out[0] == 'P') { epdSondaPines(out, n); return; }
  // 'G' = ROTACION en caliente: out[1] = '0'..'3'. Ver gRotacion/px().
  // Herramienta de taller: permite enderezar la imagen sin volver a grabar.
  if (out[0] == 'G') {
    const int r = out[1] - '0';
    gRotacion = (r >= 0 && r <= 3) ? r : 0;
    epdPinesBitBang();
    epdPinesReposo();
    gReady = true;
    dibujaPruebaOrientacion();
    epdFullRefresh();
    // ★ Se informa del valor que se ha aplicado DE VERDAD, no solo de la etiqueta pintada:
    //   si la 0 y la 1 se vieran igual, aqui tiene que verse por que.
    snprintf(out, n,
             "EPD rotacion APLICADA=%d | bitidx del primer pixel con cada rotacion: "
             "R0=%lu R1=%lu R2=%lu R3=%lu (tienen que ser CUATRO numeros distintos) | "
             "la F tiene que salir con el palo a la IZQUIERDA y las barras a la DERECHA",
             gRotacion,
             (unsigned long)gBitIdxR[0], (unsigned long)gBitIdxR[1],
             (unsigned long)gBitIdxR[2], (unsigned long)gBitIdxR[3]);
    return;
  }
  // 'R' = REPINTAR ahora mismo el estado del nodo, y decir cuantos bytes ha movido.
  // Herramienta de taller: sirve para lanzar un refresco a peticion y ver el resultado sin
  // depender de lo que pasó en el arranque (que no se ve: abrir el puerto reinicia la placa).
  //
  // ★★ ES LA PRUEBA DEL REFRESCO PARCIAL (Paso 1, 2026-09-15) ★★
  // La primera vez que se llama en un arranque el refresco es COMPLETO (todavia no se sabe
  // que hay pintado). La SEGUNDA vez ya hay plano anterior fiable, asi que el mismo comando
  // hace un PARCIAL: el operador ve la pantalla cambiar sin el parpadeo largo de 2 s. La
  // respuesta dice cual de los dos ha hecho y cuanto ha tardado, que es lo que hay que
  // apuntar en la bitacora.
  if (out[0] == 'R') {
    const uint32_t antes = gBytesBitBang;
    const uint32_t nCompletosAntes = gNCompletos;
    const uint32_t nParcialesAntes = gNParciales;
    const uint32_t t0 = millis();
    epdPinesBitBang();
    epdPinesReposo();
    gReady = true;
    clearBuf(true);
    drawText(6, 6, callSinSSID(), 3);
    hLine(4, EPD_W - 4, 34, 2);
    drawTextCenter(48, "DISPLAY OK", 3);
    drawTextCenter(84, "T-Echo Plus", 2);
    // Prueba visible de que la pantalla se ha repintado: la hora (en segundos desde el
    // arranque) cambia en cada llamada, asi que el parcial tiene pixeles que mover.
    {
      char b[32];
      snprintf(b, sizeof(b), "refresh %lu", (unsigned long)(gNCompletos + gNParciales + 1));
      drawTextCenter(112, b, 2);
      snprintf(b, sizeof(b), "t=%lus", (unsigned long)(millis() / 1000));
      drawTextCenter(140, b, 2);
    }
    drawTextCenter(176, "N0CALL-3  iGate", 1);
    gDirty = true;
    epdFlush();
    const bool fueParcial = (gNParciales > nParcialesAntes) && (gNCompletos == nCompletosAntes);
    snprintf(out, n,
             "EPD repintado %s: bytes=%lu fallos=%lu ms=%lu | parciales=%lu (ultimo %lums) "
             "completos=%lu (ultimo %lums) seguidos=%lu/%lu | PSEL.SCK=0x%08lX CS=%d DC=%d SCK=%d MOSI=%d",
             fueParcial ? "PARCIAL (no debe parpadear)" : "COMPLETO (parpadea, es normal)",
             (unsigned long)(gBytesBitBang - antes),
             (unsigned long)(gErrTimeout + gErrSpi), (unsigned long)(millis() - t0),
             (unsigned long)gNParciales, (unsigned long)gMsUltimoParcial,
             (unsigned long)gNCompletos, (unsigned long)gMsUltimoCompleto,
             (unsigned long)gNParcialesSeguidos, (unsigned long)kMaxParcialesSeguidos,
             (unsigned long)NRF_SPIM2->PSEL.SCK,
             (int)digitalRead(PIN_CS), (int)digitalRead(PIN_DC),
             (int)digitalRead(PIN_SCK), (int)digitalRead(PIN_MOSI));
    return;
  }
  // 'V' = VOLCADO de los registros PIN_CNF de cada pin (herramienta de taller).
  if (out[0] == 'V') { epdVolcadoPines(out, n); return; }
  // 'W' = ¿MUEVE EL BIT-BANG LOS PINES? Se empuja SCK y MOSI y se LEEN (por eso se dejan
  // con el buffer de entrada conectado). Si no se mueven, no hay transporte posible.
  if (out[0] == 'W') {
    // Contador de discrepancias entre la lectura por registro y digitalRead().
    static uint32_t gDiscrepancia = 0;
    gDiscrepancia = 0;
    epdPinesBitBang();
    epdSpiConfigura();          // suelta el SAADC de P0.31 y reengancha los pines
    epdPinesBitBang();          // y deja el bit-bang mandando

    // ★★ LA PRUEBA QUE ZANJA LA CONTRADICCION ★★
    // El banco de pruebas lee los pines EMPUJADOS con el buffer de entrada DESCONECTADO y
    // le salen altos; aqui, con el buffer CONECTADO, salen bajos. Para saber cual de las
    // dos cosas es la verdad se mide de las dos maneras, y ademas con pull-up:
    //   * si con el buffer DESCONECTADO y pull-up sale 1 -> nadie lo sujeta a masa
    //   * si con el buffer CONECTADO y pull-up sale 0     -> el que lo sujeta es el propio pin
    // Se leen las tres formas: registro IN y digitalRead().
    uint32_t v[5][4];   // [pin][0=sueltoDesc 1=subidaDesc 2=subidaCon 3=empujadoCon]
    const int pines3[3] = {PIN_SCK, PIN_MOSI, PIN_BUSY};
    for (int i = 0; i < 3; i++) {
      const int p = pines3[i];
      // (1) entrada con el buffer DESCONECTADO, sin resistencia
      pinMode(p, INPUT);
      delay(2); v[i][0] = nrf_gpio_pin_read(p);
      // (2) entrada con el buffer DESCONECTADO y pull-up
      pinMode(p, INPUT_PULLUP);
      delay(2); v[i][1] = nrf_gpio_pin_read(p);
      // (3) entrada con el buffer CONECTADO y pull-up
      nrf_gpio_cfg_input(p, NRF_GPIO_PIN_PULLUP);
      delay(2); v[i][2] = nrf_gpio_pin_read(p);
      // (4) empujado a 1 con el buffer CONECTADO
      epdPinSalidaFuerte(p);
      nrf_gpio_pin_set(p);
      delay(2);
      v[i][3] = nrf_gpio_pin_read(p);
      // ★ Y LA MISMA LECTURA CON digitalRead(), que es lo que usa el core. Si los dos
      //   metodos no dicen lo mismo, el que esta roto es el instrumento (ya ha pasado hoy).
      const int dr = (digitalRead(p) == HIGH) ? 1 : 0;
      if (dr != (int)v[i][3]) gDiscrepancia++;
      v[i][3] |= ((uint32_t)dr << 8);
      pinMode(p, INPUT_PULLUP);
    }
    epdPinesBitBang();
    const uint32_t cnfSck = (uint32_t)NRF_P0->PIN_CNF[PIN_SCK];
    const uint32_t saadcPsel = (uint32_t)NRF_SAADC->CH[0].PSELP;
    snprintf(out, n,
             "EPD mueve? SCK: sueltoDesc=%lu subidaDesc=%lu subidaCon=%lu empujado(reg/dr)=%lu/%lu | "
             "MOSI: %lu/%lu/%lu/%lu/%lu | BUSY: %lu/%lu/%lu/%lu/%lu | discrepancias=%lu | "
             "CNF SCK=0x%08lX SAADC.PSELP=%lu",
             (unsigned long)(v[0][0]), (unsigned long)(v[0][1]), (unsigned long)(v[0][2]),
             (unsigned long)(v[0][3] & 0xFF), (unsigned long)((v[0][3] >> 8) & 0xFF),
             (unsigned long)(v[1][0]), (unsigned long)(v[1][1]), (unsigned long)(v[1][2]),
             (unsigned long)(v[1][3] & 0xFF), (unsigned long)((v[1][3] >> 8) & 0xFF),
             (unsigned long)(v[2][0]), (unsigned long)(v[2][1]), (unsigned long)(v[2][2]),
             (unsigned long)(v[2][3] & 0xFF), (unsigned long)((v[2][3] >> 8) & 0xFF),
             (unsigned long)gDiscrepancia,
             (unsigned long)cnfSck, (unsigned long)saadcPsel);
    return;
  }
  // 'B' = estado del interruptor del panel P1.11: B0 = BAJO, B1 = ALTO, B2 = suelto.
  // Va con el numero en out[1] y despues se vuelve a sondar los pines, que es lo que
  // dice si con ese estado al panel le llega corriente.
  if (out[0] == 'B') {
    const int modo = out[1] - '0';
    if (modo == 0)      { pinMode(PIN_EPD_PWR, OUTPUT); digitalWrite(PIN_EPD_PWR, LOW); }
    else if (modo == 1) { pinMode(PIN_EPD_PWR, OUTPUT); digitalWrite(PIN_EPD_PWR, HIGH); }
    else                { pinMode(PIN_EPD_PWR, INPUT); }
    delay(100);
    epdSondaPines(out, n);
    return;
  }
  // 'C' = PRUEBA DEL CARRUSEL con refresco PARCIAL (Paso 1, 2026-09-15).
  //   `epdparcial N` dibuja N cambios de escena seguidos; cada cambio va con `epdFlush()`,
  //   que elige completo o parcial. El PRIMERO tras el arranque es completo (no hay plano
  //   anterior); los siguientes son parciales y NO deben parpadear.
  //   Es la prueba que pide el Paso 1: sin parcial, cada cambio de escena seria un parpadeo
  //   de 2 s y el carrusel (Paso 2) no tendria sentido.
  //   El primer byte del buffer dice cuantos cambios se quieren (1..9); el numero de escenas
  //   es fijo (kNumEscenas) para que lo que se ve sea exactamente el carrusel de verdad.
  if (out[0] == 'C') {
    // ★ OJO CON EL NOMBRE: el numero de escenas NO puede llamarse `n` en esta funcion,
    //   porque `n` es el TAMANO DEL BUFFER que viene del que llama. Llamarlo `n` hacia que
    //   el snprintf del final recibiera 3 como tamano de destino y truncara el mensaje
    //   (lo cazo el aviso del compilador, -Wformat-truncation). Se llama `nEscenas`.
    const int nEscenas = (out[1] >= '1' && out[1] <= '9') ? (out[1] - '0') : 3;
    epdPinesBitBang();
    epdPinesReposo();
    gReady = true;

    const uint32_t completosAntes = gNCompletos;
    const uint32_t parcialesAntes = gNParciales;
    const uint32_t tTodo = millis();

    // Primero las escenas que pide el operador, y al final SIEMPRE la de estado (que es la
    // que tiene que quedar en pantalla cuando termine la prueba).
    for (int i = 0; i < nEscenas; i++) {
      gEscena = (uint8_t)(i % kNumEscenas);
      dibujaEscena();          // deja gBuf con la escena y marca gDirty
      epdFlush();
      delay(500);              // que al operador le de tiempo a mirar cada escena
    }
    gEscena = 0;
    dibujaEscena();
    epdFlush();

    const uint32_t completosHechos = gNCompletos - completosAntes;
    const uint32_t parcialesHechos = gNParciales - parcialesAntes;
    snprintf(out, n,
             "EPD carrusel: %lu refrescos (%lu completos + %lu parciales) en %lums | "
             "el ULTIMO ha sido %s (%lums) | parcial mas reciente=%lums | "
             "en los PARCIALES la pantalla no debe parpadear ni ponerse en blanco",
             (unsigned long)(completosHechos + parcialesHechos),
             (unsigned long)completosHechos, (unsigned long)parcialesHechos,
             (unsigned long)(millis() - tTodo),
             (gMsUltimo == gMsUltimoParcial && gMsUltimoParcial > 0) ? "PARCIAL" : "COMPLETO",
             (unsigned long)gMsUltimo, (unsigned long)gMsUltimoParcial);
    return;
  }
  // 'F' = LA HUELLA DEL CONTENIDO y todos sus ingredientes (herramienta de taller).
  //   Se llama dos veces y se comparan los numeros: el que cambie es el que hace que la
  //   pantalla se repinte "sin motivo". Ver `epdHuellaTexto()`.
  if (out[0] == 'F') { epdHuellaTexto(out, n); return; }
  const uint32_t sck = NRF_SPIM2->PSEL.SCK;
  const uint32_t mosi = NRF_SPIM2->PSEL.MOSI;
  // Segundos que le quedan a la pausa del carrusel (0 = sin pausa). Se calcula con una
  // resta con signo: `millis()` da la vuelta a los 49 dias y una resta sin signo daria un
  // numero gigante si el instante ya ha pasado.
  const uint32_t restaPausa = (uint32_t)(int32_t)(gCarruselPausadoHasta - millis());
  const uint32_t pausaSeg = (restaPausa < 0x80000000u) ? (restaPausa / 1000u) : 0u;
  snprintf(out, n,
           "EPD listo=%d transporte=%s rotacion=%d bytesBitBang=%lu | SPIM2 PSEL.SCK=P%d.%d MOSI=P%d.%d ENABLE=%u | "
           "timeouts=%lu errores=%lu ultimoRefresco=%lums | "
           "parciales=%lu (ultimo %lums) completos=%lu (ultimo %lums) seguidos=%lu/%lu "
           "BUSYvisto=%lu BUSYtimeouts=%lu | "
           "carrusel=%s escena=%d (%lus) pausa=%lus | "
           "BUSY=%d CS=%d DC=%d RST=%d PWR_EN=%d REG_EN=%d P1.11=%d",
           gReady ? 1 : 0, gTransporte == 0 ? "bit-bang" : "SPIM2", gRotacion,
           (unsigned long)gBytesBitBang,
           (int)((sck >> 5) & 1), (int)(sck & 0x1F),
           (int)((mosi >> 5) & 1), (int)(mosi & 0x1F),
           (unsigned)(NRF_SPIM2->ENABLE & 1u),
           (unsigned long)gErrTimeout, (unsigned long)gErrSpi, (unsigned long)gMsUltimo,
           (unsigned long)gNParciales, (unsigned long)gMsUltimoParcial,
           (unsigned long)gNCompletos, (unsigned long)gMsUltimoCompleto,
           (unsigned long)gNParcialesSeguidos, (unsigned long)kMaxParcialesSeguidos,
           (unsigned long)gBusyAvisos, (unsigned long)gBusyTimeouts,
           gAutoAvance ? "si" : "no", (int)gEscena,
           (unsigned long)((millis() - gUltimoCambioEscenaMs) / 1000),
           (unsigned long)pausaSeg,
           (int)digitalRead(PIN_BUSY), (int)digitalRead(PIN_CS), (int)digitalRead(PIN_DC),
           (int)digitalRead(PIN_RST), (int)digitalRead(PIN_PWR_EN),
           (int)digitalRead(PIN_REG_EN), (int)digitalRead(PIN_EPD_PWR));
}

bool displayPresent() { return gReady; }
bool displayIsOn() { return gReady; }

// ★★ VOLCADO DE REGISTROS DE LOS PINES (herramienta de taller) ★★
//
// Cuando "un pin no sube" hay que mirar los registros, no suponer. Esto saca, para cada
// pin de la pantalla y para dos pines de control (P0.14 LED y P0.05 libre):
//   * PIN_CNF: DIR (0=entrada 1=salida), INPUT (buffer conectado), PULL, DRIVE
//   * y que se lee con el pin SUELTO, con PULL-UP y con PULL-DOWN
// Si con PULL-UP se lee 0, alguien esta sujetando el pin a masa de verdad (y entonces no
// tiene nada que ver con la configuracion ni con el bit-bang).
struct PinInfo { int pin; const char *nom; };

void epdVolcadoPines(char *out, size_t n) {
  if (!out || !n) return;
  const PinInfo pines[8] = {
      {PIN_SCK, "SCK31"},   {PIN_MOSI, "MOSI29"}, {PIN_CS, "CS30"},
      {PIN_DC, "DC28"},     {PIN_RST, "RST2"},    {PIN_BUSY, "BUSY3"},
      {PIN_LED_BLUE, "LED14"}, {5, "P0.05"}};
  int pos = 0;
  out[0] = '\0';

  for (int i = 0; i < 8; i++) {
    const int p = pines[i].pin;
    const NRF_GPIO_Type *g = (p < 32) ? NRF_P0 : NRF_P1;
    const uint32_t b = p & 31;

    // SUELTO: entrada sin resistencia
    nrf_gpio_cfg_input(p, NRF_GPIO_PIN_NOPULL);
    delay(2);
    const int suelto = nrf_gpio_pin_read(p);
    const uint32_t cnfSuelto = g->PIN_CNF[b];

    // PULL-DOWN: si algo lo empuja a 1, se vera 1
    nrf_gpio_cfg_input(p, NRF_GPIO_PIN_PULLDOWN);
    delay(2);
    const int conBajada = nrf_gpio_pin_read(p);

    // PULL-UP: si algo lo sujeta a masa, saldra 0
    nrf_gpio_cfg_input(p, NRF_GPIO_PIN_PULLUP);
    delay(2);
    const int conSubida = nrf_gpio_pin_read(p);

    // Y EMPUJADO por nosotros a 1 (salida), para ver si gana al que lo sujeta
    nrf_gpio_cfg_output(p);
    nrf_gpio_pin_set(p);
    delay(2);
    const uint32_t cnfSalida = g->PIN_CNF[b];
    nrf_gpio_cfg_input(p, NRF_GPIO_PIN_NOPULL);
    delay(2);
    const int empujado1 = nrf_gpio_pin_read(p);

    pos += snprintf(out + pos, n - pos,
                    "%s suelto=%d bajada=%d subida=%d empujado=%d cnfSuelto=0x%08lX cnfSalida=0x%08lX | ",
                    pines[i].nom, suelto, conBajada, conSubida, empujado1,
                    (unsigned long)cnfSuelto, (unsigned long)cnfSalida);
    if (pos >= (int)n - 120) break;
  }

  // Se dejan como estaban.
  nrf_gpio_cfg_output(PIN_SCK);  nrf_gpio_pin_clear(PIN_SCK);
  nrf_gpio_cfg_output(PIN_MOSI); nrf_gpio_pin_clear(PIN_MOSI);
  nrf_gpio_cfg_output(PIN_CS);   nrf_gpio_pin_set(PIN_CS);
  nrf_gpio_cfg_output(PIN_DC);   nrf_gpio_pin_set(PIN_DC);
  pinMode(PIN_RST, INPUT_PULLUP);
  nrf_gpio_cfg_output(PIN_LED_BLUE); nrf_gpio_pin_set(PIN_LED_BLUE);
}

//
// ¿Se puede GOBERNAR de verdad cada pin de la pantalla, o algo lo sujeta?
//
// COMO SE MIDE, y esto hay que hacerlo bien (el primer intento salio mal): no vale
// `nrf_gpio_cfg_output()` + leer, porque esa configuracion DESCONECTA el buffer de
// entrada del pin y la lectura sale SIEMPRE 0 (con lo que TODO parecia sujeto a masa,
// incluido el LED azul, que no lo esta). La forma buena es:
//     1) configurar el pin como entrada SIN resistencia y leerlo con el pin suelto
// ★★ SONDA DE PINES (comando "epdpines") — LA PRUEBA QUE SEPARA DOS MUNDOS ★★
//
// ¿Se puede GOBERNAR de verdad cada pin de la pantalla, o algo lo sujeta?
//
// ★★ DOS TRAMPAS DE INSTRUMENTACION, Y HE CAIDO EN LAS DOS (2026-09-14) ★★
//
//  1. `nrf_gpio_cfg_output()` DESCONECTA el buffer de entrada del pin, asi que leer despues
//     da SIEMPRE 0: con eso, TODO parecia sujeto a masa, incluido el LED azul.
//  2. **`nrf_gpio_pin_read()` NO ES FIABLE EN ESTE CORE.** Medido en esta misma placa, con
//     los pines empujados a 1: `nrf_gpio_pin_read()` devuelve 0 y `digitalRead()` devuelve 1
//     **para los mismos tres pines y en el mismo instante** (3 discrepancias de 3). Es
//     exactamente el mismo fallo que el proyecto ya habia documentado para el registro `IN`
//     (docs/HELLO_WORLD_TECHO.md §4.1) y en el que yo he vuelto a caer.
//     -> POR ESO AQUI SE LEE SIEMPRE CON `digitalRead()`.
//
// Esto importa porque con el instrumento malo llegue a conclusiones falsas ("los pines
// estan sujetos a masa, al panel no le llega corriente"). Con `digitalRead()` los pines
// **SI se gobiernan** (fallos=0x00): el bus estaba bien y la pantalla no pintaba porque
// SPIM2 se quedaba los pines por PSEL (ver la nota de `epdPinesBitBang()`).
//
// La prueba de CONTROL es el LED azul (P0.14): lleva una resistencia y un LED, no lo sujeta
// ningun chip. Si el LED no da 0/01, el que falla es el METODO y no la pantalla.
void epdSondaPines(char *out, size_t n) {
  if (!out || !n) return;
  // ★ PRIMERO SE SUELTA EL PERIFERICO DE LOS PINES. Si SPIM2 tiene PSEL apuntando a
  //   P0.31/P0.29, el GPIO no manda ahi y la sonda diria "no obedecen" siendo mentira.
  NRF_SPIM2->ENABLE = 0;
  NRF_SPIM2->PSEL.SCK = 0x80000000u;    // desconectado
  NRF_SPIM2->PSEL.MOSI = 0x80000000u;
  NRF_SPIM2->PSEL.MISO = 0x80000000u;

  const int pines[5] = {PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST};
  const char *nom[5] = {"SCK", "MOSI", "CS", "DC", "RST"};
  uint32_t fallos = 0;
  char det[180];
  int pos = 0;
  det[0] = '\0';

  for (int i = 0; i < 5; i++) {
    const int p = pines[i];
    // suelto (sin pull) -> si algo lo sujeta, aqui ya se ve
    nrf_gpio_cfg_input(p, NRF_GPIO_PIN_NOPULL);
    delay(2);
    const int suelto = (digitalRead(p) == HIGH) ? 1 : 0;
    // empujado a 0
    nrf_gpio_cfg_output(p);
    nrf_gpio_pin_clear(p);
    delay(2);
    nrf_gpio_cfg_input(p, NRF_GPIO_PIN_NOPULL);
    delay(2);
    const int tras0 = (digitalRead(p) == HIGH) ? 1 : 0;
    // empujado a 1
    nrf_gpio_cfg_output(p);
    nrf_gpio_pin_set(p);
    delay(2);
    nrf_gpio_cfg_input(p, NRF_GPIO_PIN_NOPULL);
    delay(2);
    const int tras1 = (digitalRead(p) == HIGH) ? 1 : 0;
    if (tras0 != 0 || tras1 != 1) fallos |= (1u << i);
    pos += snprintf(det + pos, sizeof(det) - pos, " %s=%d/%d%d", nom[i], suelto, tras0, tras1);
  }

  // Prueba de CONTROL: LED azul P0.14 (tiene que dar 0/01).
  const int pLed = PIN_LED_BLUE;
  nrf_gpio_cfg_input(pLed, NRF_GPIO_PIN_NOPULL);
  delay(2);
  const int ledSuelto = nrf_gpio_pin_read(pLed);
  nrf_gpio_cfg_output(pLed);
  nrf_gpio_pin_clear(pLed);
  delay(2);
  nrf_gpio_cfg_input(pLed, NRF_GPIO_PIN_NOPULL);
  delay(2);
  const int led0 = nrf_gpio_pin_read(pLed);
  nrf_gpio_cfg_output(pLed);
  nrf_gpio_pin_set(pLed);
  delay(2);
  nrf_gpio_cfg_input(pLed, NRF_GPIO_PIN_NOPULL);
  delay(2);
  const int led1 = nrf_gpio_pin_read(pLed);

  // ★ ¿QUIEN SUJETA P0.31? La bateria se mide con analogRead(31) en src/sensors.cpp, y el
  //   SCK de la pantalla ES P0.31. Aqui se lee el registro PSELP del SAADC: si apunta a
  //   AIN7, el ADC tiene ese pin cogido (y el ADC apunta a un pin mientras esta activo).
  const uint32_t saadcPsel = (uint32_t)NRF_SAADC->CH[0].PSELP;
  const uint32_t saadcEn = (uint32_t)NRF_SAADC->ENABLE;
  char saadc[80];
  snprintf(saadc, sizeof(saadc), "SAADC ENABLE=%lu CH0.PSELP=%lu%s",
           (unsigned long)saadcEn, (unsigned long)saadcPsel,
           (saadcPsel == SAADC_CH_PSELP_PSELP_AnalogInput7) ? " (¡apunta a AIN7 = P0.31!)" : "");

  // Se dejan como estaban (reposo: CS y DC altos, reloj y datos bajos, reset arriba).
  nrf_gpio_cfg_output(PIN_SCK);  nrf_gpio_pin_clear(PIN_SCK);
  nrf_gpio_cfg_output(PIN_MOSI); nrf_gpio_pin_clear(PIN_MOSI);
  nrf_gpio_cfg_output(PIN_CS);   nrf_gpio_pin_set(PIN_CS);
  nrf_gpio_cfg_output(PIN_DC);   nrf_gpio_pin_set(PIN_DC);
  pinMode(PIN_RST, INPUT_PULLUP);
  nrf_gpio_cfg_output(pLed); nrf_gpio_pin_set(pLed);   // LED apagado (activo a nivel bajo)

  snprintf(out, n,
           "EPD pines (suelto/tras0/tras1):%s | CONTROL LED P0.14=%d/%d%d (tiene que ser 0/01) | "
           "PWR_EN=%d REG_EN=%d P1.11=%d | %s | fallos=0x%02X -> %s",
           det, ledSuelto, led0, led1,
           (int)digitalRead(PIN_PWR_EN), (int)digitalRead(PIN_REG_EN),
           (int)digitalRead(PIN_EPD_PWR), saadc, (unsigned)fallos,
           fallos ? ((led0 == 0 && led1 == 1)
                         ? "EL PANEL NO SUBE PERO EL LED SI -> a la pantalla NO le llega corriente"
                         : "ni el LED sube: fallo del METODO, no del panel")
                  : "TODOS SE GOBIERNAN: el bit-bang llega a los pines y al panel le llega corriente");
}

// ★ SONDA DEL PERIFERICO, A PETICION (comando "epdsonda" del USB) — HERRAMIENTA DE TALLER.
//
// Existe porque "la transferencia se atasca" es un sintoma que no dice POR QUE. Esto
// lanza UNA transferencia de 1 byte y va leyendo los registros del periferico, para
// distinguir tres casos MUY distintos:
//   * TASKS_START no hace nada                -> AMOUNT se queda en 1 (no arranca)
//   * arranca pero se queda a medias          -> AMOUNT baja pero no llega a 0
//   * arranca y termina pero nadie se entera  -> AMOUNT=0 y EVENTS_END=0
// Se puede quitar cuando la pantalla este validada: no la usa el firmware.
void epdSonda(char *out, size_t n) {
  if (!out || !n) return;
  static uint8_t buf[8];
  memset(buf, 0xA5, sizeof(buf));

  const uint32_t iEn = (uint32_t)NRF_SPIM2->ENABLE;
  const uint32_t iSck = (uint32_t)NRF_SPIM2->PSEL.SCK;
  const uint32_t iMosi = (uint32_t)NRF_SPIM2->PSEL.MOSI;
  const uint32_t iFreq = (uint32_t)NRF_SPIM2->FREQUENCY;
  const uint32_t iCfg = (uint32_t)NRF_SPIM2->CONFIG;

  epdSpiConfigura();

  // ★ ENABLE tiene que ser exactamente 1. Se limpia a mano (no con nrf_spim_disable,
  //   que hace read-modify-write y conserva los bits raros).
  NRF_SPIM2->ENABLE = 0;
  NRF_SPIM2->PSEL.SCK = (uint32_t)PIN_SCK;
  NRF_SPIM2->PSEL.MOSI = (uint32_t)PIN_MOSI;
  NRF_SPIM2->PSEL.MISO = (uint32_t)EPD_MISO_PIN;
  NRF_SPIM2->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M4;
  NRF_SPIM2->CONFIG = (SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos) |
                      (SPIM_CONFIG_CPOL_ActiveHigh << SPIM_CONFIG_CPOL_Pos) |
                      (SPIM_CONFIG_CPHA_Leading << SPIM_CONFIG_CPHA_Pos);
  NRF_SPIM2->EVENTS_END = 0;
  NRF_SPIM2->EVENTS_ENDTX = 0;
  NRF_SPIM2->EVENTS_STARTED = 0;
  NRF_SPIM2->STALLSTAT = 0xFFFFFFFFu;   // limpiar los avisos de atasco de EasyDMA
  NRF_SPIM2->ENABLE = 1;

  const uint32_t jEn = (uint32_t)NRF_SPIM2->ENABLE;
  const uint32_t stall0 = (uint32_t)NRF_SPIM2->STALLSTAT;
  const uint32_t ram0 = (uint32_t)NRF_POWER->RAM[0].POWER;
  const uint32_t ram1 = (uint32_t)NRF_POWER->RAM[1].POWER;
  const uint32_t ram7 = (uint32_t)NRF_POWER->RAM[7].POWER;

  // ★ ¿RESPONDE EL PERIFERICO A SUS TAREAS? Prueba que no depende de EasyDMA ni de los
  //   pines: TASKS_SUSPEND tiene que producir EVENTS_STOPPED. Si esto no pasa, el
  //   periferico no esta gobernando NADA (esta apagado o alguien lo tiene tomado).
  NRF_SPIM2->TASKS_SUSPEND = 1;
  const uint32_t evSusp = (uint32_t)NRF_SPIM2->EVENTS_STOPPED;
  NRF_SPIM2->EVENTS_STOPPED = 0;
  NRF_SPIM2->TASKS_RESUME = 1;

  // Otra tarea que no usa datos: TASKS_STOP -> EVENTS_STOPPED.
  NRF_SPIM2->TASKS_STOP = 1;
  const uint32_t evStop = (uint32_t)NRF_SPIM2->EVENTS_STOPPED;
  NRF_SPIM2->EVENTS_STOPPED = 0;

  // ★ LA PRUEBA QUE DECIDE: transferencia LARGA (5000 bytes) vigilando el contador.
  //   Si el periferico arranca, MAXCNT se queda en 5000 y AMOUNT va bajando; si esta
  //   muerto, los dos se quedan raros y ademas EVENTS_STARTED no sube nunca.
  static uint8_t grande[EPD_BUFSZ];
  const uint32_t t0 = micros();
  NRF_SPIM2->TXD.PTR = (uint32_t)(uintptr_t)grande;
  NRF_SPIM2->TXD.MAXCNT = EPD_BUFSZ;
  NRF_SPIM2->RXD.PTR = 0;
  NRF_SPIM2->RXD.MAXCNT = 0;
  NRF_SPIM2->EVENTS_END = 0;
  NRF_SPIM2->EVENTS_ENDTX = 0;
  NRF_SPIM2->EVENTS_STARTED = 0;
  const uint32_t maxcntTras = (uint32_t)NRF_SPIM2->TXD.MAXCNT;
  epdCs(true);
  NRF_SPIM2->TASKS_START = 1;

  uint32_t am1 = 0, am2 = 0, am3 = 0, st1 = 0, amFin = 0;
  const uint32_t tas = micros();
  while (micros() - tas < 1000) {}          // 1 ms: a 4 MHz eso son ~500 bytes
  am1 = (uint32_t)NRF_SPIM2->TXD.AMOUNT;
  st1 = (uint32_t)NRF_SPIM2->EVENTS_STARTED;
  while (micros() - tas < 2000) {}
  am2 = (uint32_t)NRF_SPIM2->TXD.AMOUNT;
  while (micros() - tas < 3000) {}
  am3 = (uint32_t)NRF_SPIM2->TXD.AMOUNT;
  const uint32_t te = micros();
  while (!NRF_SPIM2->EVENTS_ENDTX && !NRF_SPIM2->EVENTS_END && micros() - te < 50000) {}
  const uint32_t dur = micros() - t0;
  const uint32_t endtx = NRF_SPIM2->EVENTS_ENDTX;
  const uint32_t fin = NRF_SPIM2->EVENTS_END;
  amFin = (uint32_t)NRF_SPIM2->TXD.AMOUNT;

  NRF_SPIM2->TASKS_STOP = 1;
  NRF_SPIM2->EVENTS_END = 0;
  NRF_SPIM2->EVENTS_ENDTX = 0;
  NRF_SPIM2->EVENTS_ENDTX = 0;
  NRF_SPIM2->EVENTS_STOPPED = 0;
  NRF_SPIM2->STALLSTAT = 0xFFFFFFFFu;
  NRF_SPIM2->ENABLE = 0;
  NRF_SPIM2->ENABLE = 1;
  epdCs(false);
  const uint32_t stall = (uint32_t)NRF_SPIM2->STALLSTAT;
  const uint32_t kEn = (uint32_t)NRF_SPIM2->ENABLE;

  snprintf(out, n,
           "EPD sonda: ENABLE %lu->%lu->%lu | PSEL SCK %lu->%lu MOSI %lu->%lu | FREQ 0x%08lX | "
           "FLANCO GPIO SCK=%d MOSI=%d | RAM[0]=0x%08lX | tareas SUSPEND=%lu STOP=%lu | "
           "xfer5000: MAXCNT=%lu AMOUNT a1ms=%lu a2ms=%lu a3ms=%lu fin=%lu | dur=%luus "
           "STARTED=%lu ENDTX=%lu END=%lu | STALL=0x%08lX",
           (unsigned long)iEn, (unsigned long)jEn, (unsigned long)kEn,
           (unsigned long)iSck, (unsigned long)NRF_SPIM2->PSEL.SCK,
           (unsigned long)iMosi, (unsigned long)NRF_SPIM2->PSEL.MOSI,
           (unsigned long)iFreq, (int)digitalRead(PIN_SCK), (int)digitalRead(PIN_MOSI),
           (unsigned long)ram0, (unsigned long)evSusp, (unsigned long)evStop,
           (unsigned long)maxcntTras, (unsigned long)am1, (unsigned long)am2,
           (unsigned long)am3, (unsigned long)amFin, (unsigned long)dur,
           (unsigned long)st1, (unsigned long)endtx, (unsigned long)fin,
           (unsigned long)stall);
}

void displayWake() { gAsleep = false; }

// ---- RETROILUMINACION (P1.11) ----
// Peticion del operador (2026-09-15): la luz de la pantalla se enciende al tocar un boton
// y se apaga sola a los 5 s. P1.11 es el interruptor de la retroiluminacion del T-Echo.
// Se deja APAGADA en reposo (el firmware de fabrica tambien la deja baja) y solo sube
// mientras el temporizador esta vivo.
static bool gLuzOn = false;
static uint32_t gLuzHasta = 0;
static bool gLuzIni = false;
void displayBacklightKick() {
  if (!gLuzIni) { pinMode(PIN_EPD_PWR, OUTPUT); gLuzIni = true; }
  gLuzOn = true;
  gLuzHasta = millis() + 5000;     // 5 s encendida
  digitalWrite(PIN_EPD_PWR, HIGH);
}
void displayBacklightTick(uint32_t nowMs) {
  if (!gLuzOn) return;
  if ((int32_t)(nowMs - gLuzHasta) >= 0) {
    gLuzOn = false;
    digitalWrite(PIN_EPD_PWR, LOW);   // se apaga sola a los 5 s
  }
}

// Pitido corto del buzzer (T-Echo Plus P0.06), sonido audible pero NO agudo (2026-09-15).
// Se hace a mano con ondas cuadradas: ~2,2 kHz durante ~60 ms. Bloquea muy poco. Solo
// existe en el Plus (PIN_BUZZER definido).
void displayBeep() {
#if defined(PIN_BUZZER)
  // ★ EL ZUMBADOR SOLO SUENA SI ESTA PLACA ES UN PLUS (2026-09-15).
  //   El zumbador es un GPIO normal (P0.06): NO se puede preguntar si existe. Pero el motor
  //   haptico va con el (solo los lleva el Plus) y ese SI se detecta por I2C, asi que se usa
  //   de carne de identidad. En un T-Echo normal el firmware NO toca P0.06 en absoluto: no
  //   se mueve un pin del que no consta a que va en esa placa.
  if (!hapticEsPlus()) return;
  static bool ini = false;
  if (!ini) { pinMode(PIN_BUZZER, OUTPUT); ini = true; }
  const uint32_t t0 = micros();
  const uint32_t periodUs = 450;          // ~2,2 kHz (no muy agudo)
  bool hi = true;
  while ((uint32_t)(micros() - t0) < 60000) {
    digitalWrite(PIN_BUZZER, hi);
    const uint32_t half = periodUs / 2;
    uint32_t w = micros();
    while ((uint32_t)(micros() - w) < half) {}
    hi = !hi;
  }
  digitalWrite(PIN_BUZZER, LOW);          // reposo
#endif
}

// ★ AVISO SONORO DE BATERIA BAJA (2026-09-15). Melodia descendente "triste", dos frases,
// la segunda mas grave y una nota final larga: el clasico aviso de bateria baja de los
// Nokia viejos (peticion del operador). Se genera igual que el pitido, con ondas cuadradas
// a mano, asi que las notas se cambian AQUI: (frecuencia en Hz, duracion en ms), 0 = pausa.
// BLOQUEA mientras suena (~1,3 s). Solo se llama justo antes de dormir por bateria baja,
// donde ese segundo no importa. En placas sin buzzer (PIN_BUZZER sin definir) no hace nada.
void displayLowBatTone() {
#if defined(PIN_BUZZER)
  // Misma regla que en displayBeep(): sin motor no es un Plus, y sin Plus no hay zumbador.
  if (!hapticEsPlus()) return;
  static bool ini = false;
  if (!ini) { pinMode(PIN_BUZZER, OUTPUT); ini = true; }
  static const uint16_t notas[][2] = {
    {494, 150}, {370, 250}, {0, 90},     // primera frase: si4 -> fa#4
    {494, 150}, {330, 280}, {0, 90},     // segunda, mas grave: si4 -> mi4
    {262, 450},                          // nota final larga y grave ("se acaba")
  };
  for (size_t i = 0; i < sizeof(notas) / sizeof(notas[0]); i++) {
    const uint16_t f  = notas[i][0];
    const uint16_t ms = notas[i][1];
    if (f == 0) { delay(ms); continue; }
    const uint32_t half = (1000000UL / f) / 2;   // medio periodo = un flanco
    const uint32_t t0 = micros();
    bool hi = true;
    while ((uint32_t)(micros() - t0) < (uint32_t)ms * 1000UL) {
      digitalWrite(PIN_BUZZER, hi);
      const uint32_t w = micros();
      while ((uint32_t)(micros() - w) < half) {}
      hi = !hi;
    }
  }
  digitalWrite(PIN_BUZZER, LOW);          // reposo
#endif
}

void displaySleep() {
  // En tinta electronica "apagar" no es borrar: la imagen se queda. Se manda el panel
  // a dormir para que no gaste.
  if (!gReady) return;
  epdCmd1(0x10, 0x01);   // deep sleep
  gAsleep = true;
}

// ★ PANTALLA DE DORMIDO (2026-09-15): se dibuja ANTES de entrar en System OFF. La tinta es
//   bistable, asi que la imagen queda fijada aunque el nodo este apagado. Muestra: zzz,
//   el indicativo del dueño EN GRANDE (sin SSID) y el mensaje libre (si lo dejo puesto).
void displaySleepScene() {
  clearBuf(true);
  // zzz dormido (en grande)
  drawTextCenter(40, "zZz Zzz", 3);
  // indicativo del dueño, en GRANDE (sin SSID), como el splash de inicio
  drawTextCenter(88, callSinSSID(), 4);
  // mensaje libre (telefono u otro), si lo dejo configurado
  if (gCfg && gCfg->sleepMsg[0]) {
    drawTextCenter(150, gCfg->sleepMsg, 2);
  }
  gDirty = true;
  gReady = true;
  // refresco COMPLETO (forzado) para que se vea la escena y luego el nodo duerme.
  epdFullRefresh();
  gDirty = false;
}

void displaySplash() { /* la pantalla se pinta entera en el primer refresco */ }

void displayPinSplash(const char *pin, bool fromStack) {
  (void)fromStack;
  snprintf(gLinea1, sizeof(gLinea1), "PAIRING: %s", pin ? pin : "------");
  gLinea2[0] = '\0';
  gLineaMs = millis();
  gDirty = true;
  epdFlush();
}
void displayPinSplashClear() { gLinea1[0] = '\0'; gLineaMs = 0; gDirty = true; }
bool displayPinSplashActive() { return false; }

// ===========================================================================
//  ★★ MENU EN PANTALLA (2026-09-15) — PORTADO DE LA OLED a la tinta electronica ★★
// ===========================================================================
// Two levels, like the OLED: a list of CATEGORIES, then the items of one category.
//   - Capacitive (P0.11) = navigate (move selection / change edit value)
//   - Físico corto (P1.10 short) = enter / confirm
//   - Físico largo (P1.10 long) = go back
// Data model translated 1:1 from display.cpp (see §P.11 in the bitácora). Values
// are persisted through the SAME path as the `set` command (typedSet + storeSave),
// and actions call the SAME functions as the OLED menu.
namespace {

// ------------------------------------------------------------ menu data -----
enum MenuKind {
  MK_HEADER, MK_INT, MK_BOOL, MK_ENUM, MK_ENUM_F, MK_FLOAT,
  MK_STRING, MK_PATH, MK_ENUM_CYCLE, MK_ACTION,
};
enum { ACT_NONE = 0, ACT_BEACON, ACT_TELEM, ACT_TELEM_META, ACT_MUTE, ACT_SLEEP,
       ACT_REBOOT, ACT_DFU, ACT_RESET, ACT_WIPE, ACT_TRACKER_BEACON, ACT_BAT,
       ACT_SET_COORDS, ACT_GPS_INFO, ACT_MSG_SEND, ACT_PROFILES, ACT_SOTA_SPOT,
       ACT_SOTA_PLAN };
struct MenuItem {
  const char *label; MenuKind kind; const char *key;
  int min, max, step;
  const char *const *opts; const int *optVals; const float *optValsF;
  int action; uint8_t modes;
};
constexpr uint8_t kMDigi=0x01, kMTrk=0x02, kMBoth=0x04, kMAll=0x07;

const char *kModeOpts[] = {"Digipeater","Tracker","Both",nullptr};
const int kModeVals[] = {0,1,2};
[[maybe_unused]] const char *kSmartOpts[] = {"No profile","Walking","Bike","Car",nullptr};
[[maybe_unused]] const int kSmartVals[] = {0,1,2,3};
const char *kDigiOpts[] = {"Off","WIDE1-1","WIDE1+WIDE2",nullptr};
const int kDigiVals[] = {0,1,2};
const char *kTncOpts[] = {"Off","TNC2 text","KISS",nullptr};
const int kTncVals[] = {CFG_TNC_OFF, CFG_TNC_TNC2, CFG_TNC_KISS};
const char *kFreqOpts[] = {"433.775","433.900","868.200",nullptr};
const int kFreqVals[] = {433775000,433900000,868200000};
const char *kBwOpts[] = {"62.5","125","250","500",nullptr};
const float kBwVals[] = {62.5f,125.0f,250.0f,500.0f};
const char *kPathOpts[] = {"0","WIDE1-1","WIDE1-1,WIDE2-1","WIDE1-1,WIDE2-2",
                           "WIDE2-1","WIDE2-2","RFONLY",nullptr};

// ★★ ICONO DEL MAPA POR PERFIL (2026-09-15) ★★
// Los codigos NO se escriben aqui: se LEEN de la configuracion
// (gCfg->profileSymbol[]/profileOverlay[]), que es donde viven los valores de
// fabrica (config.h). Asi, si algun dia se cambia un icono por defecto, el menu y
// el firmware no pueden discrepar. Estas etiquetas son solo para la pantalla.
//   0 = repetidor (/#)   1 = persona (/[)   2 = bici (/b)   3 = coche (/>)
// Origen de los codigos: ver config.h (tabla de WA8LMF + apuntes de overlays de
// aprs.org). El numero de opciones es CUATRO y coincide con el numero de perfiles:
// se puede elegir CUALQUIERA de los cuatro iconos para CUALQUIER perfil.
constexpr int kIconoOpciones = 4;
const char *kIconoNombre[kIconoOpciones] = {"Digipeater","Person","Bike","Car"};

const MenuItem kMenu[] = {
  {"Mode", MK_ENUM_CYCLE, "mode", 0,2,1, kModeOpts, kModeVals, nullptr, 0, kMAll},
  {"GPS in digi", MK_BOOL, "gpsInDigi", 0,0,0, nullptr, nullptr, nullptr, 0, kMDigi},
  {"GPS saver", MK_BOOL, "gpsEco", 0,0,0, nullptr, nullptr, nullptr, 0, kMDigi},
  {"Set coords", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_SET_COORDS, kMAll},
  {"Show GPS", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_GPS_INFO, kMAll},
  {"Send beacon", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_BEACON, kMAll},
  {"Tracker beacon", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_TRACKER_BEACON, kMAll},
  {"Telemetry", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_TELEM, kMAll},
  {"Beacon int (min)", MK_INT, "beaconInterval", 15,240,5, nullptr, nullptr, nullptr, 0, kMAll},
  {"Compressed pos", MK_BOOL, "compressedPos", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Quick msg", MK_STRING, "msgText", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Retries", MK_INT, "msgRetries", 0,5,1, nullptr, nullptr, nullptr, 0, kMAll},
  {"Tracker int (s)", MK_INT, "trackerIntervalSecs", 10,3600,10, nullptr, nullptr, nullptr, 0, kMAll},
  {"Distance (m)", MK_INT, "trackerMinDistanceM", 0,5000,50, nullptr, nullptr, nullptr, 0, kMAll},
  {"Min time (s)", MK_INT, "trackerMinSpacing", 10,120,5, nullptr, nullptr, nullptr, 0, kMAll},
  {"Profile", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_PROFILES, kMAll},
  {"Send altitude", MK_BOOL, "sendAltitude", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Sleep between", MK_BOOL, "trackerSleep", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Frequency", MK_ENUM, "frequency", 0,0,0, kFreqOpts, kFreqVals, nullptr, 0, kMAll},
  {"SF", MK_INT, "spreadingFactor", 5,12,1, nullptr, nullptr, nullptr, 0, kMAll},
  {"CR", MK_INT, "codingRate4", 5,8,1, nullptr, nullptr, nullptr, 0, kMAll},
  {"Bandwidth", MK_ENUM_F, "signalBandwidth", 0,0,0, kBwOpts, nullptr, kBwVals, 0, kMAll},
  {"Power", MK_INT, "power", 2,22,1, nullptr, nullptr, nullptr, 0, kMAll},
  {"CAD", MK_BOOL, "cadActive", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Digi mode", MK_ENUM_CYCLE, "digiMode", 0,2,1, kDigiOpts, kDigiVals, nullptr, 0, kMAll},
  {"Blacklist", MK_STRING, "blacklist", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Mute TX", MK_BOOL, "txDisabled", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Callsign", MK_STRING, "callsign", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Tocall", MK_STRING, "tocall", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Path", MK_STRING, "path", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Path digi", MK_PATH, "pathDigi", 0,0,0, kPathOpts, nullptr, nullptr, 0, kMAll},
  {"Path tracker", MK_PATH, "pathTracker", 0,0,0, kPathOpts, nullptr, nullptr, 0, kMAll},
  {"Path both", MK_PATH, "pathBoth", 0,0,0, kPathOpts, nullptr, nullptr, 0, kMAll},
  {"Symbol", MK_STRING, "symbol", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Comment", MK_STRING, "comment", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Latitude", MK_FLOAT, "latitude", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Longitude", MK_FLOAT, "longitude", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Queries", MK_BOOL, "queriesEnabled", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"USB TNC", MK_ENUM_CYCLE, "tncProtocol", 0,2,1, kTncOpts, kTncVals, nullptr, 0, kMAll},
  // == Bluetooth (pareja) ==
  {"Bluetooth", MK_BOOL, "bleEnabled", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"PIN BT", MK_STRING, "blePin", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  // == Sensores ==
  {"Send WX", MK_BOOL, "wxSensorActive", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Send telem", MK_BOOL, "sendBatteryTelemetry", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Telem int (min)", MK_INT, "telemetryIntervalMin", 0,720,15, nullptr, nullptr, nullptr, 0, kMAll},
  // ★ INTERVALO DE METEOROLOGIA (2026-09-15): ajuste NUEVO, justo debajo del de
  //   telemetria y con su mismo formato (MK_INT, 0..720, paso 15: 0 = no automatico,
  //   el resto 15..720 min, igual que la regla de config.cpp). El paquete WX iba fijo
  //   a 15 minutos dentro de main.cpp y no habia manera de tocarlo desde aqui.
  {"WX int (min)", MK_INT, "wxIntervalMin", 0,720,15, nullptr, nullptr, nullptr, 0, kMAll},
  {"Probe corr", MK_FLOAT, "temperatureCorrection", -5,5,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Chip offset", MK_FLOAT, "chipTempOffset", -10,10,0, nullptr, nullptr, nullptr, 0, kMAll},
  // == Pantalla ==
  {"Auto-advance", MK_BOOL, "sceneAutoAdvance", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Screen off (s)", MK_INT, "screenTimeoutSecs", 0,3600,15, nullptr, nullptr, nullptr, 0, kMAll},
  {"Popups", MK_BOOL, "popups", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  // == Energia ==
  {"Cutoff (mV)", MK_INT, "sleepCutMv", 2500,4200,50, nullptr, nullptr, nullptr, 0, kMAll},
  {"Wake (mV)", MK_INT, "sleepWakeMv", 2600,4500,50, nullptr, nullptr, nullptr, 0, kMAll},
  {"Show battery", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_BAT, kMAll},
  {"Sleep", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_SLEEP, kMAll},
  // == Remoto ==
  {"Remote control", MK_BOOL, "remoteEnabled", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Operators", MK_STRING, "managers", 0,0,0, nullptr, nullptr, nullptr, 0, kMAll},
  {"Mute/unmute", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_MUTE, kMAll},
  // == Ajustes ==
  {"Reboot", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_REBOOT, kMAll},
  {"Factory reset", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_RESET, kMAll},
  {"Wipe all", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_WIPE, kMAll},
  // == SOTA == (2026-09-27) Va AL FINAL de la tabla para no mover los indices de las
  //   demas secciones; en pantalla sale la PRIMERA (ver kMenuSections).
  //   "Plan activation": cumbre, frecuencia, modo e indicativo -> gCfg->sotaPlan.
  //   "SOTA spot": pide el comentario y envia el plan a APRS2SOTA.
  {"Plan activation", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_SOTA_PLAN, kMAll},
  {"SOTA spot", MK_ACTION, nullptr, 0,0,0, nullptr, nullptr, nullptr, ACT_SOTA_SPOT, kMAll},
};
constexpr int kMenuCount = (int)(sizeof(kMenu)/sizeof(kMenu[0]));
// ★ 2026-09-27 (firmware SOTA): el ORDEN EN PANTALLA es el de este array, no el de
//   kMenu[]: cada seccion dice donde empiezan y acaban sus items con
//   kMenuSectionFirst/kMenuSectionEnd (indices ABSOLUTOS en kMenu[]). Asi "SOTA" y
//   "Messages" suben arriba del todo (justo debajo de "Sleep") sin mover la tabla.
const char *kMenuSections[] = {
  "SOTA","Messages","Mode","GPS","Beacons","Tracker","Radio","Digi","APRS",
  "Bluetooth","Sensors","Display","Power","Remote","Settings", nullptr
};
// Secciones que el codigo trata a parte (por su posicion en kMenuSections).
constexpr int kSecSota = 0;
constexpr int kSecMode = 2;   // "Mode" abre directamente la lista de modos (kMenu[0])
// item index of the first item of each section
// ★ OJO (2026-09-15): estos dos arrays van por INDICE ABSOLUTO dentro de kMenu[], asi
//   que al anadir "WX cada (min)" en Sensores (posicion 44) hay que desplazar +1 todas
//   las secciones siguientes: Pantalla 46->47, Energia 49->50, Remoto 53->54, Ajustes 56->57.
// ★ 2026-09-26: desplazado +1 desde "Tracker" en adelante por el nuevo item
//   "Spot SOTA" al final de Mensajes (posicion 12). Antes: {0,1,5,10,12,18,
//   24,27,39,41,47,50,54,57}. Mismo patron que el desplazamiento de
//   "WX cada (min)" documentado en el aviso de mas abajo -- si se anade otro
//   item, se recomputa con el mismo metodo (no a mano: un guion de un
//   caracter aqui no se nota hasta que el menu ensena la seccion equivocada).
// ★ 2026-09-27: "SOTA spot" sale de Mensajes (posicion 12) y la seccion SOTA va al
//   final de kMenu[] (60..61): todo lo que estaba detras de Mensajes baja -1, y el
//   array sigue el orden de kMenuSections (SOTA, Messages, Mode, GPS...).
const int kMenuSectionFirst[] = {60,10,0,1,5,12,18,24,27,39,41,47,50,54,57};

// --------------- menu state ---------------
bool gMenuOn = false;
bool gMenuEditing = false;
int gMenuCat = -1;        // -1 = category list, else section index
int gMenuIdx = 0;         // selected row
char gEditBuf[32] = "";   // buffer while editing a value
int gEditPos = 0;
int gMenuEditItemAbs = 0; // indice absoluto del item que se esta editando
uint32_t gMenuLastActMs = 0;  // ultima interaccion con el menu (para el auto-cierre, kMenuInactivoMs)
// ★ SUBMENU DE PERFILES (2026-09-15): editor dedicado para 1) elegir el perfil activo y
//   2) editar su SSID/tiempos/metros. gMenuPerfMode: 0=off, 1=lista de perfiles,
//   2=editar los campos del perfil seleccionado.
int gMenuPerfMode = 0;
int gMenuPerfIdx = 0;   // 0..3 (fijo, peaton, bici, coche) o fila de campos
int gMenuPerfField = 0; // 0=SSID 1=slow 2=fast 3=dist 4=ICONO (ver kPerfCampos)
// ★ CAMPOS DEL PERFIL (2026-09-15): eran CUATRO y ahora son CINCO. El quinto es el
//   ICONO DEL MAPA, que va con el perfil (config.h: profileSymbol/profileOverlay).
//   El numero se usa en la navegacion (menuNavigate/menuShort) y en el dibujo
//   (menuPinta): si se anade otro campo, se cambia AQUI y en las etiquetas de
//   menuPinta, y los tres sitios siguen de acuerdo.
constexpr int kPerfCampos = 5;
// ★ SUBMENU DE OPCIONES (2026-09-15): para los enum (Modo, TNC, Frecuencia...) se abre un
//   submenu donde CADA opcion ocupa una linea, con "Volver" y "Salir" encima, la activa
//   marcada con ">", y scroll. Misma letra y estilo que el resto del menu.
int gMenuEnumAbs = -1;      // item cuyo submenu de opciones esta abierto
int gMenuEnumIdx = 0;       // fila del submenu: 0=Volver 1=Salir 2+=opcion
// ★ CONFIRMACION DE ACCIONES DESTRUCTIVAS (2026-09-15, arreglo de la auditoria G2).
//   Mismo par de variables que la OLED (display.cpp: gConfirmAction/gConfirmUntil): la
//   accion NO se ejecuta a la primera pulsacion; se pide confirmacion y solo la
//   SEGUNDA pulsacion de la MISMA accion dentro de la ventana la lleva a cabo.
//   ★★ CUAL PULSACION, EN ESTA PANTALLA: LA CORTA (corregido el 2026-09-16; hasta entonces
//      estos comentarios decian "larga", que es justo el gesto que CANCELA). Aqui el mapa es
//      CORTO = entrar/ejecutar/confirmar y LARGO = volver atras, al reves que en la OLED:
//      ver el comentario largo de la pantalla de confirmacion (menuPinta) y menuLong().
//   0 = no hay nada pendiente (ACT_NONE).
int gConfirmAction = 0;
uint32_t gConfirmUntil = 0;

// Valor ACTUAL de la clave `it` como texto (para mostrar a la derecha del ítem).
// Para los enum se muestra la ETIQUETA (opts[i]) que coincide con el valor actual.
const char *menuValorTexto(const MenuItem &it) {
  static char buf[24];
  if (!it.key) { buf[0]='\0'; return buf; }
  JsonDocument doc;
  configToJson(*gCfg, doc.to<JsonObject>());
  JsonVariantConst v = doc[it.key];
  if (v.isNull()) { snprintf(buf,sizeof buf,"?"); return buf; }
  if (it.opts) {
    // buscar la ETIQUETA de la opcion que coincide con el valor actual (int o float)
    if (v.is<int>() && it.optVals) {
      int val = v.as<int>();
      for (int i=0; it.opts[i]; i++) if (it.optVals[i]==val) return it.opts[i];
    } else if ((v.is<float>()||v.is<int>()) && it.optValsF) {
      float val = v.as<float>();
      for (int i=0; it.opts[i]; i++)
        if (fabsf(it.optValsF[i] - val) < 0.001f) return it.opts[i];
    } else if (it.kind == MK_PATH) {
      const char *s = v.as<const char*>();
      for (int i=0; it.opts[i]; i++) if (s && !strcmp(it.opts[i], s)) return it.opts[i];
    }
    // si no hay match exacto (p.ej. valor numerico que no esta en las opts), cae abajo
  }
  if (v.is<bool>())      snprintf(buf,sizeof buf, "%s", v.as<bool>() ? "Yes" : "No");
  else if (v.is<float>()||v.is<double>()) snprintf(buf,sizeof buf, "%.2f", v.as<float>());
  else if (v.is<int>())  snprintf(buf,sizeof buf, "%d", v.as<int>());
  else { const char *s=v.as<const char*>(); return s?s:""; }
  return buf;
}

// ★★ EL PROTOCOLO DEL TNC, EN PALABRA Y NO EN NUMERO (2026-09-15) ★★
// La configuracion guarda `tncProtocol` como NUMERO (config.h: 0 = OFF, 1 = TNC2,
// 2 = KISS) y el operador quiere LEERLO en la pantalla como palabra.
//   ★ LA TABLA ES CORTA A PROPOSITO: la palabra se pinta en una fila de la lista, que
//     va a escala 2, o sea 6*2 = 12 px por caracter y la fila empieza en x=14: caben
//     15 caracteres de los 200 del panel. Con el "TNC: " delante, lo mas largo es
//     "TNC: TNC2" / "TNC: KISS" (9 caracteres = 108 px), asi que sobra sitio. NO se
//     usan las etiquetas largas del submenu ("Apagado", "TNC2 texto"): esas viven en
//     kTncOpts y alli si caben, porque el submenu pinta una opcion por linea.
//   ★ SE BUSCA POR VALOR, NO POR INDICE: si la configuracion trae un numero que no es
//     ninguno de los tres (lo puede dejar un configurador tocado a mano), se contesta
//     "?" en vez de ensenar una palabra que no le corresponde. El valor se lee de gCfg,
//     que es la MISMA copia que usa el resto del firmware: no hay una segunda verdad.
const char *tncProtocoloPalabra() {
  const int v = gCfg ? (int)gCfg->tncProtocol : (int)CFG_TNC_OFF;
  switch (v) {
    case CFG_TNC_OFF:  return "OFF";
    case CFG_TNC_TNC2: return "TNC2";
    case CFG_TNC_KISS: return "KISS";
    default:           return "?";
  }
}

// Guarda un valor con cliTypedSet + storeSave (mismo motor que `set`, en cli.cpp).
bool menuSave(const char *key, const char *val) {
  if (!gCfg || !key) return false;
  String err;
  bool ok = cliTypedSet(*gCfg, String(key), String(val ? val : ""), err);
  // ★ EL GUARDADO EN LA FLASH VA APARTE DE LA TRAZA (2026-09-16): estaban en la MISMA
  //   linea (`if (ok) { storeSave(*gCfg); Serial.printf(...); }`) y al agrupar la traza en
  //   el modo diagnostico se lo llevaba por delante. Aqui NO se toca: guardar es la funcion
  //   de esta funcion; lo unico que se calla es el aviso por el USB.
  if (ok) storeSave(*gCfg);
  if (diagTrazaTaller()) {
    if (ok) Serial.printf("MENU %s=%s ok\r\n", key, val?val:"");
    else    Serial.printf("MENU %s=%s ERR %s\r\n", key, val?val:"", err.c_str());
  }
  return ok;
}

// ---- ICONO DEL PERFIL: leer / ciclar / guardar (ver kIconoNombre arriba) -----
// El valor VIVE en gCfg (profileOverlay[i] + profileSymbol[i]), que es lo que lee
// la baliza (aprs.cpp). Aqui NO hay una segunda copia del ajuste: si el operador
// lo cambia desde el configurador web, el menu ensena el valor nuevo en el acto.
//
// ★ NO se pasa por cliTypedSet()/configFromJson(): `profileSymbol` no es una clave
//   suelta de la configuracion (es un array de cuatro), asi que el motor de `set`
//   no la conoce. Se escribe en la configuracion y se guarda con storeSave(), que
//   es el mismo final del camino (la copia se serializa entera a la flash).
//
// El orden de la rueda (repetidor, persona, bici, coche) es el de kIconoNombre.
const char kIconoCodigo[kIconoOpciones] = {'#', '[', 'b', '>'};
// La tabla de los cuatro por defecto: primaria ('/'). Ver config.h.
const char kIconoTabla[kIconoOpciones] = {'/', '/', '/', '/'};

void iconoPerfilTexto(int perfil, char *out, size_t n) {
  if (!out || n == 0) return;
  out[0] = '\0';
  if (!gCfg || perfil < 0 || perfil > 3) return;
  snprintf(out, n, "%c%c", gCfg->profileOverlay[perfil][0], gCfg->profileSymbol[perfil][0]);
}

int iconoPerfilIndice(int perfil) {
  if (!gCfg || perfil < 0 || perfil > 3) return -1;
  for (int i = 0; i < kIconoOpciones; i++) {
    if (gCfg->profileSymbol[perfil][0] == kIconoCodigo[i]) return i;
  }
  return -1;   // un icono que no es de los cuatro (p.ej. puesto por el configurador)
}

void iconoPerfilPon(int perfil, int idx) {
  if (!gCfg || perfil < 0 || perfil > 3) return;
  if (idx < 0 || idx >= kIconoOpciones) return;
  gCfg->profileSymbol[perfil][0] = kIconoCodigo[idx];
  gCfg->profileSymbol[perfil][1] = '\0';
  // Los cuatro iconos son de la TABLA PRIMARIA ('/'). Se reafirma a proposito: si
  // el perfil traia una tabla rara de una configuracion manipulada, al elegir un
  // icono desde el menu queda coherente con lo que se ve en la pantalla.
  gCfg->profileOverlay[perfil][0] = kIconoTabla[idx];
  gCfg->profileOverlay[perfil][1] = '\0';
}

}  // namespace (datos del menu)

// ---------------------------------------------------------------------------
//  MOTOR DEL MENU (funciones de display.h) — NAVEGACION + EDICION + ACCIONES.
//  Mapa de botones (operador, 2026-09-15): capacitivo = navegar / cambiar valor;
//  fisico corto = entrar / confirmar; fisico largo = volver atras. Pinta sobre gBuf.
//  Modelo de edicion: al entrar se edita el item; NAVEGAR (capacitivo) cambia y GUARDA
//  el valor (memoria + flash via typedSet/storeSave); CONFIRMAR (fisico corto) sale de la
//  edicion. Para BOOL, entrar o navegar alterna y guarda; confirmar sale.
// ---------------------------------------------------------------------------

#define kMenuSectores 16   // 15 secciones + el final (indice de kMenuSectionFirst)
// Mismo desplazamiento +1 que en kMenuSectionFirst por el item nuevo "WX cada (min)".
// ★ 2026-09-26: mismo desplazamiento que kMenuSectionFirst, ver el aviso de
//   alli. Antes: {1,5,10,12,18,24,27,39,41,47,50,54,57,60,60}.
// ★ 2026-09-27: mismo orden que kMenuSectionFirst (SOTA primero). La ultima entrada
//   repetida es "el final" (kMenuCount), como antes.
const int kMenuSectionEnd[] = {62,12,1,5,10,18,24,27,39,41,47,50,54,57,60,62};
static_assert(sizeof(kMenuSectionFirst) / sizeof(kMenuSectionFirst[0]) == kMenuSectores - 1,
              "una entrada de kMenuSectionFirst por seccion");
static_assert(sizeof(kMenuSectionEnd) / sizeof(kMenuSectionEnd[0]) == kMenuSectores,
              "una entrada de kMenuSectionEnd por seccion + el final");
// Si se anade o quita un item de kMenu[], esto salta y obliga a recalcular los dos arrays.
static_assert(kMenuCount == 62, "kMenu[] ha cambiado: recalcular kMenuSectionFirst/End");

bool menuIsOpen() { return gMenuOn; }
bool menuIsEditing() { return gMenuEditing; }

static bool menuItemVisible(const MenuItem &it) {
  (void)it;
  // 2026-09-15: se muestran TODOS los items siempre (sin ocultar por modo). La ocultacion
  // por modo era un detalle de la OLED; aqui simplifica el mapeo fila<->item y el operador
  // quiere ver todas las opciones. Si mas adelante se quiere ocultar las que no aplican,
  // se restaura la logica por mascara.
  return true;
}

// ★★ ITEMS QUE EN ESTA PLACA NO HACEN NADA (2026-09-15, arreglo de la auditoria G3) ★★
//
// El problema que arregla: hay tres ajustes que el menu de la tinta GUARDA en la flash y
// que despues NADIE lee en la tinta, asi que el operador los toca, ve que se guardan y cree
// que ha cambiado algo. La OLED ya lo dice por escrito en su tabla ("these two entries are
// INERT values in the stored config: the menu still shows and edits them, nothing reads
// them", refiriendose al Bluetooth): aqui se hace lo mismo, pero ADEMAS se avisa en la
// pantalla, que es donde el operador lo necesita.
//
// Por que no se ocultan: el ajuste es compartido con la OLED y con el configurador web (la
// configuracion vive en la misma flash), y quien tenga las dos placas espera encontrar la
// misma lista. Ocultarlos haria que el menu de la tinta pareciera incompleto.
//
// Se senala por CLAVE y no metiendo un campo nuevo en MenuItem: asi no hay que tocar las 60
// filas de kMenu (que ademas son datos con un orden de inicializacion delicado).
static bool avisoItemInerte(const char *key) {
  if (!key) return false;
  // "Apagar pantalla(s)": la tinta NO tiene apagado por tiempo. En la tinta electronica la
  // imagen es bistable (se queda sin gastar) y displaySleep() solo se usa al dormir el nodo;
  // cfg.screenTimeoutSecs no se lee en NINGUN sitio de este fichero.
  if (!strcmp(key, "screenTimeoutSecs")) return true;
  // "Avisos": cfg.popups solo lo lee la OLED (display.cpp). Aqui los avisos son la banda
  // "TX" y la linea de estado, y no se pueden desactivar con ese ajuste.
  if (!strcmp(key, "popups")) return true;
  // "Bluetooth" y "PIN BT": el BLE esta FUERA DE COMPILACION (src/ble_kiss.cpp.off), asi que
  // los dos valores son inertes en las DOS placas, no solo en esta.
  if (!strcmp(key, "bleEnabled") || !strcmp(key, "blePin")) return true;
  return false;
}

// Texto que se anade a la etiqueta en la lista. ★ TIENE QUE SER MUY CORTO: las filas se
// pintan a escala 2 (una letra ~12 px en un panel de 200), y "Apagar pantalla(s)" ya mide
// 19 caracteres = 228 px, o sea que se sale por la derecha EL SOLO. Por eso no cabe un
// "(no)" detras: se pone un asterisco de una letra y el significado se explica en la LEYENDA
// que se pinta debajo del titulo de la categoria (ver seccionTieneInerte), que es lo que hace
// que el aviso se entienda sin comerse la etiqueta.
static const char *etiquetaInerte(const char *key) {
  return avisoItemInerte(key) ? " *" : "";
}

// ¿Esta CATEGORIA tiene algun item inerte? Sirve para pintar la leyenda solo donde hace
// falta, en vez de en las 14 secciones.
static bool seccionTieneInerte(int catAbs) {
  if (catAbs < 0) return false;
  const int a = kMenuSectionFirst[catAbs];
  const int b = kMenuSectionEnd[catAbs];
  for (int i = a; i < b; i++) {
    if (avisoItemInerte(kMenu[i].key)) return true;
  }
  return false;
}

// ---- ACCIONES (menuEjecutaAccion) ----
// ★ CONFIRMACION: COPIADA DEL PATRON QUE YA FUNCIONA EN LA OLED (ver display.cpp:
//   menuIsDestructive() + gConfirmAction/gConfirmUntil + el manejo en menuLong).
//   En la OLED, la accion que apaga o borra no se ejecuta a la primera: se enseña
//   "Pulsa largo: confirmar" y solo una SEGUNDA pulsacion larga dentro de 3 s la hace.
//   En la tinta NO existia y una pulsacion larga de mas en "Borrado" borraba la
//   configuracion y reiniciaba sin preguntar (auditoria G2).
//
// OJO CON EL AVISO EN ESTA PANTALLA: cada refresco cuesta ~1,5 s, asi que un popup que
// caduque solo no vale (el operador se lo pierde). Al pedir la confirmacion se PINTA UNA
// PANTALLA DE CONFIRMACION ENTERA (ver menuPinta) y se queda fija; si vence la ventana, el
// aviso desaparece solo en el siguiente refresco.
//
// ★ LA VENTANA NO SON 3 S COMO EN LA OLED, SINO 15 (2026-09-15). El motivo es la pantalla,
//   no el patron: el aviso tarda ~1,5 s en aparecer (mas ~0,5-1 s hasta que displayRefresh
//   lo manda), asi que con 3 s al operador le quedaban ~1,5 s para LEER el aviso y volver a
//   pulsar: una carrera que se pierde. El patron es el mismo (segunda pulsacion CORTA de la
//   MISMA accion dentro de la ventana), solo cambia el tiempo. 15 s es ademas MENOS que
//   el auto-cierre del menu (kMenuInactivoMs, 60 s desde el 2026-09-26), asi que el aviso
//   nunca se queda mas rato que el menu.
constexpr uint32_t kConfirmMs = 15000;

// ★ DORMIR YA NO PIDE CONFIRMACION (peticion del operador, 2026-09-16).
//   Si el operador elige "Dormir" en el menu, el nodo se duerme y ya: la eleccion de la
//   fila del menu ES la confirmacion. Motivo: dormir NO es destructivo (no borra nada, no
//   apaga la radio para siempre y se sale dando al boton), y la pantalla de confirmacion
//   obligaba a dos pulsaciones para algo que se hace a menudo (p. ej. al guardar el
//   nodo en la mochila). El aviso de "Solo sin cable USB" se sigue dando donde toca (dentro
//   del case ACT_SLEEP), asi que si hay cable el operador se entera igual.
//   Lo que SI sigue pidiendo confirmacion: reiniciar, valores de fabrica, borrado total y
//   modo grabacion, que es donde una pulsacion de mas cuesta la configuracion.
static bool esAccionDestructiva(int act) {
  return act == ACT_REBOOT || act == ACT_RESET ||
         act == ACT_WIPE || act == ACT_DFU;
}

// Titulo del aviso de confirmacion. nullptr = esa accion no pide confirmacion.
// (ACT_SLEEP ya no aparece aqui a proposito: ver esAccionDestructiva().)
static const char *tituloConfirmacion(int act) {
  switch (act) {
    case ACT_REBOOT: return "REBOOT";
    case ACT_RESET:  return "FACTORY RESET";
    case ACT_WIPE:   return "WIPE ALL";
    case ACT_DFU:    return "DFU MODE";
    default:         return nullptr;
  }
}

// ¿Hay una confirmacion pendiente y todavia dentro de la ventana? La comparacion se hace
// en aritmetica SIN SIGNO (como en la OLED), que es inmune al desbordamiento de millis().
static bool confirmacionPendiente() {
  return gConfirmAction != 0 && (int32_t)(millis() - gConfirmUntil) < 0;
}

// Definida mas abajo, con el resto del asistente SOTA.
static bool sotaOpenSpot();

static void menuEjecutaAccion(int act) {
  if (!gCfg) return;

  // ★ PRIMERA pulsacion CORTA: NO se ejecuta, se PIDE CONFIRMACION.
  //   SEGUNDA pulsacion CORTA de la MISMA accion dentro de la ventana: se ejecuta.
  //   (La LARGA, mientras hay confirmacion pendiente, la CANCELA: ver menuLong.)
  //   Cualquier otra accion cancela la peticion pendiente y empieza de cero.
  if (esAccionDestructiva(act)) {
    if (confirmacionPendiente() && gConfirmAction == act) {
      gConfirmAction = 0;        // confirmada: se cae al switch y se ejecuta
    } else {
      gConfirmAction = act;
      gConfirmUntil = millis() + kConfirmMs;
      gLinea1[0] = '\0';         // que el aviso de la pantalla no lo tape un popup viejo
      gLinea2[0] = '\0';
      gDirty = true;
      return;
    }
  } else {
    gConfirmAction = 0;          // una accion normal cancela lo que hubiera pendiente
  }

  switch (act) {
    case ACT_BEACON:
      if (tncKissActive() && !tncKissPaused()) { snprintf(gLinea1,sizeof gLinea1,"KISS: app in control"); }
      else {
        int16_t st = aprsSendManualBeacon(*gCfg);
        snprintf(gLinea1,sizeof gLinea1, st==RADIOLIB_ERR_NONE ? "Beacon OK" : "Beacon ERR");
      }
      break;
    case ACT_TRACKER_BEACON:
      snprintf(gLinea1,sizeof gLinea1, trackerBeaconNow(*gCfg)==RADIOLIB_ERR_NONE ? "Trk beacon" : "No fix");
      break;
    case ACT_TELEM:
      snprintf(gLinea1,sizeof gLinea1, aprsSendTelemetry(*gCfg)==RADIOLIB_ERR_NONE ? "Telem OK" : "Telem ERR");
      break;
    case ACT_TELEM_META:
      snprintf(gLinea1,sizeof gLinea1, aprsSendTelemetryMeta(*gCfg)==RADIOLIB_ERR_NONE ? "Meta OK" : "Meta ERR");
      break;
    case ACT_MUTE: {
      gCfg->txDisabled = !gCfg->txDisabled;
      storeSave(*gCfg);
      radioSetMuted(gCfg->txDisabled);
      snprintf(gLinea1,sizeof gLinea1, gCfg->txDisabled ? "MUTE on" : "MUTE off");
      break;
    }
    case ACT_BAT: {
      float bv = sensorsBatteryVolt(gSens);
      snprintf(gLinea1,sizeof gLinea1, "Bat %.2fV INA %.2fV", (double)bv, (double)gSens.inaBusV);
      break;
    }
    case ACT_GPS_INFO: {
      const GpsData &g = gpsGet();
      if (g.fix) snprintf(gLinea1,sizeof gLinea1, "fix %u %.5f", (unsigned)g.sats, g.lat);
      else snprintf(gLinea1,sizeof gLinea1, "GPS no fix");
      break;
    }
    case ACT_SET_COORDS:
      // ★ FIJAR COORDS EN LA TINTA (2026-09-15): antes esto solo pintaba "No
      //   soportado en tinta" y no hacia nada. Ahora se lanza LA MISMA sesion que
      //   ya funcionaba en la OLED: la lleva el rastreador (trackerSetCoordsStart,
      //   ver tracker.h), porque es quien gobierna el GPS y quien decide cuando la
      //   posicion esta asentada. Esta pantalla solo da la orden, ensena el
      //   progreso (pintaSesionCoords) y guarda al final (displaySaveCoords).
      //
      //   ★ AQUI HABIA UNA COMPROBACION PREVIA QUE YA NO HACE FALTA (quitada el
      //   2026-09-15): si el nodo estaba en modo repetidor sin "GPS en repetidor",
      //   se avisaba "activa GPS en repetidor" y NO se arrancaba la captura. Aquel
      //   aviso existia porque la sesion no podia encender el GPS por su cuenta.
      //   Ahora SI puede (la sesion manda sobre el modo; ver el guardia de
      //   gpsManage() en tracker.cpp), asi que pedirle al operador que activara un
      //   ajuste a mano era pedirle algo que el aparato hace solo. Se arranca
      //   directamente y el GPS se enciende y se apaga solo.
      trackerSetCoordsStart();
      coordsPantallaInicia();
      break;
    case ACT_SOTA_PLAN:
      // Cierra el menu de ajustes y abre el asistente (ver mas abajo, justo
      // despues de menuClose()): son dos pantallas modales distintas, nunca
      // a la vez. Al terminar (o al salir) se vuelve a esta seccion del menu.
      menuClose();
      sotaOpen();
      break;
    case ACT_SOTA_SPOT:
      menuClose();
      if (!sotaOpenSpot()) {
        // Sin plan no hay spot: se avisa en el carrusel (el menu no pinta avisos).
        snprintf(gLinea1, sizeof gLinea1, "SOTA: no plan");
        gLinea2[0] = '\0';
        gLineaMs = millis();
      }
      break;
    case ACT_MSG_SEND: {
      String to = aprsLastHeardCall();
      if (gCfg->msgText[0]=='\0') snprintf(gLinea1,sizeof gLinea1, "Empty message");
      else if (to.length()==0) snprintf(gLinea1,sizeof gLinea1, "Nobody heard");
      else snprintf(gLinea1,sizeof gLinea1, aprsSendMessage(*gCfg, to.c_str(), gCfg->msgText)==RADIOLIB_ERR_NONE ? "Sent" : "Send failed");
      break;
    }
    case ACT_SLEEP: {
      if (powerUsbPresent()) {
        // ★ Con el USB conectado NO se dibuja la pantalla fija de dormido (dejaria el panel
        //   con el zzz pegado y al soltar el cable un estado raro): se avisa con un popup.
        //   Detalle que pidio el operador (2026-09-15).
        snprintf(gLinea1, sizeof gLinea1, "Only without USB"); gLinea2[0] = '\0';
        displayPopupWait("Only without USB cable", 3000);   // ~3 s, diseno hermosa
      } else {
        displaySleepScene();   // escena de dormido fija (tinta bistable) y System OFF
        powerSleepNow(*gCfg);
      }
      break;
    }
    case ACT_REBOOT: NVIC_SystemReset(); break;
    // ACT_DFU no tiene fila en kMenu (en esta placa el modo grabacion se pide por USB con
    // "dfu confirm", ver cli.cpp). La rama se deja porque esAccionDestructiva() ya lo
    // contempla y asi la lista de acciones destructivas queda completa, como en la OLED.
    case ACT_DFU: break;
    case ACT_PROFILES:
      // Abre el editor de perfiles (estado dedicado del menu de la tinta).
      gMenuPerfMode = 1; gMenuPerfIdx = 0; gMenuPerfField = 0;
      gMenuLastActMs = millis();
      gDirty = true;
      break;
    case ACT_RESET: {
      // Mismo camino que la OLED (display.cpp, ACT_RESET), pero con el terminador
      // EXPLICITO: aqui `strncpy` no lo ponia y luego se leia keepCall/keepMgrs con
      // strncpy otra vez, que recorre el origen hasta el '\0' (lectura fuera del buffer
      // si el indicativo o los operadores llenaban el campo entero).
      char keepCall[16], keepMgrs[64]; bool keepRemote = gCfg->remoteEnabled;
      strncpy(keepCall, gCfg->callsign, sizeof(keepCall) - 1);
      keepCall[sizeof(keepCall) - 1] = '\0';
      strncpy(keepMgrs, gCfg->managers, sizeof(keepMgrs) - 1);
      keepMgrs[sizeof(keepMgrs) - 1] = '\0';
      *gCfg = DigiConfig();
      strncpy(gCfg->callsign, keepCall, sizeof(gCfg->callsign) - 1);
      gCfg->callsign[sizeof(gCfg->callsign) - 1] = '\0';
      strncpy(gCfg->managers, keepMgrs, sizeof(gCfg->managers) - 1);
      gCfg->managers[sizeof(gCfg->managers) - 1] = '\0';
      gCfg->remoteEnabled = keepRemote;
      storeSave(*gCfg);
      snprintf(gLinea1,sizeof gLinea1, "Reset OK");
      break;
    }
    case ACT_WIPE:
      storeWipe(); *gCfg = DigiConfig(); storeSave(*gCfg); NVIC_SystemReset();
      break;
    default: break;
  }
  gLineaMs = millis();
  gDirty = true;
}

// ---- EDICION ----
static void comenzarEdicion(const MenuItem &it) {
  gMenuEditing = true;
  // Carga el valor ACTUAL en el buffer (ARREGLO G1 de la auditoria 2026-09-15): sin esto,
  // editar un string (callsign, symbol, msgText...) arrancaba vacio y no dejaba ver/corregir
  // lo que hay. Se rellena aqui para todos los tipos editables; para string/float se usa.
  strncpy(gEditBuf, menuValorTexto(it), sizeof(gEditBuf) - 1);
  gEditBuf[sizeof(gEditBuf) - 1] = '\0';
  gEditPos = 0;
  gDirty = true;
}

// ★★ EL NUMERO QUE SE VE TIENE QUE SER EL QUE SE ACABA DE GUARDAR (2026-09-15) ★★
// HALLAZGO al anadir el RESTAR (ver editaResta): `gEditBuf` solo se rellena al ENTRAR en
// el item (comenzarEdicion) y al girar la rueda de letras (MK_STRING). En los numeros
// quien guarda es menuSave() directamente en la configuracion, y NADIE volvia a escribir
// el buffer: la pantalla de edicion (menuPinta, "drawTextCenter(70, gEditBuf, 3)") seguia
// enseñando el valor de cuando se entro, asi que se podia tocar veinte veces y el numero
// grande no se movia aunque la configuracion si cambiaba. Con el sumar eso ya era malo;
// con el restar es peor: el operador no puede ver a donde va ni comprobar que la
// correccion a la baja ha entrado.
// Se rellena desde la MISMA copia que se acaba de guardar (gCfg, a traves de
// menuValorTexto, que es de donde salia el texto al entrar), asi que pantalla y
// configuracion no pueden discrepar. Y si menuSave() ha RECHAZADO el valor (fuera de
// rango para configFromJson), el buffer enseña el que de verdad hay, no el intento.
// Solo se llama en los tipos NUMERICOS: el texto tiene su propio buffer (la rueda).
static void refrescaBufferNumerico(const MenuItem &it) {
  snprintf(gEditBuf, sizeof gEditBuf, "%s", menuValorTexto(it));
}

// Cambia el valor del item (edición live) y GUARDA.
static void editaSiguiente(const MenuItem &it) {
  JsonDocument doc;
  configToJson(*gCfg, doc.to<JsonObject>());
  char buf[24];
  switch (it.kind) {
    case MK_BOOL: {
      bool b = doc[it.key] | false;
      menuSave(it.key, b ? "0" : "1");
      break;
    }
    case MK_INT: {
      long v = (long)(doc[it.key] | 0);
      long step = it.step ? it.step : 1;
      long nv = v + step; if (nv > it.max) nv = it.min; if (nv < it.min) nv = it.min;
      snprintf(buf,sizeof buf,"%ld",nv); menuSave(it.key, buf);
      break;
    }
    // ★★ TOPE DE RANGO EN LOS FLOAT (2026-09-15, arreglo de la auditoria G4) ★★
    //   Antes: `float nv = v + step;` a secas. No habia tope por ARRIBA ni por ABAJO, y
    //   ademas menuSave() NO valida rangos (guarda directo en gCfg + storeSave, a
    //   diferencia de cliTypedSet()/configFromJson(), que si validan). Resultado: con
    //   "Corr sonda" (limite +-5) o "Ajuste chip" (limite +-10) unos pocos toques dejaban
    //   un valor fuera de rango que el firmware aceptaba, y que luego el configurador web
    //   RECHAZABA al leerlo ("chipTempOffset -10..10"): la configuracion quedaba en un
    //   estado que el propio firmware no habria aceptado por el camino normal.
    //   Ahora se respeta el [min,max] del item: si el paso se sale por arriba, el valor da
    //   la vuelta al minimo (igual que hace el MK_INT de arriba y que la OLED), y si el
    //   valor guardado ya venia fuera de rango se mete dentro en el primer toque.
    //   Si un item no declara limites (min==max==0), no se toca nada: es el caso de
    //   Latitud/Longitud, que se validan en configFromJson (-90..90 / -180..180).
    case MK_FLOAT: {
      float v = doc[it.key] | 0.0f;
      float step = it.step ? (float)it.step : 0.1f;
      float nv = v + step;
      if (it.max > it.min) {                      // el item SI declara limites
        const float fmin = (float)it.min, fmax = (float)it.max;
        if (v < fmin || v > fmax) nv = fmin;      // venia fuera de rango: se mete dentro
        else if (nv > fmax) nv = fmin;            // tope por arriba: vuelve al minimo
        else if (nv < fmin) nv = fmin;            // tope por abajo
      }
      snprintf(buf,sizeof buf,"%.1f",(double)nv); menuSave(it.key, buf);
      break;
    }
    case MK_ENUM_CYCLE: case MK_ENUM: case MK_ENUM_F: {
      int n = 0; while (it.opts && it.opts[n]) n++;
      int idx = 0;
      if (it.optValsF && !it.optVals) {
        // opciones FLOAT (p.ej. ancho de banda): buscar el indice del valor float actual
        float v = doc[it.key] | 0.0f;
        for (int i=0;i<n;i++) if (fabsf(it.optValsF[i]-v) < 0.001f) { idx=i; break; }
        int ni = n ? (idx+1) % n : 0;
        // ARREGLO G3 (auditoria 2026-09-15): escribir el float SIN truncar (62.5, no "62"),
        // o la validacion de configFromJson (exige 62.5/125/250/500 exactos) lo rechaza.
        char fbuf[16];
        snprintf(fbuf,sizeof fbuf,"%.1f",(double)it.optValsF[ni]);
        menuSave(it.key, fbuf);
      } else if (it.optVals) {
        long v = (long)(doc[it.key] | 0);
        for (int i=0;i<n;i++) if (it.optVals[i]==v) { idx=i; break; }
        int ni = n ? (idx+1) % n : 0;
        snprintf(buf,sizeof buf,"%d", it.optVals[ni]);
        menuSave(it.key, buf);
      } else {
        long v = (long)(doc[it.key] | 0);
        idx = (v >= 0 && v < n) ? v : 0;
        int ni = (idx+1) % n;
        snprintf(buf,sizeof buf,"%d", ni);
        menuSave(it.key, buf);
      }
      break;
    }
    case MK_PATH: {
      const char *cur = menuValorTexto(it);
      int n=0; while (it.opts && it.opts[n]) n++;
      int idx=0; for (int i=0;i<n;i++) if(!strcmp(it.opts[i],cur)) { idx=i; break; }
      int ni = n ? (idx+1)%n : idx;
      menuSave(it.key, it.opts[ni]);
      break;
    }
    case MK_STRING: {
      // incrementar un carácter: se guarda en gEditBuf (sin persistir hasta confirmar)
      //
      // ★★ COMPROBADO AL ARREGLAR G5 (2026-09-15): EL SIGNO MENOS YA ESTABA AQUI ★★
      //   '-' figura en la rueda desde el principio (entre el '9' y el '/'), asi que los
      //   campos de TEXTO ya admitian negativos. Lo que NO admitia negativos era el editor
      //   de NUMEROS: MK_FLOAT no usa esta rueda, usa el atajo de "sumar 0.1" (ver
      //   editaSiguiente), y por eso "Latitud"/"Longitud"/"Corr sonda"/"Ajuste chip" se
      //   quedaban clavados en positivo. El arreglo de G5 es, por tanto, en el formato y
      //   en el rango de MK_FLOAT (y en el min/max de esos items en kMenu), NO aqui.
      //   Se deja escrito para que nadie vuelva a "anadir el menos" a esta rueda: ya esta.
      static const char kC[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/#.,_:~";
      String s = gEditBuf;
      if ((int)s.length() <= gEditPos) s += ' ';
      char c = s[gEditPos];
      int ci = 0; for (; kC[ci] && kC[ci]!=c; ci++);
      s[gEditPos] = kC[(ci+1) % (int)(sizeof(kC)-1)];
      strncpy(gEditBuf, s.c_str(), sizeof(gEditBuf)-1); gEditBuf[sizeof(gEditBuf)-1]='\0';
      gDirty = true;
      break;
    }
    case MK_HEADER: case MK_ACTION: break;
  }
  // ★ (2026-09-15) Que el numero grande de la pantalla de edicion sea el que se acaba de
  //   guardar (ver refrescaBufferNumerico). En el texto NO se toca el buffer: lo construye
  //   la rueda de letras y se persiste al confirmar con el fisico corto.
  if (it.kind == MK_INT || it.kind == MK_FLOAT) refrescaBufferNumerico(it);
}

// ★★ RESTAR EN LOS AJUSTES NUMERICOS (2026-09-15, peticion del operador) ★★
//
// EL PROBLEMA: el editor de numeros solo sumaba. El toque CAPACITIVO llama a
// menuNavigate() -> editaSiguiente(), y eso siempre suma un paso (+0,1 en los FLOAT),
// asi que un ajuste que empieza en 0 (Latitud, Longitud, Corr sonda, Ajuste chip) NO
// podia bajar a negativo desde el aparato: habia que ir al configurador web o al cable.
//
// GESTO ELEGIDO: DOBLE TOQUE FISICO mientras se esta editando. Mapa de gestos que hay
// hoy (button.cpp + main.cpp + este fichero) y por que ese y no otro:
//   - CAPACITIVO (menuNavigate, main.cpp:73-83): navega y suma el paso. Ahi solo llega
//     el FLANCO del toque (no su duracion), asi que no se puede distinguir toque corto
//     de toque largo: no hay sitio para un segundo sentido sin tocar main.cpp.
//   - FISICO CORTO (menuShort): entra en el item, CONFIRMA y sale de la edicion (y
//     persiste el texto). Es el gesto que mas se usa: no se le toca.
//   - FISICO LARGO (menuLong): volver atras, el gesto de "volver" de TODO el menu. En la
//     edicion hace exactamente lo mismo que menuEditCancel() (gMenuEditing = false).
//   - DOBLE TOQUE (menuEditCancel): hoy solo CANCELA la edicion, y main.cpp:117-120 lo
//     entrega SOLO si se esta editando (`if (menuIsEditing()) menuEditCancel();`).
// El doble toque es, por tanto, el gesto que MENOS conflictos crea: 1) fuera de la
// edicion no llega a este codigo, asi que no puede disparar nada de otras pantallas, y
// 2) NO quita ninguna capacidad, porque salir de la edicion sin guardar sigue estando en
// el toque LARGO con el mismo efecto (menuLong). Al corto y al largo no se les toca nada.
//   ★ CONFLICTO CONOCIDO (se dice, no se esconde): el toque corto tarda hasta 800 ms en
//     resolverse (button.cpp: kClickWindowMs espera a ver si el toque es doble). Si el
//     operador toca una vez para confirmar y, al no ver nada, toca otra vez, eso ES un
//     doble toque: en vez de confirmar, resta un paso. No se pierde nada (sigue dentro de
//     la edicion y sale con el largo), y la pantalla de edicion avisa del gesto (ver
//     menuPinta). ALTERNATIVA si aun asi molesta: dejar el doble toque como cancelar y
//     poner el RESTAR en el toque LARGO; se descarta porque el largo dejaria de ser
//     "volver" justo en la pantalla donde mas se usa.
//   ★ SOLO NUMEROS: en texto y path el doble toque sigue CANCELANDO la edicion (restar
//     texto no tiene sentido). Lo decide menuEditCancel(), que es quien filtra.
//
// TOPES: los MISMOS it.min/it.max de kMenu que usa el sumar, con su misma regla de rueda:
// si el paso se sale por abajo, el valor da la vuelta al maximo, igual que al sumar da la
// vuelta al minimo cuando se pasa por arriba (regla que ya estaba y NO se toca). El valor
// NUNCA queda fuera de [min,max], y si la configuracion traia un valor ya fuera de rango
// (lo puede dejar un configurador), el primer doble toque lo mete dentro. Los items sin
// limites declarados (min==max==0: Latitud/Longitud) se validan en configFromJson
// (-90..90 / -180..180), igual que al sumar.
static void editaResta(const MenuItem &it) {
  JsonDocument doc;
  configToJson(*gCfg, doc.to<JsonObject>());
  char buf[24];
  switch (it.kind) {
    case MK_INT: {
      long v = (long)(doc[it.key] | 0);
      long step = it.step ? it.step : 1;
      long nv = v - step;
      if (it.max > it.min) {                      // el item SI declara limites
        if (v < it.min || v > it.max) nv = it.max;   // venia fuera de rango: se mete dentro
        else if (nv < it.min) nv = it.max;           // tope por abajo: vuelve al maximo
        else if (nv > it.max) nv = it.max;           // red de seguridad
      }
      snprintf(buf,sizeof buf,"%ld",nv); menuSave(it.key, buf);
      break;
    }
    case MK_FLOAT: {
      float v = doc[it.key] | 0.0f;
      float step = it.step ? (float)it.step : 0.1f;
      float nv = v - step;
      if (it.max > it.min) {                      // el item SI declara limites
        const float fmin = (float)it.min, fmax = (float)it.max;
        if (v < fmin || v > fmax) nv = fmax;      // venia fuera de rango: se mete dentro
        else if (nv < fmin) nv = fmax;            // tope por abajo: vuelve al maximo
        else if (nv > fmax) nv = fmax;            // red de seguridad
      }
      snprintf(buf,sizeof buf,"%.1f",(double)nv); menuSave(it.key, buf);
      break;
    }
    default: break;   // texto/path/acciones no se restan (menuEditCancel los filtra antes)
  }
  // ★ (2026-09-15) Mismo remate que en editaSiguiente: lo que se ve tiene que ser lo que
  //   se acaba de guardar, o el operador resta a ciegas (ver refrescaBufferNumerico).
  if (it.kind == MK_INT || it.kind == MK_FLOAT) refrescaBufferNumerico(it);
}

// ---- NAVEGACIÓN / edit ----
// Modelo de filas (2026-09-15):
//   MENU PRINCIPAL (gMenuCat<0): filas = [Salir] + secciones[0..mid-1] + [Salir] +
//     secciones[mid..].  mid = n/2. Con n filas de secciones hay n+2 filas en total.
//   SUBMENU (gMenuCat>=0): filas = [Volver atras] + [Salir] + items.
static int menuMainSecCount() { int n=0; for(; kMenuSections[n]; n++) {} return n; }
// Modelo del MENU PRINCIPAL (2026-09-15): filas =
//   [0] Salir, [1] Dormir, secciones[0..mid-1], [kSalirMid] Salir, secciones[rest].
static int kSalirMid(int n)           { return 2 + n/2; }
static int menuMainTotFilas(int n)    { return n + 3; }
static bool menuMainFilaSalir(int r, int n) { return r==0 || r==kSalirMid(n); }
static bool menuMainEsDormir(int r)          { return r==1; }
static int  menuMainFilaSeccion(int r, int n) {
  if (menuMainFilaSalir(r, n) || menuMainEsDormir(r)) return -1;
  int mid = kSalirMid(n);
  return (r < mid) ? (r-2) : (r-3);
}

void menuNavigate() {
  if (!gMenuOn) return;
  gMenuLastActMs = millis();
  gUltimoToqueMs = millis();   // sello de "aqui ha habido mano del operador"
  gUltimoToqueAgrupaMs = millis();   // el tactil SI agrupa: puede ir en rafaga (Butter)
  displayBacklightKick();
  // ---- CONFIRMACION PENDIENTE: navegar NO hace nada ----
  // Mientras se esta preguntando "seguro?", mover el cursor seria invisible (la pantalla
  // enseña el aviso, no la lista) y ademas dejaria al operador sin saber donde esta al
  // volver. Se IGNORA el toque, igual que la OLED ignora el segundo "largo" cuando ya ha
  // pedido la confirmacion. Se sale confirmando (pulsacion CORTA del fisico: ver
  // menuEjecutaAccion) o dejando vencer la ventana. (Este comentario decia "largo" hasta el
  // 2026-09-16: era el mismo error que la pantalla, que mandaba pulsar largo, que es cancelar.)
  if (confirmacionPendiente()) return;
  // ---- SUBMENU DE OPCIONES (enum): navegar va por [Volver, Salir, opcion...] ----
  if (gMenuEnumAbs >= 0 && gMenuEnumAbs < kMenuCount) {
    const MenuItem &it = kMenu[gMenuEnumAbs];
    int nopt = 0; while (it.opts && it.opts[nopt]) nopt++;
    int total = 2 + nopt;                 // Volver(0) Salir(1) opciones(2..)
    gMenuEnumIdx = (gMenuEnumIdx + 1) % total;   // wrap: al llegar abajo vuelve a Volver
    gDirty = true; return;
  }
  // ---- EDITOR DE PERFILES ----
  if (gMenuPerfMode == 1) {        // 0..3 = perfil; 4 = "editar ajustes del activo"
    gMenuPerfIdx = (gMenuPerfIdx + 1) % 5;
    gDirty = true; return;
  }
  if (gMenuPerfMode == 2) {        // editar un campo del perfil: ciclo de valor
    int p = gMenuPerfIdx;
    if (gMenuPerfField == 0) {     // SSID 1..15 (anti-duplicado lo decide al guardar via config)
      int s = gCfg->profileSsid[p] + 1; if (s > 15) s = 0;
      gCfg->profileSsid[p] = (uint8_t)s;
    } else if (gMenuPerfField == 1) {  // tiempo lento (0..3600, paso 30)
      int v = gCfg->profileSlowSec[p] + 30; if (v > 3600) v = 0;
      gCfg->profileSlowSec[p] = v;
    } else if (gMenuPerfField == 2) {  // tiempo rapido
      int v = gCfg->profileFastSec[p] + 10; if (v > 3600) v = 0;
      gCfg->profileFastSec[p] = v;
    } else if (gMenuPerfField == 3) {  // metros (0..5000, paso 50)
      int v = gCfg->profileDistM[p] + 50; if (v > 5000) v = 0;
      gCfg->profileDistM[p] = v;
    } else {                            // ★ ICONO DEL MAPA de este perfil (2026-09-15)
      // Se recorre la MISMA rueda que ensena la pantalla (kIconoNombre). Si el
      // valor guardado no es uno de los cuatro (lo puso el configurador), el
      // primer toque entra por el principio de la rueda en vez de quedarse quieto.
      int idx = iconoPerfilIndice(p);
      idx = (idx < 0) ? 0 : ((idx + 1) % kIconoOpciones);
      iconoPerfilPon(p, idx);
    }
    storeSave(*gCfg);
    gDirty = true; return;
  }
  if (gMenuEditing) {
    const MenuItem &it = kMenu[gMenuEditItemAbs];
    editaSiguiente(it);
    if (it.kind == MK_STRING) { gEditPos++; if (gEditPos > 15) gEditPos = 0; }
    gDirty = true;
    return;
  }
  if (gMenuCat < 0) {
    int n = menuMainSecCount();
    gMenuIdx = (gMenuIdx + 1) % menuMainTotFilas(n);   // wrap
  } else {
    int a = kMenuSectionFirst[gMenuCat];
    int b = kMenuSectionEnd[gMenuCat];
    int itemCount = b - a;
    int total = itemCount + 2;                  // + Volver + Salir
    // lineas: 0=Volver, 1=Salir, 2..=items
    int idx = gMenuIdx; int guard = total;
    do { idx = (idx + 1) % total; guard--; }
    while (guard > 0 && idx >= 2 && idx < total && !menuItemVisible(kMenu[a + (idx-2)]));
    gMenuIdx = idx;
  }
  gDirty = true;
}

void menuShort() {
  if (!gMenuOn) return;
  gMenuLastActMs = millis();
  displayBacklightKick();
  // ---- SUBMENU DE OPCIONES (enum) ----
  if (gMenuEnumAbs >= 0 && gMenuEnumAbs < kMenuCount) {
    const MenuItem &it = kMenu[gMenuEnumAbs];
    if (gMenuEnumIdx == 0) { gMenuEnumAbs = -1; gDirty = true; return; }   // Volver
    if (gMenuEnumIdx == 1) { menuClose(); return; }                        // Salir
    // guardar la opcion elegida (la fila 2+i)
    int optIdx = gMenuEnumIdx - 2;
    char buf[24];
    if (it.optVals) snprintf(buf,sizeof buf,"%d", it.optVals[optIdx]);
    else if (it.optValsF) snprintf(buf,sizeof buf,"%.1f",(double)it.optValsF[optIdx]);
    else snprintf(buf,sizeof buf,"%d", optIdx);
    menuSave(it.key, buf);
    gMenuEnumAbs = -1;
    gDirty = true;
    return;
  }
  // ---- EDITOR DE PERFILES ----
  if (gMenuPerfMode == 1) {
    if (gMenuPerfIdx < 4) {
      gCfg->smartBeaconPreset = (uint8_t)gMenuPerfIdx;   // elegir perfil = hacerlo activo
      storeSave(*gCfg);
      gDirty = true; return;
    } else {
      int activo = gCfg->smartBeaconPreset; if (activo > 3) activo = 0;
      gMenuPerfIdx = activo; gMenuPerfField = 0; gMenuPerfMode = 2;   // editar el activo
      gDirty = true; return;
    }
  }
  if (gMenuPerfMode == 2) {
    gMenuPerfField++; if (gMenuPerfField > kPerfCampos - 1) { gMenuPerfField = 0; gMenuPerfMode = 1; }
    gDirty = true; return;
  }
  if (gMenuEditing) {
    // ARREGLO G1 (auditoria 2026-09-15): al confirmar una edicion de STRING se debe
    // PERSISTIR el buffer. Para INT/ENUM/PATH/FLOAT el valor ya se guardo "live" en
    // cada navegacion; para STRING el buffer se construye en memoria y aqui se escribe.
    if (gMenuEditItemAbs >= 0 && gMenuEditItemAbs < kMenuCount) {
      const MenuItem &it = kMenu[gMenuEditItemAbs];
      if (it.kind == MK_STRING) {
        char cleaned[sizeof(gEditBuf)];
        int p = 0;
        for (int i=0; gEditBuf[i] && gEditBuf[i]!='~' && i<(int)sizeof(cleaned)-1; i++)
          cleaned[p++] = gEditBuf[i];
        cleaned[p] = '\0';
        menuSave(it.key, cleaned);
      }
    }
    gMenuEditing = false;
    gDirty = true;
    return;
  }
  if (gMenuCat < 0) {
    int n = menuMainSecCount();
    if (menuMainFilaSalir(gMenuIdx, n)) { menuClose(); return; }
    // Dormir (fila virtual del menu principal): pasa por el MISMO camino que las demas
    // acciones, asi que tambien pide confirmacion (auditoria G2).
    if (menuMainEsDormir(gMenuIdx)) { menuEjecutaAccion(ACT_SLEEP); return; }
    int sec = menuMainFilaSeccion(gMenuIdx, n);
    // ★ SECCION "MODO" abre DIRECTAMENTE la lista de modos (2026-09-15): como esa seccion
    //   solo tiene el ítem "Modo", se salta un nivel y van los modos por linea, sin la
    //   pantalla intermedia de "Modo -> Modo".
    if (sec == kSecMode) {
      gMenuEnumAbs = 0;              // ítem "Modo" (kMenu[0])
      gMenuEnumIdx = 2;
      gDirty = true; return;
    }
    gMenuCat = sec; gMenuIdx = 0; gDirty = true;
    return;
  }
  // submenu
  int a = kMenuSectionFirst[gMenuCat];
  int b = kMenuSectionEnd[gMenuCat];
  int idx = gMenuIdx;
  if (idx == 0) { gMenuCat = -1; gMenuIdx = 0; gDirty = true; return; }      // Volver atras
  if (idx == 1) { menuClose(); return; }                                     // Salir
  int iAbs = idx - 2;
  if (iAbs < (b - a)) {
    const MenuItem &it = kMenu[a + iAbs];
    gMenuEditItemAbs = a + iAbs;
    if (it.kind == MK_ACTION) { menuEjecutaAccion(it.action); }
    else if (it.kind == MK_BOOL) { editaSiguiente(it); }
    else if (it.kind == MK_ENUM_CYCLE || it.kind == MK_ENUM || it.kind == MK_ENUM_F) {
      gMenuEnumAbs = a + iAbs;
      gMenuEnumIdx = 2;
      // DIAGNOSTICO (2026-09-15): ver en el USB que opciones tiene el submenu, porque se
      // reporto que en "Modo" solo sale un "Modo". Netif enhorabuena.
      // Va con el modo diagnostico (2026-09-16): es una traza de taller, no algo que tenga
      // que ver el operador cada vez que abre un desplegable.
      if (diagTrazaTaller()) {
        int nd = 0; while (it.opts && it.opts[nd]) nd++;
        Serial.printf("MENU enum %s key=%s nopt=%d | %s / %s / %s\r\n",
                      it.label, it.key ? it.key : "?", nd,
                      (nd>0)?it.opts[0]:"", (nd>1)?it.opts[1]:"", (nd>2)?it.opts[2]:"");
      }
      gDirty = true;
    }
    else { comenzarEdicion(it); }
  }
}

void menuLong() {
  if (!gMenuOn) return;
  gMenuLastActMs = millis();
  displayBacklightKick();
  // ---- CONFIRMACION PENDIENTE: largo = cancelar (no se ejecuta nada) ----
  // Se mira ANTES que lo demas: la pulsacion larga en la pantalla de confirmacion es un
  // "no". Es el equivalente al gesto con el que la OLED cancela (moverse a otra fila).
  if (confirmacionPendiente()) { gConfirmAction = 0; gDirty = true; return; }
  // ---- SUBMENU DE OPCIONES: largo = volver ----
  if (gMenuEnumAbs >= 0) { gMenuEnumAbs = -1; gDirty = true; return; }
  // ---- EDITOR DE PERFILES: largo = volver un nivel ----
  if (gMenuPerfMode == 2) { gMenuPerfMode = 1; gDirty = true; return; }
  if (gMenuPerfMode == 1) { gMenuPerfMode = 0; gDirty = true; return; }
  if (gMenuEditing) { gMenuEditing = false; gDirty = true; return; }
  if (gMenuCat < 0) { menuClose(); }
  else { gMenuCat = -1; gMenuIdx = 0; gDirty = true; }   // vuelve a categorias
}

void menuOpen() { gMenuOn = true; gMenuCat = -1; gMenuIdx = 0; gMenuPerfMode = 0; gMenuLastActMs = millis(); displayBacklightKick(); gDirty = true; }
// Al cerrar se olvida cualquier confirmacion pendiente: si no, al volver a entrar en el
// menu con la ventana de 3 s todavia viva, la primera pulsacion larga ejecutaria el
// borrado sin preguntar (justo lo que este arreglo viene a impedir).
void menuClose() { gMenuOn = false; gMenuEditing = false; gMenuIdx = 0; gMenuPerfMode = 0; gConfirmAction = 0; gConfirmUntil = 0; gDirty = true; }

// ===========================================================================
//  ★★ ASISTENTE SOTA (2026-09-26; plan + spot desde el 2026-09-27) ★★
//  La logica de datos y navegacion vive en sota.h/sota.cpp (sin String, sin
//  hardware): aqui SOLO se conecta esa maquina de estados a la pantalla y a
//  los botones, con el MISMO mapa de gestos que el resto del menu (capacitivo
//  = navegar, fisico corto = confirmar/entrar, fisico largo = volver atras) y
//  el mismo timeout de inactividad (kMenuInactivoMs, 60 s). Se abre desde la
//  seccion "SOTA" del menu, con dos recorridos:
//    - "Plan activation" (ACT_SOTA_PLAN -> sotaOpen): al llegar a DONE se
//      guarda el plan en gCfg->sotaPlan y se VUELVE a la seccion SOTA del menu.
//    - "SOTA spot" (ACT_SOTA_SPOT -> sotaOpenSpot): parte del plan guardado,
//      pide el comentario y al llegar a DONE lo manda con aprsSendMessage()
//      (la MISMA funcion que usa el resto del firmware) y cierra al carrusel.
// ===========================================================================
bool gSotaOpen = false;
uint32_t gSotaLastActMs = 0;

bool sotaIsOpen() { return gSotaOpen; }

static void sotaAbierto() {
  gSotaOpen = true;
  gSotaLastActMs = millis();
  displayBacklightKick();
  gDirty = true;
}

// Recorrido PLAN DE ACTIVACION.
void sotaOpen() {
  sotaWizardOpen(gCfg ? gCfg->callsign : nullptr,
                 gCfg ? gCfg->sotaRecent : nullptr,
                 gCfg ? gCfg->sotaLast : nullptr);
  sotaAbierto();
}

// Recorrido SPOT. false = no hay plan guardado (no se abre nada).
static bool sotaOpenSpot() {
  if (!gCfg || !sotaWizardOpenSpot(gCfg->callsign, gCfg->sotaPlan)) return false;
  sotaAbierto();
  return true;
}

void sotaClose() {
  gSotaOpen = false;
  gDirty = true;
}

// Cierra el asistente y deja el menu abierto en la seccion SOTA, en la fila
// del item `itemRel` (0 = "Plan activation", 1 = "SOTA spot").
static void sotaVuelveAlMenu(int itemRel) {
  sotaClose();
  menuOpen();
  gMenuCat = kSecSota;
  gMenuIdx = 2 + itemRel;   // filas: 0 = Volver, 1 = Salir, 2.. = items
}

// Guarda el plan (y las recientes / la fila "Re ...") en la flash, solo si
// algo cambia: repetir el mismo plan no gasta un borrado de pagina.
static void sotaGuardarPlan() {
  if (!gCfg) return;
  char plan[sizeof(gCfg->sotaPlan)];
  char rec[sizeof(gCfg->sotaRecent)];
  char last[sizeof(gCfg->sotaLast)];
  sotaWizardPlan(plan, sizeof(plan));
  sotaWizardRecents(rec, sizeof(rec));
  sotaWizardLastSpot(last, sizeof(last));
  if (strcmp(plan, gCfg->sotaPlan) != 0 || strcmp(rec, gCfg->sotaRecent) != 0 ||
      strcmp(last, gCfg->sotaLast) != 0) {
    snprintf(gCfg->sotaPlan, sizeof(gCfg->sotaPlan), "%s", plan);
    snprintf(gCfg->sotaRecent, sizeof(gCfg->sotaRecent), "%s", rec);
    snprintf(gCfg->sotaLast, sizeof(gCfg->sotaLast), "%s", last);
    storeSave(*gCfg);
  }
}

// Construye el mensaje, lo envia a "APRS2SOTA" y avisa por el mismo camino
// que el resto de acciones de radio (gLinea1, ver menuEjecutaAccion ACT_*).
static void sotaEnviarSpot() {
  char msg[80];
  sotaWizardMessage(msg, sizeof(msg));
  int16_t st = aprsSendMessage(*gCfg, kSotaPasarela, msg);
  snprintf(gLinea1, sizeof(gLinea1), st == RADIOLIB_ERR_NONE ? "SOTA: sent" : "SOTA: TX failed");
  gSpot = (st == RADIOLIB_ERR_NONE) ? SPOT_ENVIANDO : SPOT_NO_ENVIADO;
  gLinea2[0] = '\0';
  gLineaMs = millis();
}

void sotaNavigate() {
  if (!gSotaOpen) return;
  gSotaLastActMs = millis();
  displayBacklightKick();
  sotaWizardNavigate(1);
  gDirty = true;
}

void sotaShort() {
  if (!gSotaOpen) return;
  gSotaLastActMs = millis();
  displayBacklightKick();
  if (!sotaWizardConfirm()) {
    // sotaWizardConfirm() devuelve false EXACTAMENTE al llegar a DONE (ver
    // sota.h): ahi, y solo ahi, se envia (spot) o se guarda (plan).
    if (sotaWizardIsSpot()) {
      sotaEnviarSpot();
      sotaClose();
    } else {
      sotaGuardarPlan();
      sotaVuelveAlMenu(0);
    }
    return;
  }
  gDirty = true;
}

void sotaLong() {
  if (!gSotaOpen) return;
  gSotaLastActMs = millis();
  displayBacklightKick();
  // Ya estaba en el primer paso: largo = salir, de vuelta a la seccion SOTA.
  if (!sotaWizardBack()) sotaVuelveAlMenu(sotaWizardIsSpot() ? 1 : 0);
  else gDirty = true;
}

// ★★ DOBLE TOQUE = RESTAR (2026-09-15) ★★
// Esta funcion es el UNICO camino por el que entra el DOBLE TOQUE fisico en el menu:
// main.cpp (handleButton, rama BTN_DOUBLE) solo la llama `if (menuIsEditing())`, asi que
// desde aqui no se puede estropear nada de las otras pantallas. El porque del gesto y el
// mapa completo de botones estan contados en editaResta().
//   - Campo NUMERICO (MK_INT/MK_FLOAT): resta un paso y SE QUEDA editando, para poder
//     seguir corrigiendo con el capacitivo y ver el valor nuevo en el acto.
//   - Cualquier otro campo (texto, path): CANCELA la edicion, como hasta hoy.
// Lo que NO se toca: la cancelacion de la sesion "Fijar coords" (vive en displayRefresh y
// mira gUltimoToqueMs, que aqui no se escribe) ni el antirrebote. Y el cierre del menu por
// inactividad (kMenuInactivoMs) se refresca a proposito cuando se resta: esto ES mano del operador y
// no debe contar como "tiempo sin tocar".
// Salir sin guardar tampoco se pierde en los numericos: el toque LARGO hace lo mismo que
// esta funcion hacia antes (menuLong: `if (gMenuEditing) { gMenuEditing = false; ... }`).
void menuEditCancel() {
  if (gMenuEditing && gMenuEditItemAbs >= 0 && gMenuEditItemAbs < kMenuCount) {
    const MenuItem &it = kMenu[gMenuEditItemAbs];
    if (it.kind == MK_INT || it.kind == MK_FLOAT) {
      editaResta(it);
      gMenuLastActMs = millis();   // ha habido mano del operador: el menu no se cierra por inactividad
      gDirty = true;
      return;
    }
  }
  gMenuEditing = false;   // texto/path (y demas casos): sigue siendo CANCELAR la edicion
  gDirty = true;
}

// ---- RENDER ----
// Auto-cierre del menu sin tocar (peticion del operador, 2026-09-15): si nadie
// interactua, el menu se cierra solo y la pantalla vuelve al carrusel.
// ★ 2026-09-26: 15 s -> 60 s (peticion del operador: se cerraba mientras aun lo estaba
//   leyendo). La misma constante vale para el asistente SOTA (pintaSota).
constexpr uint32_t kMenuInactivoMs = 60000;
void menuPinta() {
  if (gMenuOn && (uint32_t)(millis() - gMenuLastActMs) > kMenuInactivoMs) {
    menuClose();
    return;
  }
  clearBuf(true);
  const int esc = 2;                       // letra mas grande (peticion del operador)
  const int rowH = 18;                     // alto de fila para escala 2
  const int y0 = 30;

  // ★★ PANTALLA DE CONFIRMACION (2026-09-15, arreglo de la auditoria G2) ★★
  //   En la OLED el aviso es el popup "Pulsa largo: confirmar", que dura lo que dura un
  //   popup. Aqui NO vale: el panel tarda ~1,5 s por refresco y el operador se lo perderia.
  //   Por eso el aviso es una PANTALLA ENTERA que se queda fija (tinta bistable) hasta que
  //   se confirme, se cancele con una pulsacion larga o venza la ventana, con la misma letra
  //   y el mismo estilo que el resto del menu:
  //       "CONFIRMAR"                          (escala 1, arriba)
  //       [nombre de la accion]                (escala 2, dentro de un recuadro)
  //       "pulsa CORTO otra vez"               (escala 1)
  //       "para confirmar"
  //       "fisico largo: cancelar"             (escala 1, abajo)
  //   Si vence la ventana (kConfirmMs) sin confirmar, esto se apaga solo en el siguiente
  //   refresco (menuPinta se llama desde displayRefresh) y vuelve a verse la lista.
  //
  //   ★★ OJO: AQUI PUSO "PULSA LARGO OTRA VEZ" Y ESTUVO MAL HASTA EL 2026-09-16 ★★
  //     Ese texto venia copiado de la OLED, donde el gesto que confirma SI es el largo. En
  //     esta pantalla es AL REVES, porque el mapa del menu de tinta es el otro (el que dice
  //     el manual): CORTO = entrar / ejecutar / confirmar; LARGO = volver atras, y aqui
  //     volver atras es CANCELAR (ver menuLong: `if (confirmacionPendiente()) gConfirmAction = 0`).
  //     O sea que la pantalla mandaba hacer justo el gesto que cancela, y ademas se
  //     contradecia con su propia ultima linea ("fisico largo: cancelar"). Lo cazo el
  //     operador: «mantengo pulsado y solo sale del menu». NO es del Project Butter: el
  //     gesto no se ha tocado, lo que estaba mal era la instruccion.
  if (gMenuOn && confirmacionPendiente()) {
    const char *tit = tituloConfirmacion(gConfirmAction);
    if (tit) {
      drawTextCenter(16, "CONFIRM", 1);
      // Recuadro alrededor del nombre de la accion, para que se lea de un vistazo.
      const int tw = textWidth(tit, 2);
      int cx0 = (EPD_W - tw) / 2 - 8; if (cx0 < 4) cx0 = 4;
      int cx1 = (EPD_W + tw) / 2 + 8; if (cx1 > EPD_W - 4) cx1 = EPD_W - 4;
      barraH(cx0, cx1, 58, 2);
      barraH(cx0, cx1, 84, 2);
      barraV(cx0, 58, 84, 2);
      barraV(cx1, 58, 84, 2);
      drawTextCenter(64, tit, 2);
      drawTextCenter(120, "press SHORT again", 1);
      drawTextCenter(134, "to confirm", 1);
      drawTextCenter(168, "long press: cancel", 1);
      return;
    }
  }

  // ★ PANTALLA DE EDICION LIMPIA (2026-09-15): al editar un valor se dibuja SOLO esta
  //   pantalla (etiqueta + valor grande + cursor), NO el menu detras. Sustituye al antiguo
  //   banner que se montaba encima del menu arruinando la lectura.
  if (gMenuOn && gMenuEditing && gMenuEditItemAbs >= 0 && gMenuEditItemAbs < kMenuCount) {
    const MenuItem &it = kMenu[gMenuEditItemAbs];
    drawTextCenter(12, it.label, 2);
    // ★ AVISO DE AJUSTE QUE NO HACE NADA (2026-09-15, arreglo de la auditoria G3).
    //   Aqui es donde el operador se entera: no en la lista (donde el "(no)" se pierde
    //   entre comillas), sino al ENTRAR a tocarlo. Se dice que se guarda (es verdad: se
    //   guarda en la flash y lo puede usar la OLED o el configurador) pero que en esta
    //   placa no cambia nada. Se pinta ANTES del "return" de la pantalla de edicion para
    //   que salga en el mismo refresco.
    if (avisoItemInerte(it.key)) {
      drawTextCenter(34, "saved, but on this", 1);
      drawTextCenter(46, "display it does NOTHING", 1);
    }
    // el valor que se esta construyendo, en grande y centrado
    int ww = textWidth(gEditBuf, 3);
    if (ww > EPD_W - 16) ww = EPD_W - 16;
    drawTextCenter(70, gEditBuf, 3);
    int xx = (EPD_W - textWidth(gEditBuf, 3)) / 2; if (xx < 4) xx = 4;
    int cxl = xx + gEditPos * textWidth("W", 3);
    barraV(cxl, 96, 100, 6);              // subrayado del caracter que se edita
    // ★ PISTA DEL GESTO DE RESTAR (2026-09-15): el doble toque no tiene ninguna marca en
    //   la pantalla, asi que se dice aqui. SOLO en los campos numericos, que son los
    //   unicos en los que el doble toque resta: en el texto sigue siendo "cancelar la
    //   edicion" (ver menuEditCancel) y ponerlo ahi seria mentir. Escala 1 y centrado:
    //   "capacitivo suma, doble resta" son 29 caracteres = 174 px de los 200 del panel, y
    //   cae en el hueco que hay entre el cursor (y=96..100) y el pie (y=150).
    if (it.kind == MK_INT || it.kind == MK_FLOAT) {
      drawTextCenter(126, "touch: +  double: -", 1);
    }
    drawTextCenter(150, "long press: back", 1);
    return;
  }

  // ---- SUBMENU DE OPCIONES (enum): Volver/Salir arriba, cada opcion en su linea ----
  if (gMenuEnumAbs >= 0 && gMenuEnumAbs < kMenuCount) {
    const MenuItem &it = kMenu[gMenuEnumAbs];
    drawTextCenter(12, it.label, 1);
    int nopt = 0; while (it.opts && it.opts[nopt]) nopt++;
    int total = 2 + nopt;
    int vis = 8;
    int top = gMenuEnumIdx - (vis / 2); if (top < 0) top = 0;
    if (top > total - vis) top = (total - vis < 0) ? 0 : total - vis;
    int y = y0;
    for (int r = top; r < total && y < 186; r++, y += rowH) {
      if (r == gMenuEnumIdx) { barraH(6, EPD_W-6, y-1,1); barraH(6, EPD_W-6, y+rowH-2,1); barraV(6,y-1,y+rowH-2,1); barraV(EPD_W-6,y-1,y+rowH-2,1); }
      if (r == 0) { drawText(14, y, "< Back", esc); continue; }
      if (r == 1) { drawText(14, y, "Exit", esc); continue; }
      const char *opt = it.opts ? it.opts[r - 2] : "";
      const char *cur = menuValorTexto(it);
      bool activa = opt && !strcmp(opt, cur);
      char row[40];
      snprintf(row, sizeof(row), "%s%s", opt ? opt : "", activa ? "   >" : "");
      drawText(14, y, row, esc);
    }
    return;
  }

  // ---- EDITOR DE PERFILES (2026-09-15) ----
  const char *kPerfName[4] = {"Fixed/Digi","Walking","Bicycle","Car"};
  if (gMenuPerfMode == 1) {
    drawTextCenter(12, "PROFILES", 1);
    int y = y0;
    for (int i = 0; i < 4 && y < 160; i++, y += rowH) {
      if (i == gMenuPerfIdx) { barraH(6, EPD_W-6, y-1,1); barraH(6, EPD_W-6, y+rowH-2,1); barraV(6,y-1,y+rowH-2,1); barraV(EPD_W-6,y-1,y+rowH-2,1); }
      char row[40];
      snprintf(row, sizeof(row), "%s%s", kPerfName[i],
               (i == (int)gCfg->smartBeaconPreset) ? "  >" : "");
      drawText(14, y, row, 1);
    }
    // fila 5: editar ajustes del perfil activo
    if (4 == gMenuPerfIdx) { barraH(6, EPD_W-6, y-1,1); barraH(6, EPD_W-6, y+rowH-2,1); barraV(6,y-1,y+rowH-2,1); barraV(EPD_W-6,y-1,y+rowH-2,1); }
    drawText(14, y, "Edit settings", 1);
    return;
  }
  if (gMenuPerfMode == 2) {
    char t[24];
    snprintf(t, sizeof(t), "Profile %s", kPerfName[gMenuPerfIdx]);
    drawTextCenter(12, t, 1);
    // ★ CINCO campos desde el 2026-09-15: el ultimo es el ICONO DEL MAPA del
    //   perfil (el que sale en aprs.fi). La fila del icono ensena las dos cosas:
    //   el NOMBRE (que es lo que se elige, con un solo boton) y el PAR de codigos
    //   APRS ("/[") entre parentesis, que es lo que sale al aire. El nombre va
    //   PRIMERO para que quepa: "Icono: Persona  (/[)" son 20 caracteres a escala
    //   1 = 120 px de los 200 del panel.
    const char *fn[kPerfCampos] = {"SSID (1-15)", "Slow time(s)", "Fast time(s)",
                                   "Meters", "Icon"};
    int y = y0;
    for (int i = 0; i < kPerfCampos && y < 170; i++, y += rowH) {
      if (i == gMenuPerfField) { barraH(6, EPD_W-6, y-1,1); barraH(6, EPD_W-6, y+rowH-2,1); barraV(6,y-1,y+rowH-2,1); barraV(EPD_W-6,y-1,y+rowH-2,1); }
      char row[44];
      if (i == 0) {
        snprintf(row, sizeof(row), "%s: %lu  <", fn[i], (unsigned long)gCfg->profileSsid[gMenuPerfIdx]);
      } else if (i == 1) {
        snprintf(row, sizeof(row), "%s: %d  <", fn[i], gCfg->profileSlowSec[gMenuPerfIdx]);
      } else if (i == 2) {
        snprintf(row, sizeof(row), "%s: %d  <", fn[i], gCfg->profileFastSec[gMenuPerfIdx]);
      } else if (i == 3) {
        snprintf(row, sizeof(row), "%s: %d  <", fn[i], gCfg->profileDistM[gMenuPerfIdx]);
      } else {
        const int idx = iconoPerfilIndice(gMenuPerfIdx);
        char par[4];
        iconoPerfilTexto(gMenuPerfIdx, par, sizeof(par));
        // Si el icono guardado no es uno de los cuatro de la rueda (lo puso el
        // configurador), se ensena el codigo tal cual en vez de mentir con un
        // nombre que no le corresponde.
        snprintf(row, sizeof(row), "%s: %s  (%s)", fn[i],
                 (idx >= 0) ? kIconoNombre[idx] : par, par);
      }
      drawText(14, y, row, 1);
    }
    drawText(14, y, "< Back", 1);
    return;
  }

  int y = y0;

  // scroll: calcular la fila superior para que la seleccion sea visible (8 filas caen)
  const int visibles = 8;
  if (gMenuCat < 0) {
    drawTextCenter(12, "MENU", 1);
    int n = menuMainSecCount();
    int total = menuMainTotFilas(n);
    int top = gMenuIdx - (visibles / 2); if (top < 0) top = 0;
    if (top > total - visibles) top = (total - visibles < 0) ? 0 : total - visibles;
    y = y0;
    for (int r = top; r < total && y < 186; r++, y += rowH) {
      if (r == gMenuIdx) { barraH(6, EPD_W - 6, y - 1, 1); barraH(6, EPD_W - 6, y + rowH - 2, 1); barraV(6, y - 1, y + rowH - 2, 1); barraV(EPD_W - 6, y - 1, y + rowH - 2, 1); }
      if (menuMainFilaSalir(r, n)) drawText(14, y, "Exit", esc);
      else if (menuMainEsDormir(r)) drawText(14, y, "Sleep", esc);
      else {
        int sec = menuMainFilaSeccion(r, n);
        if (sec >= 0) drawText(14, y, kMenuSections[sec], esc);
      }
    }
  } else {
    int a = kMenuSectionFirst[gMenuCat];
    int b = kMenuSectionEnd[gMenuCat];
    drawTextCenter(12, kMenuSections[gMenuCat], 1);
    // ★ LEYENDA DEL ASTERISCO (2026-09-15, arreglo G3): solo en las categorias que tienen
    //   algun ajuste inerte, y en letra pequena para no quitar sitio a las filas. Dice que
    //   significa el " *" que llevan esas etiquetas. La explicacion larga sale al ENTRAR
    //   en el item (ver la pantalla de edicion).
    if (seccionTieneInerte(gMenuCat)) {
      drawTextCenter(24, " * saved but unused here", 1);
    }
    // filas: 0=Volver, 1=Salir, 2+i=item
    int itemCount = b - a;
    int total = itemCount + 2;
    int top = gMenuIdx - (visibles / 2); if (top < 0) top = 0;
    if (top > total - visibles) top = (total - visibles < 0) ? 0 : total - visibles;
    y = y0;
    for (int r = top; r < total && y < 186; r++, y += rowH) {
      if (r == gMenuIdx) { barraH(6, EPD_W - 6, y - 1, 1); barraH(6, EPD_W - 6, y + rowH - 2, 1); barraV(6, y - 1, y + rowH - 2, 1); barraV(EPD_W - 6, y - 1, y + rowH - 2, 1); }
      if (r == 0) { drawText(14, y, "< Back", esc); continue; }
      if (r == 1) { drawText(14, y, "Exit", esc); continue; }
      int iAbs = r - 2;
      if (iAbs < itemCount) {
        const MenuItem &it = kMenu[a + iAbs];
        if (!menuItemVisible(it)) { continue; }
        // ★ (2026-09-15) Solo la ETIQUETA, en grande. Se elimina el valor a la derecha:
        //   con textos largos se pisaba, y el valor en minuscula rompía la coherencia.
        //   Para ver/cambiar el valor se ENTRA en la opcion (submenu o pantalla de edicion).
        //
        // ★ TNC EN PALABRA (2026-09-15, peticion del operador): el protocolo del puerto
        //   USB se lee como "TNC: OFF" / "TNC: TNC2" / "TNC: KISS", no como un numero de
        //   la configuracion. Es el UNICO item con valor en la fila a proposito: la regla
        //   de esta lista es "solo la etiqueta" (los valores largos se pisaban), y esta
        //   cabe de sobra (9 caracteres de los 15 que entran a escala 2). El valor se lee
        //   de la configuracion en el momento de pintar, asi que si se cambia desde el
        //   configurador web, la fila lo dice sin reiniciar. Ver tncProtocoloPalabra().
        if (it.key && !strcmp(it.key, "tncProtocol")) {
          char conTnc[20];
          snprintf(conTnc, sizeof conTnc, "TNC: %s", tncProtocoloPalabra());
          drawText(14, y, conTnc, esc);
          continue;   // fila ya pintada: el "continue" del for no se salta nada mas
        }
        // ★ AVISO DE AJUSTE INERTE (2026-09-15, arreglo G3): los items que en esta placa se
        //   guardan pero no hacen nada llevan un " *" pegado a la etiqueta, y la leyenda de
        //   arriba dice que significa. Al ENTRAR en el item, la pantalla de edicion lo
        //   explica con todas las letras ("se guarda, pero en esta pantalla NO hace nada").
        //   La marca es de UNA letra a proposito: "Apagar pantalla(s)" ya roza el borde del
        //   panel y cualquier sufijo mas largo se saldria.
        if (avisoItemInerte(it.key)) {
          char conAviso[32];
          snprintf(conAviso, sizeof conAviso, "%s%s", it.label, etiquetaInerte(it.key));
          drawText(14, y, conAviso, esc);
        } else {
          drawText(14, y, it.label, esc);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
//  Dibujo del asistente SOTA. Mismo estilo visual que el submenu de opciones
//  de arriba (recuadro de seleccion, hasta 8 filas visibles con scroll) para
//  los pasos con lista; RECAP y CONFIRM son pantallas de una sola pantalla,
//  en el mismo estilo que la pantalla "CONFIRMAR" de menuPinta.
// ---------------------------------------------------------------------------
namespace {

static const char *const kSotaStepTitle[] = {
  "Association", "Region", "Digit 1/3", "Digit 2/3", "Digit 3/3",
  "Confirm summit",
  "Freq. hundreds", "Freq. tens", "Freq. units",
  "Freq. tenths", "Freq. hundredths", "Freq. thousandths",
  "Mode", "Call prefix", "Comment", "Confirm",
};

// Declaracion adelantada: se usa desde pintaSota() antes de su definicion mas
// abajo en este fichero.
int dibujaTextoEnvuelto(int x, int y, const char *s, int maxChars, int maxLineas, int yTope);

void pintaSota() {
  if (gSotaOpen && (uint32_t)(millis() - gSotaLastActMs) > kMenuInactivoMs) {
    sotaClose();
    return;
  }
  clearBuf(true);
  const int esc = 2;
  const int rowH = 18;
  const int y0 = 30;
  SotaStep step = sotaWizardStep();

  if (step == SOTA_STEP_RECAP) {
    char summary[24];
    sotaWizardSummary(summary, sizeof(summary));
    drawTextCenter(16, "SOTA SUMMIT", 1);
    const int tw = textWidth(summary, 3);
    int cx0 = (EPD_W - tw) / 2 - 8; if (cx0 < 4) cx0 = 4;
    int cx1 = (EPD_W + tw) / 2 + 8; if (cx1 > EPD_W - 4) cx1 = EPD_W - 4;
    barraH(cx0, cx1, 58, 2);
    barraH(cx0, cx1, 100, 2);
    barraV(cx0, 58, 100, 2);
    barraV(cx1, 58, 100, 2);
    drawTextCenter(72, summary, 3);
    drawTextCenter(140, "short: next", 1);
    drawTextCenter(154, "long: edit", 1);
    return;
  }

  if (step == SOTA_STEP_CONFIRM) {
    char msg[80];
    sotaWizardMessage(msg, sizeof(msg));
    const bool spot = sotaWizardIsSpot();
    drawTextCenter(12, spot ? "SEND SPOT?" : "SAVE PLAN?", 1);
    // El mensaje puede ser largo: mismo envoltorio de texto que ya usa la
    // escena "Mensajes" (dibujaTextoEnvuelto), probado para esto mismo.
    // En el PLAN es el mismo texto que saldra en el spot, sin el comentario.
    dibujaTextoEnvuelto(8, 36, msg, 24, 5, 150);
    drawTextCenter(160, spot ? "short: send" : "short: save", 1);
    drawTextCenter(174, "long: edit", 1);
    return;
  }

  if (step == SOTA_STEP_DONE) {
    // No deberia llegar a pintarse: sotaShort() envia/guarda y cierra el asistente
    // en el mismo gesto que hace la transicion a DONE. Por si acaso, para no
    // dejar el panel en blanco un refresco.
    drawTextCenter(90, "...", 2);
    return;
  }

  // ---- pasos con lista: mismo patron visual que el submenu de opciones ----
  if (step >= 0 && step < (int)(sizeof(kSotaStepTitle) / sizeof(kSotaStepTitle[0])))
    drawTextCenter(12, kSotaStepTitle[step], 1);
  int total = sotaWizardOptionCount();
  int cur = sotaWizardCursor();
  int vis = 8;
  int top = cur - (vis / 2); if (top < 0) top = 0;
  if (top > total - vis) top = (total - vis < 0) ? 0 : total - vis;
  int y = y0;
  char lbl[24];
  for (int r = top; r < total && y < 186; r++, y += rowH) {
    if (r == cur) { barraH(6, EPD_W-6, y-1,1); barraH(6, EPD_W-6, y+rowH-2,1); barraV(6,y-1,y+rowH-2,1); barraV(EPD_W-6,y-1,y+rowH-2,1); }
    sotaWizardOptionLabel(r, lbl, sizeof(lbl));
    drawText(14, y, lbl, esc);
  }
}

}  // namespace

void displayNextScene(bool porToque) {
  if (menuIsOpen()) return;   // con el menú abierto, el short no cambia de diapositiva
  gEscena = (uint8_t)((gEscena + 1) % kNumEscenas);
  gUltimoCambioEscenaMs = millis();
  gUltimoToqueMs = millis();     // sello de "aqui ha habido mano del operador"
  // ★ Solo el TACTIL agrupa repintados (ver kAgrupaToquesMs). El fisico llega con su
  //   ventana de 600 ms ya cumplida: aplazarle el pintado no agrupa nada, solo retrasa.
  if (porToque) gUltimoToqueAgrupaMs = millis();
  gCarruselPausadoHasta = millis() + kPausaTrasBotonMs;
  displayBacklightKick();
  gDirty = true;
  // ★ DIAGNOSTICO (2026-09-16): cada cambio de diapositiva A MANO se anuncia por el USB,
  //   igual que el del carrusel automatico (ver mas abajo). Es lo que permite COMPROBAR
  //   que cuatro toques seguidos son CUATRO cambios y no uno, sin depender de la vista:
  //   se cuentan las lineas y se miran los repintados que hay entre ellas.
  if (diagTrazaTaller()) {
    Serial.printf("PANTALLA: escena %d (%s)\r\n", (int)gEscena, porToque ? "toque" : "boton");
  }
}

void displayPopup(const char *text) {
  if (!text) return;
  // ★★ LOS AVISOS DE PROGRESO DE "FIJAR COORDS" LOS PINTA LA PANTALLA DE LA
  //   SESION, NO ESTE CAMINO (2026-09-15) ★★
  //   El bucle principal (main.cpp) manda "GPS 3/20" por CADA muestra mientras la
  //   sesion esta en fase 2. En la OLED eso es un aviso por segundo y se ve; en
  //   tinta cada aviso seria un refresco de 1,5 s (unos 30 s de panel pintando
  //   para ver cambiar un digito) y ademas taparia la pantalla de progreso, que ya
  //   lleva su contador y su barra. Se descartan SOLO esos avisos y SOLO mientras
  //   dura la sesion: cualquier otro texto (una baliza, un RX, un error) sigue
  //   pintandose igual. El aviso por USB lo sigue dando el rastreador.
  if (coordsPantallaActiva()) {
    const char *p = text;
    while (*p == ' ') p++;
    const bool esProgreso = (p[0] == 'G' && p[1] == 'P' && p[2] == 'S' && p[3] == ' ') &&
                            strchr(p, '/') != nullptr;
    if (esProgreso) return;
  }
  snprintf(gLinea1, sizeof(gLinea1), "%s", text);
  gLinea2[0] = '\0';
  gLineaMs = millis();
  gDirty = true;   // displayRefresh pinta una vez y actualiza la huella (B4)
}

// Guardar coordenadas desde la pantalla: ESTO ES LO QUE FALTABA (2026-09-15).
//
// Antes esta funcion devolvia false a secas ("no existe en esta version"), asi que
// la sesion de captura del rastreador (trackerSetCoordsTick) llegaba al final,
// pedia guardar y recibia un "no": en el T-Echo, "Fijar coords" no guardaba NADA
// (y el propio menu lo decia: "No soportado en tinta").
//
// COMO SE GUARDA: por el MISMO camino que el resto del menu (cliTypedSet +
// storeSave, ver menuSave), o sea pasando por la validacion de configFromJson. No
// se escribe en la configuracion a mano: una latitud fuera de rango tiene que dar
// error, no colarse en la flash.
//
// ★ PRIMERO SE COMPRUEBA TODO, DESPUES SE GUARDA UNA SOLA VEZ: son DOS claves
//   (latitude y longitude) y cada una es un merge completo, asi que si la latitud
//   entrara y la longitud no, el nodo se quedaria con una posicion a medias (la
//   latitud nueva con la longitud vieja: un punto que no existe). Por eso:
//     1) se comprueban los limites (-90..90 / -180..180, los MISMOS que aplica
//        configFromJson, aqui solo se adelanta el rechazo),
//     2) se validan las dos claves con cliTypedSet (que NO toca la flash: solo
//        aplica el valor en RAM y dice si es valido),
//     3) y solo entonces se llama a storeSave(), UNA vez, con las dos ya dentro.
//   Si algo falla en (1) o (2) se dejan las dos como estaban (no se guarda nada).
bool displaySaveCoords(double lat, double lon) {
  if (!isfinite(lat) || !isfinite(lon) || lat < -90.0 || lat > 90.0 ||
      lon < -180.0 || lon > 180.0) {
    if (diagTrazaTaller()) {
      Serial.printf("TINTA fijar coords: posicion fuera de rango (%.5f %.5f)\r\n", lat, lon);
    }
    return false;
  }
  // OJO CON EL TIPO, QUE ES LA TRAMPA DE ESTE CAMINO: si el texto lleva punto
  // decimal, cliTypedSet lo manda como FLOAT y configFromJson lo acepta (acepta
  // float e int en latitude/longitude). Seis decimales es mas de lo que el GPS
  // puede decir (~0,1 m) y lo que se guarda acaba en un float, asi que la
  // precision de sobra no estorba y evita redondear a metros.
  char la[24], lo[24];
  snprintf(la, sizeof(la), "%.6f", lat);
  snprintf(lo, sizeof(lo), "%.6f", lon);

  const float latAntes = gCfg->latitude;
  const float lonAntes = gCfg->longitude;
  String err;
  const bool okLat = cliTypedSet(*gCfg, "latitude", la, err);
  const bool okLon = okLat ? cliTypedSet(*gCfg, "longitude", lo, err) : false;
  if (!okLat || !okLon) {
    // Se deshace lo que hubiera entrado en RAM: la configuracion tiene que quedar
    // EXACTAMENTE como estaba (en la flash no se ha escrito nada todavia).
    gCfg->latitude = latAntes;
    gCfg->longitude = lonAntes;
    if (diagTrazaTaller()) {
      Serial.printf("TINTA fijar coords: ERROR al guardar (lat=%d lon=%d) %s\r\n",
                    okLat ? 1 : 0, okLon ? 1 : 0, err.c_str());
    }
    return false;
  }
  storeSave(*gCfg);   // las dos claves validadas: una sola escritura en la flash
  gCoordsLat = lat;
  gCoordsLon = lon;
  if (diagTrazaTaller()) {
    Serial.printf("TINTA fijar coords: guardado %.5f %.5f\r\n", lat, lon);
  }
  return true;
}

void displayPopupWait(const char *text, uint32_t totalMs) {
  // ★ POPUP BONITO (2026-09-15): recuadro negro con texto invertido, centrado, que dura
  //   `totalMs` y luego vuelve al carrusel. Se usa p.ej. para avisar del modo Dormir con
  //   el USB conectado ("solo sin cable USB").
  if (text) {
    // pintar la escena de fondo limpia y el recuadro encima
    if (gReady) {
      dibujaEscena();                       // fondo (carrusel/aviso actual)
      const int tw = textWidth(text, 2);
      int bx = (EPD_W - tw - 40) / 2; if (bx < 6) bx = 6;
      const int bw = tw + 40;
      const int bh = 38;
      const int by = (EPD_H - bh) / 2;
      // Recuadro negro con las esquinas cortadas (pastilla), hecho a mano porque
      // pastillaRellena vive en el namespace de dibujo y no es accesible aqui.
      relleno(bx, by, bw, bh);                     // cuerpo
      px(bx, by, false); px(bx + 1, by, false);   // esquina superior izquierda
      px(bx, by + 1, false);
      px(bx + bw - 1, by, false); px(bx + bw - 2, by, false);   // superior derecha
      px(bx + bw - 1, by + 1, false);
      px(bx, by + bh - 1, false); px(bx + 1, by + bh - 1, false);   // inferior izquierda
      px(bx, by + bh - 2, false);
      px(bx + bw - 1, by + bh - 1, false); px(bx + bw - 2, by + bh - 1, false);   // inferior derecha
      px(bx + bw - 1, by + bh - 2, false);
      drawTextInv((EPD_W - tw) / 2, by + 12, text, 2);   // texto invertido, centrado
      // ★ OJO (2026-09-15): aqui estaba `gDirty = false;` ANTES de epdFlush(), y
      //   epdFlush() empieza con `if (!gReady || !gDirty) return;` -> salia sin pintar
      //   NADA. Por eso el popup "no salia": se perdian los 3 s sin dibujar. La bandera
      //   la apaga epdFlush() el solo; aqui hay que DEJARLA EN ALTA para que pinte.
      gDirty = true;
      epdFlush();
    }
  }
  delay(totalMs);
  gDirty = true;   // volver a pintar el carrusel tras el aviso
}

void displayNoteRx(const char *from, float rssi, float snr, const char *kind) {
  rxLogPush(from, rssi, snr);
  snprintf(gLinea1, sizeof(gLinea1), "RX %s", from ? from : "?");
  snprintf(gLinea2, sizeof(gLinea2), "%.0f dBm  %.1f dB", (double)rssi, (double)snr);
  (void)kind;
  gLineaMs = millis();
  gDirty = true;   // displayRefresh pinta una vez (B4)
}
void displayNoteDigi(const char *from, float rssi, float snr) {
  rxLogPush(from, rssi, snr);   // un repetido tambien entra en la lista de RX
  snprintf(gLinea1, sizeof(gLinea1), "DIGI %s", from ? from : "?");
  snprintf(gLinea2, sizeof(gLinea2), "%.0f dBm", (double)rssi);
  (void)snr;
  gLineaMs = millis();
  gDirty = true;   // displayRefresh pinta una vez (B4)
}
void displayNoteTx(const char *what) {
  txLogPush(what);
  snprintf(gLinea1, sizeof(gLinea1), "TX %s", what ? what : "");
  gLinea2[0] = '\0';
  gLineaMs = millis();
  gDirty = true;   // displayRefresh pinta una vez (B4) y el TX-reciente entra en la huella
}

// Mensaje recibido: SOLO guarda en el anillo y pide repintado. El aviso
// emergente ("MSG remitente\ntexto") lo sigue poniendo aprs.cpp con su propio
// displayPopup() justo despues de llamar aqui -- duplicarlo aqui pisaria ese
// texto (mejor formateado, con el cuerpo) con uno mas corto.
void displayNoteMsg(const char *from, const char *text) {
  msgLogPush(from, text);
  // Respuesta de la pasarela al ultimo spot: se mira el texto YA LIMPIO (sin el
  // "{nnn" del numero de mensaje) que acaba de guardar msgLogPush().
  if (gSpot != SPOT_NINGUNO && from && strcasecmp(from, kSotaPasarela) == 0) {
    const MsgLog &e = gMsgLog[(gMsgLogHead - 1 + kMsgLogMax) % kMsgLogMax];
    const SpotEstado r = spotDeRespuesta(e.text);
    if (r != SPOT_NINGUNO) gSpot = r;
  }
  gDirty = true;   // por si la escena "Mensajes" esta activa ahora mismo
}

void displayNoteMsgEnd(const char *to, MsgEnd how) {
  if (gSpot != SPOT_ENVIANDO || !to || strcasecmp(to, kSotaPasarela) != 0) return;
  if (how == MSG_END_ACK) return;   // acuse: sigue "Sending" hasta que conteste
  gSpot = (how == MSG_END_REJ) ? SPOT_ERROR : SPOT_NO_ENVIADO;
  gDirty = true;
}

// ---------------------------------------------------------------------------
//  El pintado de las pantallas
// ---------------------------------------------------------------------------
namespace {

// ★ (subida a escala 2) `modoNombre()` se queda aqui aunque la escena de Estado ya no lo
//   use: el modo se quito de la linea de contadores porque a escala 2 no cabe junto a
//   ellos (ver `pintaEstado()`). El atributo es solo para que el compilador no avise de
//   "definida y no usada" (-Wunused-function) mientras no haya ninguna pantalla que la use.
[[maybe_unused]] const char *modoNombre(uint8_t m) {
  switch (m) {
    case 0: return "Digipeater";
    case 1: return "Tracker";
    case 2: return "Digi+Tracker";   // antes "Ambos" (etiqueta mas clara, 2026-09-15)
    default: return "Off";
  }
}

// ---------------------------------------------------------------------------
//  Utilidades de dibujo y datos que usa la cabecera y el pie.
// ---------------------------------------------------------------------------

// Rectangulo SOLO CONTORNO (marco de la pastilla RX, de los puntos y de la bateria).
void rectVacio(int x0, int y0, int x1, int y1) {
  barraH(x0, x1, y0, 1);
  barraH(x0, x1, y1, 1);
  barraV(x0, y0, y1, 1);
  barraV(x1, y0, y1, 1);
}

// Porcentaje de bateria para la barrita (0..100; 0 si no hay lectura creible).
// Misma regla que la OLED (display.cpp, drawBatteryFooter): 3,0..4,2 V -> 0..100 %.
int bateriaPct() {
  const float bv = sensorsBatteryVolt(gSens);
  if (bv <= 0.0f) return 0;
  int p = (int)((bv - 3.0f) / 1.2f * 100.0f);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

// ---------------------------------------------------------------------------
//  ★★ CABECERA Y PIE — EL ESTILO DE LA OLED, adaptado a este panel 200x200 vertical ★★
//
//  Se replica la interfaz de la OLED (display.cpp) pero con las zonas pensadas para
//  nuestra resolucion y con un UNICO escalado de texto (coherencia):
//
//    ARRIBA  (y 4..25)
//      ┌─ pastilla con el titulo de la pantalla: indicativo + modo
//      └─ a la derecha: pastilla RX/TX (se R-ellen-0 en negro al transmitir; MUTE si
//         el nodo esta silenciado; "--" si la radio no esta lista)
//    DEBAJO  separador discontinuo (guiño al aleman)
//    ABAJO   (y ~184..196)
//      ┌─ rectangulitos del carrusel: el de la pantalla actual relleno
//      └─ a la derecha: bateria con su % y el cuerpo que se llena/vacia
//
//  La leccion de la OLED: "lo invertido comunica estado" (RX contorneado, TX relleno).
// ---------------------------------------------------------------------------

// ── pastilla (ovalo aproximado) de la cabecera: rectangulo relleno con las esquinas
// redondeadas a mano (en esta resolucion basta con cortar las esquinas).
void pastillaRellena(int x0, int y0, int x1, int y1) {
  relleno(x0, y0, x1 - x0, y1 - y0);          // cuerpo
  px(x0, y0, false); px(x0 + 1, y0, false);   // esquina superior izquierda (corta)
  px(x0, y0 + 1, false);
  px(x1, y0, false); px(x1 - 1, y0, false);   // superior derecha
  px(x1, y0 + 1, false);
  px(x0, y1, false); px(x0 + 1, y1, false);   // inferior izquierda
  px(x0, y1 - 1, false);
  px(x1, y1, false); px(x1 - 1, y1, false);   // inferior derecha
  px(x1, y1 - 1, false);
}

// ── OVALO de la cabecera: el titulo de la pantalla (tu indicativo + modo) en una pastilla
// blanca con texto negro, como la `headerBar` de la OLED. Se adapta a la longitud.
void pintaOvalo(const char *titulo) {
  const int pad = 8;                 // aire interior
  const int y0 = 4;                  // alto de la zona del titulo (se mantiene para el
                                     // alineado del texto, ya sin el rectangulo)
  // ★ (2026-09-15) Se ELIMINA el rectangulo del titulo: ahora las diapositivas muestran el
  //   titulo como texto simple, sin la caja. Se conserva la columna x (8+pad) y la altura
  //   para que el dibujo quede igual de legible; la pastilla RX/TX de la derecha no cambia.
  drawText(8 + pad, y0 + 3, titulo, 2);
}

// ── pastilla RX/TX a la derecha de la cabecera, como `statusPill` de la OLED:
// RX = solo contorno (texto negro, no transmite); TX = rellena de negro con texto blanco
// cuando estoy transmitiendo (se mantiene 3 s tras el envio); MUTE si el nodo esta
// silenciado; "--" si la radio no lista.
void pintaPillRX() {
  const uint32_t last = radioLastTxMs();
  bool tx = (last != 0 && (uint32_t)(millis() - last) < 3000);
  const char *txt = "RX";
  if (gCfg && gCfg->txDisabled) txt = "MUTE";
  else if (tx) txt = "TX";
  else if (!radioReady()) txt = "--";

  const int pad = 8;
  const int y0 = 4, y1 = 22;
  const int tw = textWidth(txt, 2);
  const int x1 = EPD_W - 8;
  const int x0 = x1 - tw - pad * 2;
  if (tx) {
    pastillaRellena(x0, y0, x1, y1);            // rellena de negro
    const int dx = x0 + pad + 2;                 // texto BLANCO (inverso) centrado aprox
    drawTextInv(dx, y0 + 3, txt, 2);
  } else {
    rectVacio(x0, y0, x1, y1);                   // solo contorno
    drawText(x0 + pad, y0 + 3, txt, 2);
  }
}

// ── CABECERA completa: ovalo con el titulo + pastilla RX/TX + separador discontinuo.
void pintaCabecera(const char *titulo) {
  pintaOvalo(titulo);
  pintaPillRX();
  // separador discontinuo a y=25 (5 on / 3 off), guiño al aleman
  for (int x0 = 4; x0 < EPD_W - 4; x0 += 8) {
    int x1 = x0 + 4;
    if (x1 > EPD_W - 4) x1 = EPD_W - 4;
    hLine(x0, x1, 25, 1);
  }
}

// ── ★★ EL AVISO, EN UN SOLO PASO (2026-09-15) ★★ ────────────────────────────
//
// SUSTITUYE a `pintaAvisoTX()`, que dibujaba una banda "TX" mientras
// `radioLastTxMs() < 3000`. Aquel era un SEGUNDO mecanismo de aviso con su PROPIO reloj
// (3000 ms) distinto del de las lineas de aviso (`kLineaMs`, 8000 ms antes / 1500 ms
// ahora), y ademas metia su propio cambio de estado en la huella del contenido: o sea, un
// TX podia provocar TRES refrescos de tinta (aviso TX de la linea + banda TX + caducidad de
// la banda). Ahora hay UN solo camino: `displayNoteTx()` deja su texto en `gLinea1` y este
// recuadro es el unico aviso. La pastilla RX/TX de la cabecera (pintaPillRX) se mantiene:
// esa es informacion de estado, no un aviso, y no cuesta un refresco extra porque su cambio
// ya viaja en la huella.
//
// COMO SE PINTA: recuadro negro con texto invertido, centrado en la mitad baja de la
// pantalla, ENCIMA de la escena (que se sigue viendo alrededor) y SIN tocar el pie
// (y=186 en adelante), que es donde vive la bateria.
void pintaAviso() {
  if (!gLinea1[0]) return;
  const bool dosLineas = (gLinea2[0] != '\0');
  const int bh = dosLineas ? 58 : 38;
  const int by = dosLineas ? 104 : 114;
  // El ancho lo manda la linea mas larga, con margen a los lados y topes para que quepa.
  int twMax = textWidth(gLinea1, 2);
  if (dosLineas) {
    const int tw2 = textWidth(gLinea2, 1);
    if (tw2 > twMax) twMax = tw2;
  }
  int bw = twMax + 24;
  if (bw > EPD_W - 12) bw = EPD_W - 12;
  if (bw < 40) bw = 40;
  const int bx = (EPD_W - bw) / 2;
  // Recuadro negro (pastillaRellena corta las esquinas; el texto va invertido dentro).
  pastillaRellena(bx, by, bx + bw - 1, by + bh - 1);
  const int t1 = textWidth(gLinea1, 2);
  drawTextInv((EPD_W - t1) / 2, by + 8, gLinea1, 2);
  if (dosLineas) {
    const int t2 = textWidth(gLinea2, 1);
    drawTextInv((EPD_W - t2) / 2, by + 36, gLinea2, 1);
  }
}

// ── PIE: rectangulitos del carrusel (el actual relleno) a la izquierda + bateria a la
// derecha, como `footerDots` + `drawBatteryFooter` de la OLED.
void pintaPie() {
  const int yf = 186;                 // fila del pie
  // ---- rectangulitos del carrusel (kNumEscenas escenas: pequenios para que no choquen con la bateria)
  {
    const int dotW = 8, gap = 4, y = yf;
    int x0 = 12;
    for (int i = 0; i < kNumEscenas; i++) {
      int x = x0 + i * (dotW + gap);
      if (i == (int)gEscena) relleno(x, y, dotW, 6);
      else                   rectVacio(x, y, x + dotW, y + 6);
    }
  }
  // ---- bateria, REDISENADA (2026-09-15): el % va ENCIMA del icono y el cuerpo se v aci
  // segun el nivel (se ve con poca y con mucha carga). El polo positivo es un borne
  // compacto a la derecha. Nada se pinta encima de otra cosa.
  {
    const int p = bateriaPct();
    char bb[8];
    if (bateriaMv() > 0) snprintf(bb, sizeof(bb), "%d%%", p);
    else                 snprintf(bb, sizeof(bb), "--");

    // cuerpo de la bateria, anclado a la derecha; el borne positivo sobresale a la derecha
    const int by = 186, bh = 14;
    const int bw = 44;
    const int bx = EPD_W - 16 - 6 - bw;          // hueco para el borne (6 px)
    rectVacio(bx, by, bx + bw - 1, by + bh - 1);
    relleno(bx + bw, by + bh / 2 - 3, 5, 6);     // borne positivo
    if (p > 0) relleno(bx + 2, by + 2, (bw - 4) * p / 100, bh - 4);   // relleno segun el %

    // texto del % exactamente ENCIMA del cuerpo (mismo tamano, escala 2), centrado sobre el
    // cuerpo para que nunca choque con el relleno ni con los puntos del carrusel.
    const int twTxt = textWidth(bb, 2);
    drawText(bx + (bw - twTxt) / 2, by - 14, bb, 2);
  }
}

// ---------------------------------------------------------------------------
//  ★★ ICONO DEL PERFIL ACTIVO — cabecera de la escena "Estado" (2026-09-15) ★★
//
//  QUE PROBLEMA RESUELVE: el operador cambia de perfil (digi / peaton / bici / coche) y eso
//  cambia el SSID con el que sale al aire, pero en la pantalla no habia NADA que lo dijera:
//  habia que entrar al menu para saber con que perfil estaba trabajando el nodo.
//
//  DONDE VA: en la cabecera queda un hueco libre entre el indicativo y la pastilla RX/TX.
//  El indicativo lo pinta `pintaOvalo()` con `drawText(8 + pad, y0 + 3, titulo, 2)` (pad=8,
//  y0=4), o sea que EMPIEZA EN x=16 y avanza 6*2 = 12 px por caracter. La cabecera ocupa de
//  y=4 a y=22 (18 px de alto), asi que un icono de 16x16 centrado va de y=5 a y=20.
//
//  ★ EL COLOR, QUE NO ES EVIDENTE (leido en el codigo, no supuesto): el ultimo argumento de
//    `px()` es un booleano que significa "negro", NO "pinta":
//        px(x, y, true)  -> gBuf &= ~bit -> el bit queda a 0 -> NEGRO (tinta)
//        px(x, y, false) -> gBuf |=  bit -> el bit queda a 1 -> BLANCO (borra)
//    Y se usan LOS DOS: `drawChar()` y `relleno()` pintan tinta con `true` (por eso el texto
//    sale negro); `drawCharInv()` pinta blanco con `false` (el texto de la pastilla TX) y
//    `pastillaRellena()` usa `false` para RECORTAR las esquinas. Aqui la figura tiene que
//    quedar en TINTA, igual que el texto, asi que TODO va con `true`.
//
//  ★ NADA SE DIBUJA ENCIMA DE NADA: el icono solo se pinta si CABE en el hueco (la cuenta y
//    la comprobacion estan en `pintaEstado()`); con un indicativo largo no se dibuja nada y
//    la cabecera se queda exactamente como estaba.
// ---------------------------------------------------------------------------

// Pinta una figura de 16x16 descrita como arte ASCII: '#' = tinta, cualquier otra cosa =
// blanco (no se pinta). Se para en el '\0' de cada fila, asi que una fila corta no se sale.
void pintaFigura16(int x0, int y0, const char *const *filas) {
  for (int fy = 0; fy < 16; fy++) {
    const char *fila = filas[fy];
    if (!fila) continue;
    for (int fx = 0; fx < 16 && fila[fx]; fx++)
      if (fila[fx] == '#') px(x0 + fx, y0 + fy, true);   // true = TINTA (ver px())
  }
}

// ── PERFIL 0 = digi / fijo: ESTRELLA de 5 puntas con una "D" dentro (el simbolo clasico del
//    digipeater). La "D" va RECORTADA EN BLANCO sobre la estrella rellena: a 16x16 una "D" de
//    tinta sobre estrella de contorno deja las dos figuras en hilachas y no se lee ninguna.
//    OJO: la "D" esta dibujada a mano (5x7, trazos de 1 px) y NO es la letra de `EpdFont5x7`.
//    Se probaron las dos recortadas sobre esta misma estrella, y la de la fuente --que tiene
//    el lado derecho redondeado-- deja el trazo tan fino que a 1:1 se lee peor que esta.
//                            0123456789012345
const char *const kIconoDigi[16] = {
  "................",   // 0
  "................",   // 1
  ".......##.......",   // 2   punta de arriba
  ".......##.......",   // 3
  "......####......",   // 4
  "....#....###....",   // 5   hombro izquierdo / cuello
  ".####.###.#####.",   // 6   brazo izq. | hueco de la D | brazo der.
  "..###.###.####..",   // 7
  "...##.###.###...",   // 8
  "....#.###.##....",   // 9
  "....#.###.##....",   // 10
  "....#....###....",   // 11
  "....########....",   // 12  base de la D + union de las patas
  "....##....##....",   // 13  las dos patas de abajo
  "................",   // 14
  "................"    // 15
};

// ── PERFIL 1 = PEATON: cabeza, cuerpo con los brazos abiertos, y las dos piernas.
//                            0123456789012345
const char *const kIconoPeaton[16] = {
  "................",   // 0
  "......####......",   // 1   cabeza (4x4)
  "......####......",   // 2
  "......####......",   // 3
  "......####......",   // 4
  ".......##.......",   // 5   cuello
  "....########....",   // 6   hombros
  "..############..",   // 7   brazos abiertos
  "......####......",   // 8   tronco
  "......####......",   // 9
  "......####......",   // 10
  "......####......",   // 11
  ".....##..##.....",   // 12  las dos piernas, que se abren hacia abajo
  "....##....##....",   // 13
  "...##......##...",   // 14
  "..###......###.."    // 15  pies
};

// ── PERFIL 2 = BICI: las dos ruedas (aros de 7x7) y el cuadro en rombo, con el manillar
//    arriba a la derecha y el sillin arriba a la izquierda.
//                            0123456789012345
const char *const kIconoBici[16] = {
  "................",   // 0
  "................",   // 1
  "................",   // 2
  ".........###....",   // 3   manillar
  "...####.........",   // 4   sillin
  "....#######.....",   // 5   tubo superior
  ".....#....#.....",   // 6   tubo del sillin | horquilla
  ".....#...#.#....",   // 7
  "......#..#.#....",   // 8   tubo diagonal
  "..###..#.#.###..",   // 9   parte de arriba de las dos ruedas
  ".#...#.##.#.#.#.",   // 10  aros
  "#..#######..#..#",   // 11  vaina al buje trasero + caja de pedalier
  "#.....#..#.....#",   // 12  aros
  "#.....#..#.....#",   // 13
  ".#...#....#...#.",   // 14
  "..###......###.."    // 15  parte de abajo de las dos ruedas
};

// ── PERFIL 3 = COCHE: silueta de perfil (techo, parabrisas recortado, carroceria) con las
//    dos ruedas debajo.
//                            0123456789012345
const char *const kIconoCoche[16] = {
  "................",   // 0
  "................",   // 1
  "................",   // 2
  ".....######.....",   // 3   techo
  "....##....##....",   // 4   montantes con la ventanilla en blanco
  "...##......##...",   // 5
  "..############..",   // 6   capo / maletero
  ".##############.",   // 7   carroceria
  ".##############.",   // 8
  ".##############.",   // 9
  ".##############.",   // 10
  "..###......###..",   // 11  las dos ruedas
  "..###......###..",   // 12
  "..###......###..",   // 13
  "................",   // 14
  "................"    // 15
};

// Las cuatro figuras, en el MISMO orden que `smartBeaconPreset` (ver config.h):
//   0 = off (fijo/digi), 1 = human (peaton), 2 = bike (bici), 3 = car (coche)
const char *const *const kIconosPorPerfil[4] = {
  kIconoDigi, kIconoPeaton, kIconoBici, kIconoCoche
};

// Dibuja en (x, y) el icono del perfil activo (`gCfg->smartBeaconPreset`). Un valor fuera de
// 0..3 (flash de otra version) se pinta como digi, que es el perfil 0.
void iconoPerfil(int x, int y, int perfil) {
  if (perfil < 0 || perfil > 3) perfil = 0;
  pintaFigura16(x, y, kIconosPorPerfil[perfil]);
}

// Fila "Etiqueta ........ valor" a escala 2: la etiqueta pegada a la izquierda y el valor
// alineado a la DERECHA, para que los numeros de filas seguidas se lean en columna.
void filaDato(int y, const char *etiqueta, const char *valor) {
  drawText(12, y, etiqueta, 2);
  drawText(EPD_W - 12 - textWidth(valor, 2), y, valor, 2);
}

// Escena 0: inicio (lo que se ve casi siempre). La cabecera lleva tu indicativo
// (como la barra de titulo de la OLED).
void pintaEstado() {
  const char *call = callSinSSID();   // solo el indicativo, sin el SSID
  pintaCabecera(call);

  // ★ ICONO DEL PERFIL ACTIVO (2026-09-15): en la cabecera queda un hueco libre entre el
  //   indicativo y la pastilla RX/TX. Ahi se dibuja (DESPUES de la cabecera, que es quien
  //   pinta el indicativo) un icono de 16x16 con el perfil con el que trabaja el nodo.
  //
  //   LA CUENTA, CON LAS MEDIDAS YA VERIFICADAS:
  //     - el indicativo empieza en x = 8 + pad = 8 + 8 = 16 y avanza 12 px por caracter
  //       (escala 2), asi que ocupa hasta 16 + textWidth(call, 2);
  //     - el icono empieza 8 px despues de la ultima letra: xIcono = 16 + ancho + 8;
  //     - el icono mide 16 px de ancho, o sea que acaba en xIcono + 16;
  //     - la pastilla RX/TX NO se toca ni se desplaza: si el icono no cabe en el hueco,
  //       simplemente NO se dibuja.
  //
  //   ★ EL LIMITE DE LA DERECHA NO ES SIEMPRE 154. Leyendo `pintaPillRX()`: la pastilla se
  //     dimensiona con su texto, `x0 = (EPD_W - 8) - textWidth(txt, 2) - pad*2` = 192 - tw -
  //     16. Con "RX", "TX" y "--" sale x0 = 154 (el valor medido), pero con "MUTE" (nodo
  //     silenciado) el texto es mas largo y sale x0 = 192 - 46 - 16 = 130. Para que el icono
  //     no la pise NUNCA se comprueba contra el PEOR caso (130), que es el unico que garantiza
  //     el hueco en los cuatro estados de la pastilla.
  {
    const int xIcono = 16 + textWidth(call, 2) + 8;   // 16 = inicio del indicativo, 8 = margen
    const int kPastillaXIzq = 130;                    // peor caso de pintaPillRX() ("MUTE")
    if (xIcono + 16 <= kPastillaXIzq) {
      const int perfil = gCfg ? (int)gCfg->smartBeaconPreset : 0;
      iconoPerfil(xIcono, 5, perfil);                 // y = 5..20, centrado en la cabecera
    }
  }

  // ★ 2026-09-27 (firmware dedicado a SOTA): la escena de inicio se queda con lo que
  //   importa en la montana y nada mas -- posicion, velocidad, altitud, satelites y
  //   mensajes. Fuera los contadores R/T/D, la meteorologia, el RSSI/SNR y la linea de
  //   bateria (la bateria sigue en el pie, como en todas las escenas).
  //
  //   LA CUENTA DEL HUECO: contenido de y = 34 a y = 170 (el pie empieza en 172).
  //     2 lineas de coordenadas (34, 54) + raya (76) + 4 filas de dato (84..144, salto 20).
  //     La ultima fila acaba en 144 + 14 = 158: 14 px de aire hasta el pie.
  //   ANCHO: "N 43.12345" / "W 123.12345" = 11 caracteres a escala 2 = 132 px. Las filas
  //   de dato son "Speed" + valor alineado a la derecha ("9999 m" = 6): nunca se tocan.
  const GpsData &g = gpsGet();
  char b[24];
  int y = 34;
  if (g.fix) {
    snprintf(b, sizeof(b), "%c %.5f", g.lat >= 0 ? 'N' : 'S', fabs(g.lat));
    drawTextCenter(y, b, 2); y += 20;
    snprintf(b, sizeof(b), "%c %.5f", g.lon >= 0 ? 'E' : 'W', fabs(g.lon));
    drawTextCenter(y, b, 2); y += 20;
  } else {
    drawTextCenter(y + 10, "GPS: searching", 2); y += 40;
  }
  y += 2;
  hLine(6, EPD_W - 6, y, 1); y += 8;

  if (g.fix) snprintf(b, sizeof(b), "%.0f km/h", (double)g.speedKmh);
  else       snprintf(b, sizeof(b), "--");
  filaDato(y, "Speed", b); y += 20;

  if (g.fix && g.altValid) snprintf(b, sizeof(b), "%.0f m", (double)g.altM);
  else                     snprintf(b, sizeof(b), "--");
  filaDato(y, "Alt", b); y += 20;

  snprintf(b, sizeof(b), "%u", (unsigned)g.sats);
  filaDato(y, "Sats", b); y += 20;

  snprintf(b, sizeof(b), "%lu", (unsigned long)gMsgTotal);
  filaDato(y, "Msgs", b);
}

// Escena 1: SOTA -- el plan de activacion (menu SOTA -> "Plan activation"), lo que
// ha sido del ultimo spot y la hora UTC (la que va al libro de guardia).
//
// ★ 2026-09-27, disposicion pedida por el operador:
//       F4LEK/P
//     on F/PE-110
//      100.000 CW
//     ------------
//     APRS status:
//       Spotted
//     ------------
//      12:07 UTC
//   La hora va a MINUTOS: con segundos el panel se repintaria cada segundo.
//
//   LA CUENTA DEL HUECO (200x200, contenido de y = 32 a 170; el pie empieza en 172),
//   todo a escala 2 (14 px de alto) salvo la etiqueta "APRS status:" (escala 1):
//     indicativo 32..45, cumbre 52..65, frecuencia/modo 72..85, raya 93,
//     etiqueta 100..106, estado 112..125, raya 133, hora 142..155.
//   ANCHO: "on VK3/VE-123" = 13 y "145.500 OTHER" = 13 caracteres = 156 px. El
//     indicativo con prefijo puede pasar de 16 caracteres (192 px): baja a escala 1.
void pintaCumbre() {
  pintaCabecera("SOTA");
  SotaPlan c;
  const bool hay = gCfg && sotaPlanParse(gCfg->sotaPlan, gCfg->callsign, &c);
  char b[32];
  if (!hay) {
    drawTextCenter(52, "No plan", 2);
    drawTextCenter(76, "Menu > SOTA >", 1);
    drawTextCenter(88, "Plan activation", 1);
  } else {
    if (textWidth(c.call, 2) <= EPD_W - 12) drawTextCenter(32, c.call, 2);
    else                                    drawTextCenter(36, c.call, 1);
    snprintf(b, sizeof(b), "on %s", c.ref);
    drawTextCenter(52, b, 2);
    snprintf(b, sizeof(b), "%s %s", c.freq, c.mode);
    drawTextCenter(72, b, 2);
  }
  hLine(6, EPD_W - 6, 93, 1);

  drawTextCenter(100, "APRS status:", 1);
  const char *t;
  switch (gSpot) {
    case SPOT_ENVIANDO:   t = "Sending"; break;
    case SPOT_NO_ENVIADO: t = "Not sent"; break;
    case SPOT_SPOTTED:    t = "Spotted"; break;
    case SPOT_DUPE:       t = "Dupe"; break;
    case SPOT_ERROR:      t = "Error"; break;
    default:              t = "--"; break;   // todavia no se ha enviado ningun spot
  }
  drawTextCenter(112, t, 2);
  hLine(6, EPD_W - 6, 133, 1);

  const GpsData &g = gpsGet();
  if (g.timeValid) snprintf(b, sizeof(b), "%02u:%02u UTC", (unsigned)g.utcH, (unsigned)g.utcM);
  else             snprintf(b, sizeof(b), "--:-- UTC");
  drawTextCenter(142, b, 2);
}

// Escena 3: estaciones oidas (la OLED tenia "ESTACIONES": indicativo + distancia y rumbo
// cuando hay fijacion GPS, o edad si no).
//
// ★★ EL MISMO FALLO DE ANCHO ESTABA AQUI (revisado y arreglado 2026-09-15) ★★
//   QUE PASABA: `"%-9s %s"` NO recortaba el indicativo (9 es un ancho MINIMO), asi que un
//   `N0CALL-3` salia entero; el recorte a 6 caracteres era solo de "Ultimos RX". Pero el
//   ancho estaba calculado a ojo y el peor caso REAL no cabia: `N0CALL-15` (9) + 1 espacio +
//   una distancia de 7 caracteres ("999.9km") = 17 caracteres x 12 px (esta escena va a
//   ESCALA 2) = 204 px sobre un panel de 200. Con un indicativo de 9 el dato se salia por la
//   derecha. No se habia visto porque los indicativos de las pruebas eran de 7.
//   AHORA, con la cuenta hecha y el peor caso garantizado:
//     - indicativo: ancho de campo 10 + recorte EXPLICITO a 9. Nueve es el peor caso real de
//       este sistema (6 caracteres + guion + 2 cifras = "N0CALL-15"); mas largo no lo admite
//       ni AX.25, asi que no hay nada legitimo que recortar aqui;
//     - dato (distancia o edad): 5 caracteres. El formato se ha elegido PARA QUE NUNCA pase
//       de ahi: metros enteros por debajo de 1 km ("742m"), kilometros enteros de 1 a 4
//       cifras ("9km", "999km") y, por encima de 9999 km (imposible en LoRa, es una red de
//       decenas de km), ">9999". Se pierde el decimal del kilometro, que a esas distancias
//       no aporta nada;
//     - 10 + 1 + 5 = 16 caracteres = 192 px de 200, CON EL PEOR CASO. Y la columna del dato
//       empieza siempre en x = 12 + 11*12 = 144, asi que las filas se leen en columna.
void pintaEstaciones() {
  pintaCabecera("Stations");
  HeardStation hs[8];
  uint8_t n = aprsHeardStations(hs, 8);
  const GpsData &g = gpsGet();
  int y = 34;
  char b[48];
  if (n == 0) {
    drawTextCenter(90, "None yet", 2);
    return;
  }
  // Anchos en caracteres para la letra ESCALA 2 de esta escena (12 px por caracter).
  constexpr int kCallCol = 10;   // 9 de indicativo + 1 de aire
  constexpr int kDatoCol = 5;    // "999km" / "9999m" / "59min" / "23h"
  for (uint8_t i = 0; i < n && i < 5; i++) {
    if (g.fix && hs[i].hasPos) {
      float d = gpsDistanceM(g.lat, g.lon, hs[i].lat, hs[i].lon);
      char ds[10];
      if (d < 1000.0f) snprintf(ds, sizeof(ds), "%.0fm", (double)d);
      else if (d < 9999.0f * 1000.0f) snprintf(ds, sizeof(ds), "%.0fkm", (double)(d / 1000.0f));
      else snprintf(ds, sizeof(ds), ">9999");   // inalcanzable en LoRa: no se miente con un numero
      // %-10.9s = 10 de ancho MINIMO y recorte a 9 (el peor caso real con SSID).
      snprintf(b, sizeof(b), "%-*.*s %*s", kCallCol, kCallCol - 1, hs[i].call, kDatoCol, ds);
    } else {
      uint32_t s = (millis() - hs[i].ms) / 1000;
      char a[12];
      if (s < 60)      snprintf(a, sizeof(a), "%lus", (unsigned long)s);
      else if (s<3600) snprintf(a, sizeof(a), "%lum", (unsigned long)(s/60));
      else             snprintf(a, sizeof(a), "%luh", (unsigned long)(s/3600));
      snprintf(b, sizeof(b), "%-*.*s %*s", kCallCol, kCallCol - 1, hs[i].call, kDatoCol, a);
    }
    drawText(12, y, b, 2); y += 20;
  }
}

// Escena 4: ULTIMOS RX — lista de lo que se ha recibido (la OLED tenia "ULTIMOS RX").
//
// ★ ESCALA 1 Y SALTO DE 20 px (2026-09-15, peticion del operador): antes iba a ESCALA 2
//   (letra de 14 px de alto) con salto 20. Ahora la letra es de 7 px (escala 1) y se MANTIENE
//   el salto de 20, o sea que las filas quedan mas aireadas y se leen de un vistazo.
//
// ★★ EL INDICATIVO YA NO SE CORTA (arreglado 2026-09-15) ★★
//   QUE PASABA: la linea se montaba con `"%.6s  %.0f/%.0f"`, y ese `%.6s` RECORTA el
//   indicativo a 6 caracteres. Un `N0CALL-3` (7) perdia el SSID ENTERO y en la pantalla se
//   leia "N0CALL-" y nada mas; un `N0CALL-15` (9) perdia mas todavia. El operador lo vio
//   como "N0CALL-      -106/-4" y creyo que era cosa del tamano de letra (por eso se bajo a
//   escala 1), pero NO era el tamano: era el formato. Por eso se seguia cortando igual.
//   AHORA: ancho de campo con RECORTE EXPLICITO a la derecha ("%-11.11s"), asi que los
//   indicativos normales salen ENTEROS CON SU SSID y las columnas quedan alineadas (que es
//   de lo que va esta pantalla: poder comparar de un vistazo).
//
//   LA CUENTA DEL HUECO, que es lo que hay que respetar (panel de 200x200):
//     - cabecera: y = 4..25 (la pinta pintaCabecera)
//     - contenido: empieza en y = 30
//     - PIE: de y = 172 en adelante. El texto del "%" de la bateria dibuja en `pintaPie()`
//       con `drawText(..., by - 14, bb, 2)` y `by = 186`, o sea y = 172..185 (14 px de alto
//       a escala 2); el cuerpo de la bateria va en y = 186..199. **NADA de la lista puede
//       pasar de y = 172.**
//     6 filas con salto 20 desde y = 30: la ultima EMPIEZA en 30 + 5*20 = 130 y su letra
//       (7 px) termina en y = 136. Quedan 36 px de aire hasta el pie: cabe con holgura.
//       (A escala 1 el texto NO tiene descendentes: drawChar pinta 7 filas exactas, asi que
//       el calculo es el alto de la letra y nada mas.)
//     ANCHO, CON EL PEOR CASO REAL (`N0CALL-15`, que son 6 caracteres + guion + 2 cifras):
//         2 espacios de margen + [indicativo 11] + 1 espacio + [rssi/snr 9] =
//         2 + 11 + 1 + 9 = 23 caracteres x 6 px = 138 px de los 200. Cabe de sobra.
//         La columna del indicativo empieza en x=12 y la de rssi/snr SIEMPRE en el mismo
//         pixel (x = 12 + 12*6 = 84), que es lo que hace que las columnas se lean en columna.
//   OJO: NO se ha tocado `pintaUltimosTX` (sigue a escala 2) porque el operador pidio
//   expresamente esta escena; alli no hay indicativos que recortar (lo que se pinta son
//   etiquetas cortas: "BEACON", "TELEM"...), asi que no tiene este problema.
void pintaUltimosRX() {
  pintaCabecera("Last RX");
  if (gRxLogN == 0) { drawTextCenter(90, "Nothing received", 2); return; }
  constexpr int kEscala = 1;   // antes 2
  constexpr int kSalto = 20;   // el mismo que antes: mas aire entre filas
  // Ancho de las columnas, en caracteres (a escala 1 cada caracter mide 6 px).
  constexpr int kCallCol = 12;   // 11 de indicativo + 1 de aire (ver la cuenta de arriba)
  constexpr int kDatoCol = 9;    // "-120/-20" = 9 como mucho
  int y = 30;
  for (int i = 0; i < gRxLogN && i < 6; i++) {
    // del mas reciente al mas viejo
    int idx = (gRxLogHead - 1 - i + kRxLogMax) % kRxLogMax;
    const RxLog &e = gRxLog[idx];
    char b[40];
    // %-12.11s = 12 de ancho como MINIMO y recorte a 11 (el peor caso real: "N0CALL-15");
    // %*.*f    = el dato alineado a la derecha en su columna (anchura 9, 0 decimales).
    snprintf(b, sizeof(b), "%-*.*s %*.*f/%.0f",
             kCallCol, kCallCol - 1, e.call,
             kDatoCol, 0, (double)e.rssi, (double)e.snr);
    drawText(12, y, b, kEscala); y += kSalto;
  }
}

// Escena 5: ULTIMOS TX — lista de lo que se ha transmitido (la OLED tenia "ULTIMOS TX").
void pintaUltimosTX() {
  pintaCabecera("Last TX");
  if (gTxLogN == 0) { drawTextCenter(90, "Nothing sent", 2); return; }
  int y = 30;
  for (int i = 0; i < gTxLogN && i < 6; i++) {
    int idx = (gTxLogHead - 1 - i + kTxLogMax) % kTxLogMax;
    const TxLog &e = gTxLog[idx];
    char b[40];
    snprintf(b, sizeof(b), "TX %s", e.what);
    drawText(12, y, b, 2); y += 20;
  }
}

// "34s" / "5m" / "2h" -- igual de compacto que el ageStr() de la OLED
// (display.cpp), pero este fichero no comparte funciones con aquel (son dos
// unidades de compilacion distintas para dos pantallas distintas).
String edadTexto(uint32_t ms) {
  uint32_t s = (millis() - ms) / 1000;
  char b[8];
  if (s < 60) snprintf(b, sizeof(b), "%us", (unsigned)s);
  else if (s < 3600) snprintf(b, sizeof(b), "%um", (unsigned)(s / 60));
  else snprintf(b, sizeof(b), "%uh", (unsigned)(s / 3600));
  return String(b);
}

// Envuelve `s` en lineas de como mucho `maxChars` caracteres (corta por el
// ultimo espacio que quepa, o a saco si una palabra sola no cabe) y las
// dibuja a escala 1 empezando en (x,y). Se para en `maxLineas` lineas o si se
// sale de `yTope`, lo que llegue antes -- un mensaje muy largo se recorta en
// pantalla (el texto COMPLETO sigue en gMsgLog[].text, esto es solo dibujo).
// Devuelve el y siguiente (tras la ultima linea dibujada), para encadenar
// mas contenido debajo.
//
// ★ 2026-09-25: reescrita SIN String (ver la nota igual de larga en
//   msgLogPush): solo puntero + snprintf con un buffer de pila, como
//   rxLogPush/txLogPush. El bug que reporto el operador ("el mensaje se
//   corta tras la primera linea") desaparecio en las pruebas con esta
//   version.
int dibujaTextoEnvuelto(int x, int y, const char *s, int maxChars, int maxLineas, int yTope) {
  if (!s || !s[0]) return y;
  const char *p = s;
  for (int lineas = 0; *p && lineas < maxLineas && y <= yTope; lineas++) {
    int len = (int)strlen(p);
    int corte = len;
    if (corte > maxChars) {
      // ultimo espacio dentro de los primeros maxChars caracteres.
      int esp = -1;
      for (int i = maxChars; i >= 0; i--) {
        if (p[i] == ' ') { esp = i; break; }
      }
      corte = (esp > 0) ? esp : maxChars;   // sin espacio que valga: corta a saco
    }
    char linea[40];   // maxChars (32 en pintaMensajes) + margen: cabe de sobra
    int nCopy = corte;
    if (nCopy >= (int)sizeof(linea)) nCopy = sizeof(linea) - 1;
    memcpy(linea, p, nCopy);
    linea[nCopy] = '\0';
    drawText(x, y, linea, 1);
    y += 8;
    p += corte;
    while (*p == ' ') p++;   // salta el espacio de corte, para la vuelta siguiente
  }
  return y;
}

// Escena 2: MENSAJES -- los ultimos mensajes APRS recibidos, con su cuerpo
// completo (no solo el remitente, que ya sale en "Ultimos RX"). Ver
// gMsgLog / msgLogPush mas arriba.
void pintaMensajes() {
  char t[24];
  snprintf(t, sizeof(t), "Messages (%d)", gMsgLogN);
  pintaCabecera(t);
  if (gMsgLogN == 0) { drawTextCenter(90, "No messages", 2); return; }
  int y = 30;
  // Como el cuerpo puede ser largo se listan como mucho 3: el resto no cabe
  // en 200x200 (el operador ya los vio pasar por el aviso emergente).
  for (int i = 0; i < gMsgLogN && i < 3 && y < 176; i++) {
    int idx = (gMsgLogHead - 1 - i + kMsgLogMax) % kMsgLogMax;
    const MsgLog &e = gMsgLog[idx];
    char cab[32];
    snprintf(cab, sizeof(cab), "%s  %s", e.call, edadTexto(e.ms).c_str());
    drawText(4, y, cab, 1); y += 10;
    // ~32 caracteres por linea a escala 1 (6 px/caracter, 200 px de ancho).
    y = dibujaTextoEnvuelto(4, y, e.text, 32, 3, 176);
    y += 6;   // aire entre mensajes
  }
}

// ★ ¿Este aviso es de TRAFICO (RX / DIGI / TX)? Se reconoce por el PREFIJO EXACTO:
//     displayNoteRx   -> "RX <indicativo>"
//     displayNoteDigi -> "DIGI <indicativo>"
//     displayNoteTx   -> "TX <QUE>"
//   ★ 2026-09-26 (traduccion al ingles): antes bastaba la PRIMERA LETRA ('R' o 'T'),
//     pero con los textos en ingles eso ya no es exacto ("REJECTED", "Tracker: ..."), y
//     "REPITE" paso a ser "DIGI". Por eso ahora se compara el prefijo entero.
//   Los de trafico son los que duran kAvisoMs (1,5 s) porque pueden llegar en rafaga y son
//   los que hacian que el panel estuviera pintando sin parar; los demas (bateria, menu,
//   arranque) duran kLineaMs y hay que poder leerlos.
static bool avisoDeTrafico(const char *s) {
  if (!s || !s[0]) return false;
  return !strncmp(s, "RX ", 3) || !strncmp(s, "TX ", 3) || !strncmp(s, "DIGI ", 5);
}

// Cuanto dura el aviso que hay ahora mismo en pantalla.
static uint32_t duracionAviso() { return avisoDeTrafico(gLinea1) ? kAvisoMs : kLineaMs; }

// ---------------------------------------------------------------------------
//  PANTALLA DE LA SESION "FIJAR COORDS" (2026-09-15)
//
//  Se pinta EN LUGAR del menu o del carrusel mientras dura la captura. Esta
//  arriba del todo en `dibujaEscena()` a proposito: la captura la lanza el menu y
//  sin esto el operador se quedaria mirando una lista quieta (que en tinta parece
//  colgada) durante los minutos que puede tardar el GPS.
//
//  ★★ LO QUE PIDE EL OPERADOR CON ESTA PANTALLA (sus palabras) ★★
//  "¿hay una ventana de estado mientras se realiza la tarea? Pues que muestre como
//  va y en que parte del proceso, para que el usuario este tranquilo."
//  Por eso NINGUN estado se queda sin decir algo que CAMBIE:
//    - BUSCANDO: "Buscando GPS..." + LOS SATELITES QUE VE AHORA (que es lo que
//      demuestra que el aparato esta trabajando, aunque aun no haya fijado) + el
//      aviso de que puede tardar y de que se puede salir con un boton.
//    - ASENTANDO: "Asentando..." + "Muestra n/N" + BARRA DE PROGRESO + satelites.
//    - GUARDADO: latitud y longitud guardadas (la prueba de que se hizo).
//    - FALLO: que no se pudo guardar.
//  Y en todos: "Toca un boton para salir" / "para cancelar". La sesion NO se cierra
//  sola por tiempo: la fijacion tarda lo que tarde (decision del operador).
//
//  ★ EL CONTADOR ES EL DEL RASTREADOR (trackerSetCoordsNeed), no un numero escrito
//    aqui: si alli se cambia el numero de muestras, la pantalla lo dice sola.
// ---------------------------------------------------------------------------
void pintaSesionCoords() {
  pintaCabecera("SET COORDS");

  const GpsData &g = gpsGet();
  char b[40];

  if (gCoordsPantalla == COORDS_BUSCANDO || (gCoordsPantalla == COORDS_ASENTANDO && !g.fix)) {
    drawTextCenter(48, "Searching GPS...", 2);
    // ★ LOS SATELITES A LA VISTA, EN GRANDE Y SIEMPRE: es el unico numero que se
    //   mueve mientras no hay fijacion, y es lo que le dice al operador que el
    //   receptor esta oyendo el cielo y que solo falta esperar. Sin esto, una
    //   pantalla quieta durante minutos parece un cuelgue.
    snprintf(b, sizeof(b), "%u sats in view", (unsigned)g.satsInView);
    drawTextCenter(78, b, 2);
    drawTextCenter(106, "may take minutes", 1);
    drawTextCenter(120, "more open sky = faster", 1);
    drawTextCenter(168, "Press a button to cancel", 1);
  } else if (gCoordsPantalla == COORDS_ASENTANDO) {
    // Hay fijacion: se cuentan las lecturas SEGUIDAS que pide el rastreador antes
    // de dar la posicion por buena (las primeras traen el salto tipico del fix).
    const uint8_t need = trackerSetCoordsNeed() ? trackerSetCoordsNeed() : 1;
    const uint8_t done = trackerSetCoordsDone();
    drawTextCenter(44, "Settling...", 2);
    snprintf(b, sizeof(b), "Sample %u/%u", (unsigned)done, (unsigned)need);
    drawTextCenter(74, b, 2);
    // Barra de progreso: marco + relleno. Con 20 muestras avanza de 5 en 5 %.
    const int bx0 = 20, bx1 = EPD_W - 20, by0 = 100, by1 = 114;
    rectVacio(bx0, by0, bx1, by1);
    const int w = (bx1 - bx0 - 2);
    if (done > 0 && w > 0) relleno(bx0 + 1, by0 + 1, w * (int)done / (int)need, by1 - by0 - 1);
    // Los satelites y la posicion que se esta midiendo: la pantalla sigue contando
    // algo nuevo en cada escalon, no solo el numero de muestras.
    snprintf(b, sizeof(b), "%u sat  %.5f", (unsigned)g.sats, g.lat);
    drawTextCenter(126, b, 1);
    drawTextCenter(168, "Press a button to cancel", 1);
  } else if (gCoordsPantalla == COORDS_GUARDADO) {
    // Se guardo: se ensena LO QUE SE HA GUARDADO, que es la comprobacion que el
    // operador necesita (y asi no hay que ir al menu a mirar Latitud/Longitud).
    drawTextCenter(44, "SAVED", 2);
    hLine(20, EPD_W - 20, 68, 1);
    snprintf(b, sizeof(b), "%.5f", gCoordsLat);
    drawTextCenter(78, b, 2);
    snprintf(b, sizeof(b), "%.5f", gCoordsLon);
    drawTextCenter(98, b, 2);
    drawTextCenter(126, "is the node's fixed position", 1);
    drawTextCenter(140, "GPS turned off again", 1);
    drawTextCenter(168, "Press a button to exit", 1);
  } else if (gCoordsPantalla == COORDS_FALLO) {
    drawTextCenter(54, "SAVE ERROR", 2);
    drawTextCenter(88, "the position was not", 1);
    drawTextCenter(102, "saved to the config", 1);
    drawTextCenter(130, "try again", 1);
    drawTextCenter(168, "Press a button to exit", 1);
  }
}

// ★ Dibuja en `gBuf` lo que toque AHORA (la escena del carrusel, con el aviso reciente
//   ENCIMA si lo hay) y deja `gDirty` puesto. Es el UNICO sitio donde se decide que se ve.
//   Lo usan `displayRefresh()` (el bucle normal) y la prueba del carrusel del comando
//   `epdparcial` (herramienta de taller): asi lo que se prueba es exactamente lo mismo que
//   se vera luego en el carrusel automatico del Paso 2, y no una copia que puede divergir.
void dibujaEscena() {
  clearBuf(true);
  // ★★ LA SESION "FIJAR COORDS" MANDA SOBRE TODO (2026-09-15) ★★
  //   Va ANTES del menu a proposito: la captura se lanza desde el menu y dura
  //   minutos, asi que mientras esta en marcha lo que hay que ver es su progreso,
  //   no la lista (que en tinta parece colgada). Ver pintaSesionCoords().
  if (coordsPantallaActiva()) {
    pintaSesionCoords();
    pintaPie();
    gDirty = true;
    return;
  }
  // ★ Si el menú esta abierto, se pinta el MENU en lugar del carrusel (2026-09-15).
  if (menuIsOpen()) {
    menuPinta();
    // ARREGLO del "pantalla en blanco" (2026-09-15): menuPinta() puede AUTO-CERRAR el menu
    // por el timeout de kMenuInactivoMs y dejar gBuf en blanco. Si tras pintar el menu ya NO esta
    // abierto, se CONTINUA abajo y se dibuja la escena del carrusel, en vez de devolver.
    if (menuIsOpen()) { gDirty = true; return; }
  }
  // ★ Si el asistente SOTA esta abierto, se pinta EL en lugar del carrusel
  //   (2026-09-26, mismo patron que el menu de ajustes justo arriba).
  //   Igual que el menu: pintaSota() puede AUTO-CERRAR el asistente (timeout de kMenuInactivoMs)
  //   y dejar gBuf en blanco; en ese caso se sigue abajo y se pinta el carrusel.
  if (sotaIsOpen()) {
    pintaSota();
    if (sotaIsOpen()) { gDirty = true; return; }
  }
  // ★★ LA ESCENA SE PINTA SIEMPRE; EL AVISO VA ENCIMA (2026-09-15) ★★
  //   Antes esto era un "if (aviso) ... else { escena }": con un aviso en pantalla la escena
  //   NO se dibujaba, asi que el primer refresco dejaba el panel con el aviso solo y hacia
  //   falta un SEGUNDO refresco (al caducar) para volver a ver la escena. Pintando la escena
  //   debajo y el aviso encima, el paso 2 ya es "la escena limpia": un aviso, un refresco de
  //   contenido, y a los 1,5 s la escena de vuelta.
  // ★ 2026-09-27 (firmware dedicado a SOTA): fuera Radio, Sensores, Sistema y GPS (la
  //   posicion ya esta en la escena de inicio) y entra la escena SOTA en segundo lugar.
  //   Los comentarios "Escena N" de cada pintaXxx() se han renumerado igual.
  switch (gEscena) {
    case 1: pintaCumbre(); break;
    case 2: pintaMensajes(); break;
    case 3: pintaEstaciones(); break;
    case 4: pintaUltimosRX(); break;
    case 5: pintaUltimosTX(); break;
    default: pintaEstado(); break;   // 0 = Inicio (la que se ve casi siempre)
  }
  // El pie (PIE DE LA OLED): rectangulitos del carrusel + bateria, SIEMPRE debajo
  // del contenido y por encima de la huella, para que acompañe a todas las escenas.
  pintaPie();
  // El aviso, ENCIMA de todo y sin tapar el pie (que es donde vive la bateria).
  pintaAviso();
  gDirty = true;
}

// Voltaje de bateria que se ENSEÑA (en milivoltios, redondeado a 0,01 V).
//
// ★★ POR QUE NO SE USA `powerReadMv()` AQUI (2026-09-15) ★★
// `powerReadMv()` lee el ADC CADA VEZ que se llama y le pasa un filtro suavizador. En esta
// unidad el divisor de la bateria NO esta poblado (ya estaba documentado: `bat` contesta
// 0,00 V aunque el pin ya es el correcto, P0.04), asi que el convertidor lee RUIDO que cambia
// en cada muestra: el numero que se dibujaba era distinto a cada vuelta del bucle.
// Consecuencia medida: la huella del contenido (`huellaContenido()`) cambiaba sola y el
// firmware **repintaba casi cada segundo** (66 repintados en 60 s), que es justo lo que el
// Paso 2 tiene que evitar y lo que ensucia la pantalla.
// Se usa la MISMA lectura cacheada que el resto del firmware (`sensorsBatteryVolt()`, que ya
// descarta lo que no es un voltaje de bateria plausible) y se redondea a 0,01 V, que es la
// precision con la que se dibuja. Asi lo que se ve y lo que se compara son lo mismo.
//   Devuelve 0 cuando no hay lectura creible (y entonces se escribe "sin lectura").
uint16_t bateriaMv() {
  const float v = sensorsBatteryVolt(gSens);
  if (v <= 0.0f) return 0;
  return (uint16_t)(v * 1000.0f + 0.5f);
}

// ===========================================================================
//  ★★ PASO 2: "NO REPINTAR SI NO HA CAMBIADO NADA" (2026-09-15) ★★
//
//  POR QUE EXISTE ESTO: el firmware repintaba cada 5 s por reloj.
//  cambiara algo o no. Medido en hardware con el carrusel puesto: **19 repintados en 50
//  segundos** (16 de ellos sin motivo). Cada repintado deja su resto de tinta y gasta
//  bateria, y el propio HANDOVER avisa de que el parcial ensucia si se abusa de el.
//
//  COMO SE ARREGLA: antes de pintar se calcula una **huella** de TODO lo que se va a ver en
//  la pantalla que toca —la escena, el modo, el GPS, los sensores, la bateria, los
//  contadores de radio y el aviso de "ultimo"— y se compara con la huella de lo ultimo
//  pintado. Si son iguales, **no se manda nada al panel**.
//
//  ★ DOS COSAS QUE HAY QUE ENTENDER DE ESTA HUELLA, o se rompe sola:
//    1. Se construye con los MISMOS valores y los MISMOS redondeos que usa el dibujo
//       (coordenadas a 5 decimales, velocidad a 0 decimales, bateria a 2...). Si aqui se
//       redondea distinto que alli, o se repinta de mas o —peor— no se repinta cuando la
//       pantalla si ha cambiado.
//    2. Los avisos ("hace Ns") NO entran con su reloj a proposito: entrarian cambiando cada
//       segundo y volveriamos al repintado continuo. El aviso caduca solo (a los 1,5 s si es
//       de trafico, a los 8 s si no: ver `duracionAviso()`) y ese cambio SI se detecta porque
//       la linea pasa a estar vacia. Ese es el segundo y ULTIMO refresco de un aviso: no hay
//       ningun estado intermedio que obligue a un tercero.
//    3. ★ EL AVISO NO CAMBIA LO QUE HAY DEBAJO (2026-09-15): `dibujaEscena()` pinta SIEMPRE
//       la escena y el aviso encima. Si algun dia se volviera a "con aviso, no pintes la
//       escena", volveria el doble paso y esta huella no lo detectaria (la escena no entra en
//       la huella cuando hay aviso: solo entra el aviso).
//
//  Y queda un repintado "de refresco" cada `kRepintadoMaxMs` (60 s) por si algo se escapa de
//  la huella: la pantalla nunca se queda con un dato viejo para siempre.
// ===========================================================================
constexpr uint32_t kRepintadoMaxMs = 60000;   // repintado de refresco, aunque no cambie nada
uint64_t gHuellaPintada = 0;
bool gHuellaValida = false;

void huellaAnade(uint64_t &h, const void *datos, size_t n) {
  const uint8_t *p = (const uint8_t *)datos;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint64_t)p[i];
    h *= 1099511628211ULL;   // FNV-1a de 64 bits
  }
}

uint64_t huellaContenido() {
  uint64_t h = 1469598103934665603ULL;
  huellaAnade(h, &gEscena, sizeof(gEscena));
  const uint8_t modo = (uint8_t)(gCfg ? gCfg->mode : 0);
  huellaAnade(h, &modo, sizeof(modo));

  // Si hay un aviso reciente, lo que se ve es el aviso (no la escena): misma regla que
  // `dibujaEscena()`.
  const uint8_t aviso = (gLinea1[0] != '\0') ? 1u : 0u;
  huellaAnade(h, &aviso, sizeof(aviso));
  huellaAnade(h, gLinea1, sizeof(gLinea1));
  huellaAnade(h, gLinea2, sizeof(gLinea2));

  const GpsData &g = gpsGet();
  const uint8_t fix = g.fix ? 1u : 0u;
  // Altitud a metros enteros, como se dibuja (inicio y diferencia de la escena SOTA).
  const int alt = (g.fix && g.altValid) ? (int)lround(g.altM) : -99999;
  huellaAnade(h, &alt, sizeof(alt));
  huellaAnade(h, &gMsgTotal, sizeof(gMsgTotal));
  // El plan SOTA (escena SOTA): se puede cambiar tambien por web o por `set`.
  if (gCfg) huellaAnade(h, gCfg->sotaPlan, sizeof(gCfg->sotaPlan));
  // Escena SOTA: seguimiento del spot y hora UTC. La hora entra SOLO con esta escena
  // en pantalla (en las demas no se ve), y a MINUTOS: un repintado por minuto.
  if (gEscena == 1) {
    huellaAnade(h, &gSpot, sizeof(gSpot));
    const int minutoUtc = g.timeValid ? (int)g.utcH * 60 + (int)g.utcM : -1;
    huellaAnade(h, &minutoUtc, sizeof(minutoUtc));
  }
  huellaAnade(h, &fix, sizeof(fix));
  const long lat = lround(g.lat * 100000.0);     // 5 decimales, como en pantalla
  const long lon = lround(g.lon * 100000.0);
  huellaAnade(h, &lat, sizeof(lat));
  huellaAnade(h, &lon, sizeof(lon));
  const int vel = (int)lround(g.speedKmh);       // 0 decimales, como en pantalla
  const int sat = (int)g.sats;
  huellaAnade(h, &vel, sizeof(vel));
  huellaAnade(h, &sat, sizeof(sat));

  // ★ `satVista` (los satelites "a la vista", aun sin fijacion) NO entra en la huella.
  //   Medido con el instrumento `epdhuella`: ese numero baila de 5 a 11 en pocos segundos y
  //   era el que hacia que la pantalla se repintara sin parar aunque la linea que se dibuja
  //   apenas cambie. La posicion y la velocidad, que si importan, siguen entrando. La
  //   pantalla pierde un "sat a la vista" que parpadeaba solo; gana no repintarse cada
  //   segundo (2026-09-15).

  // ★ 2026-09-27: los sensores (temperatura, humedad, presion) ya NO entran: desde que se
  //   quitaron la escena Sensores y la linea de meteorologia del inicio no se ven en
  //   ninguna escena, y meterlos solo provocaria repintados sin cambio visible.

  // La MISMA lectura redondeada que se dibuja (ver `bateriaMv()`): el ruido del ADC de esta
  // unidad no puede entrar aqui o la huella cambiaria sola y se repintaria sin parar.
  const uint16_t mv = bateriaMv();
  huellaAnade(h, &mv, sizeof(mv));
  huellaAnade(h, &gRx, sizeof(gRx));
  huellaAnade(h, &gTx, sizeof(gTx));
  huellaAnade(h, &gDg, sizeof(gDg));

  // ★ ESTADO DE "RECIEN TRANSMITIDO" + radio lista/mute (ARREGLO A1 de la auditoria,
  //   2026-09-15): la pastilla RX/TX y el aviso de TX se deciden con
  //   `radioLastTxMs()<3000` y `txDisabled`/`radioReady()`, pero esos datos NO entraban en
  //   la huella, asi que tras una emision la pastilla negra "TX" se quedaba CONGELADA hasta
  //   los 60 s (el panel es bistable). Meter aqui el flag hace que a los 3 s la huella
  //   cambie y el repintado la devuelva a RX/borre el aviso.
  const uint8_t txReciente = (radioLastTxMs() != 0 &&
                             (uint32_t)(millis() - radioLastTxMs()) < 3000) ? 1u : 0u;
  huellaAnade(h, &txReciente, sizeof(txReciente));
  const uint8_t txMudo = (gCfg && gCfg->txDisabled) ? 1u : 0u;
  huellaAnade(h, &txMudo, sizeof(txMudo));
  const uint8_t radioListo = radioReady() ? 1u : 0u;
  huellaAnade(h, &radioListo, sizeof(radioListo));

  // ★ PERFIL ACTIVO (2026-09-15): la escena Estado dibuja un icono que depende de
  //   `smartBeaconPreset`. Elegirlo desde el MENU ya fuerza repintado (`gDirty`), pero se
  //   puede cambiar tambien desde el configurador web o por CLI, y ahi nadie avisa: el panel
  //   es bistable, asi que sin meterlo en la huella el icono se quedaria enseñando el perfil
  //   VIEJO hasta el repintado de refresco de 60 s. Es el mismo fallo --y el mismo arreglo--
  //   que el "arreglo A1" de la pastilla TX de arriba.
  const uint8_t perfilIcono = (uint8_t)(gCfg ? gCfg->smartBeaconPreset : 0);
  huellaAnade(h, &perfilIcono, sizeof(perfilIcono));
  // ★ ICONO DEL MAPA DEL PERFIL (2026-09-15): el par (tabla, codigo) de cada perfil
  //   se puede cambiar desde el MENU, desde el configurador web o con `set`, y el
  //   icono de la ESCENA tambien es un dato del perfil: si se cambia por web, sin
  //   esto la pantalla se quedaria con el dibujo viejo hasta el repintado de 60 s.
  if (gCfg) {
    for (int i = 0; i < 4; i++) {
      huellaAnade(h, gCfg->profileOverlay[i], 1);
      huellaAnade(h, gCfg->profileSymbol[i], 1);
    }
  }
  // ★ SESION "FIJAR COORDS" (2026-09-15): la pantalla de la captura solo puede
  //   cambiar en unos pocos numeros (fase, muestras hechas, muestras que hacen
  //   falta y la posicion guardada). Con ellos en la huella, el progreso se
  //   repinta SOLO cuando cambia de escalon y no en cada vuelta del bucle.
  const uint8_t coordsPantalla = gCoordsPantalla;
  huellaAnade(h, &coordsPantalla, sizeof(coordsPantalla));
  if (coordsPantalla != COORDS_OCULTA) {
    const uint8_t need = trackerSetCoordsNeed();
    const uint8_t done = trackerSetCoordsDone();
    huellaAnade(h, &need, sizeof(need));
    huellaAnade(h, &done, sizeof(done));
    huellaAnade(h, &gCoordsLat, sizeof(gCoordsLat));
    huellaAnade(h, &gCoordsLon, sizeof(gCoordsLon));
  }
  return h;
}

// ★★ INSTRUMENTO: ¿QUE DATO DE LA HUELLA ESTA CAMBIANDO? (2026-09-15) ★★
//
// Cuando el firmware repinta "sin motivo", el problema esta en un dato que entra en la huella
// y que cambia solo. Adivinar cual cuesta tardes; medirlo, dos comandos. Esto saca la huella
// y TODOS sus ingredientes en una linea: se llama dos veces y se comparan los numeros.
//
// Existe porque con la huella puesta se midieron 52 repintados en 60 segundos y el primer
// sospechoso (la bateria, cuyo ADC lee ruido en esta unidad) NO era el unico: hay que seguir
// midiendo hasta que el numero de repintados baje a lo que cambia de verdad.
void epdHuellaTexto(char *out, size_t n) {
  const GpsData &g = gpsGet();
  snprintf(out, n,
           "EPD huella=0x%08lX%08lX escena=%d aviso=%d | GPS fix=%d lat5=%ld lon5=%ld "
           "vel=%d sat=%d satVista=%d | temp=%d hum=%d pres=%d | batMv=%u rx=%lu tx=%lu dg=%lu",
           (unsigned long)(uint32_t)(huellaContenido() >> 32),
           (unsigned long)(uint32_t)(huellaContenido() & 0xFFFFFFFFULL),
           (int)gEscena, (gLinea1[0] != '\0') ? 1 : 0,
           g.fix ? 1 : 0, lround(g.lat * 100000.0), lround(g.lon * 100000.0),
           (int)lround(g.speedKmh), (int)g.sats, (int)g.satsInView,
           gSens.tempOk ? (int)lround(gSens.tempC * 10.0) : -9999,
           gSens.humOk ? (int)lround(gSens.hum) : -9999,
           gSens.pressOk ? (int)lround(gSens.pressHpa) : -9999,
           (unsigned)bateriaMv(), (unsigned long)gRx, (unsigned long)gTx, (unsigned long)gDg);
}

}  // namespace

void displayRefresh(const DigiConfig &cfg, uint32_t rxCount, uint32_t txCount,
                    uint32_t digiCount, const SensorReadings &r) {
  if (!gReady) return;
  gRx = rxCount; gTx = txCount; gDg = digiCount; gSens = r;

  // ★★ SESION "FIJAR COORDS": SEGUIMIENTO DESDE LA PANTALLA (2026-09-15) ★★
  // El rastreador es quien manda sobre el GPS (trackerSetCoordsStart/Tick, ver
  // tracker.cpp); aqui solo se MIRA en que punto esta y se ensena. El aviso por
  // USB ya lo da el rastreador ("{\"setcoords\":...}"), asi que aqui no se repite.
  //   TRK_COORDS_BUSCANDO  -> pantalla "Buscando GPS..." (sin tope de tiempo)
  //   TRK_COORDS_ASENTANDO -> pantalla "Asentando... n/N" con barra
  //   TRK_COORDS_GUARDADO  -> pantalla con la posicion guardada
  //   TRK_COORDS_ERROR     -> pantalla de error al guardar
  // ★ SOLO SE DECIDE REPINTAR POR ESCALONES (0, 25, 50, 75, 100 %): cada refresco
  //   de este panel cuesta 1,5 s y lo manda la HUELLA, que es la que compara el
  //   texto que se vera. Con el numero de muestras exacto se repintaria en cada
  //   muestra (~20 refrescos, 30 s de panel pintando) para ver cambiar un digito.
  //   Los SATELITES a la vista son la excepcion a proposito: cuando no hay
  //   fijacion son el unico dato que se mueve, y sin ellos la pantalla parece
  //   colgada (que es justo lo que el operador no quiere). Se refrescan de 30 en
  //   30 s como mucho.
  {
    static uint8_t pasoPintado = 255;
    static uint32_t ultimoSatsMs = 0;
    const uint8_t estado = trackerSetCoordsEstado();
    if (!coordsPantallaActiva()) {
      pasoPintado = 255;   // sesion cerrada: el proximo aviso empieza limpio
    } else if (estado == TRK_COORDS_GUARDADO || estado == TRK_COORDS_ERROR) {
      gCoordsPantalla = (estado == TRK_COORDS_GUARDADO) ? COORDS_GUARDADO : COORDS_FALLO;
      pasoPintado = 255;
      gDirty = true;
      if (diagTrazaTaller()) {
        Serial.printf("PANTALLA: fijar coords -> %s\r\n",
                      (estado == TRK_COORDS_GUARDADO) ? "guardado" : "error");
      }
    } else {
      const bool asentando = (estado == TRK_COORDS_ASENTANDO);
      const uint8_t need = trackerSetCoordsNeed() ? trackerSetCoordsNeed() : 1;
      const uint8_t done = trackerSetCoordsDone();
      const uint8_t paso = asentando ? (uint8_t)((done * 4u) / need) : 0;   // 0..4
      const uint8_t quiero = asentando ? COORDS_ASENTANDO : COORDS_BUSCANDO;
      // Mientras se busca, el dibujo lleva los satelites a la vista: se repinta
      // cada 30 s para que el operador VEA que el receptor trabaja (y solo si el
      // numero ha cambiado, que de eso se encarga la huella).
      const bool tocaSats = !asentando &&
                            (uint32_t)(millis() - ultimoSatsMs) >= 30000u;
      if (paso != pasoPintado || quiero != gCoordsPantalla || tocaSats) {
        if (tocaSats) ultimoSatsMs = millis();
        pasoPintado = paso;
        gCoordsPantalla = quiero;
        gDirty = true;
      }
    }
  }

  // ★ TOQUE EN LA PANTALLA DE LA SESION (2026-09-15).
  //   - CON LA CAPTURA EN MARCHA: el toque CANCELA. Es la condicion que puso el
  //     operador al quitar el tope de tiempo ("que el usuario pueda salir de ahi
  //     con un boton"): la sesion espera lo que haga falta, asi que la salida la
  //     decide el, no un reloj. trackerSetCoordsCancel() apaga el GPS si lo
  //     encendio la sesion y lo deja como estaba si ya estaba encendido.
  //     ★ El guardia de kDebounceToqueMs compara con el momento en que se LANZO la
  //     sesion (no solo con el ultimo toque): asi el toque con el que el operador
  //     acaba de elegir "Fijar coords" en el menu no cancela lo que acaba de
  //     arrancar.
  //   - CON RESULTADO YA EN PANTALLA (guardado o error): el toque la quita y
  //     vuelve al menu o al carrusel, sin tocar nada mas.
  {
    const bool toque = (uint32_t)(millis() - gUltimoToqueMs) < kDebounceToqueMs;
    if (coordsPantallaActiva() && toque) {
      if (gCoordsPantalla == COORDS_GUARDADO || gCoordsPantalla == COORDS_FALLO) {
        coordsPantallaCierra();
      } else if ((uint32_t)(millis() - gCoordsInicioMs) >= kDebounceToqueMs) {
        trackerSetCoordsCancel();   // fuera de la sesion y con el GPS devuelto
        coordsPantallaCierra();
        if (diagTrazaTaller()) {
          Serial.println("PANTALLA: fijar coords cancelado por el operador");
        }
      }
    }
  }

  // ★★ CAMBIO DE ROTACION EN CALIENTE (2026-09-14) ★★
  // Si el usuario guarda `epdRotation` (configurador web, `set epdRotation N`), la
  // configuracion cambia y aqui se aplica SIN REINICIAR: se repinta con la orientacion
  // nueva. La variable se lee de la config, no de una constante, y se compara con la
  // ultima que se pinto.
  {
    const uint8_t r = (cfg.epdRotation <= 3) ? cfg.epdRotation : 0;
    if ((int)r != gRotacion) {
      gRotacion = (int)r;
      gDirty = true;
      gUltimoPintado = 0;   // que el repintado no se posponga por el limitador de tiempo
      if (diagTrazaTaller()) {
        Serial.printf("PANTALLA: rotacion cambiada a %d, repintando\r\n", gRotacion);
      }
    }
  }

  // ★★ PASO 2: EL CARRUSEL AUTOMATICO (2026-09-15) ★★
  // El ajuste `sceneAutoAdvance` (el mismo de la OLED) manda aqui de verdad. La pausa tras
  // una pulsacion del boton se respeta siempre: si el operador acaba de elegir pantalla, el
  // carrusel no se la cambia.
  gAutoAvance = cfg.sceneAutoAdvance;
  {
    const uint32_t ahoraCarrusel = millis();
    if (gUltimoCambioEscenaMs == 0) gUltimoCambioEscenaMs = ahoraCarrusel;   // arranque de la cuenta
    const bool pausado = (int32_t)(ahoraCarrusel - gCarruselPausadoHasta) < 0;
    if (gAutoAvance && !pausado && (ahoraCarrusel - gUltimoCambioEscenaMs) >= kEscenaAutoMs) {
      gUltimoCambioEscenaMs = ahoraCarrusel;
      gEscena = (uint8_t)((gEscena + 1) % kNumEscenas);
      gDirty = true;
      if (diagTrazaTaller()) {
        Serial.printf("PANTALLA: carrusel -> escena %d\r\n", (int)gEscena);
      }
    }
  }

  // El aviso caduca solo: asi la pantalla vuelve a la escena normal. ★ UN SOLO PASO
  // (2026-09-15): los avisos de TRAFICO (RX / REPITE / TX) duran `kAvisoMs` (1,5 s) y los
  // demas `kLineaMs` (8 s). Este es el UNICO sitio donde expira un aviso: la banda "TX" que
  // tenia su propio reloj de 3000 ms ya no existe (ver pintaAviso).
  if (gLinea1[0] && (millis() - gLineaMs) > duracionAviso()) {
    gLinea1[0] = '\0';
    gLinea2[0] = '\0';
    gDirty = true;
  }

  // ★★ PASO 2: SOLO SE REPINTA SI LO QUE SE VA A VER ES DISTINTO (2026-09-15) ★★
  // Antes esto era "repintar cada 5 s pase lo que pase", y con el carrusel se midieron 19
  // repintados en 50 s (16 sin motivo). Ahora manda la huella del contenido; el reloj solo
  // fuerza un repintado de refresco cada `kRepintadoMaxMs`, por si algo se escapa.
  const uint32_t ahora = millis();
  const uint64_t huella = huellaContenido();
  const bool cambioElContenido = (!gHuellaValida || huella != gHuellaPintada);
  const bool tocaPorTiempo = (ahora - gUltimoPintado) > kRepintadoMaxMs;

  // ★ TOQUE RAPIDO (2026-09-15): si acabas de TOCAR EL TACTIL hace menos de
  //   kAgrupaToquesMs, se APLAZA el repintado (se mantiene gDirty sin pintar). Asi, si
  //   tocas varias veces seguidas en el carrusel o el menu, la posicion LOGICA avanza al
  //   instante en cada toque y solo se pinta UNA vez cuando dejas de tocar, mostrando la
  //   posicion final.
  //   ★ T-ECHO PROJECT BUTTER (2026-09-15): esto SOLO cuenta para el tactil capacitivo
  //     (`gUltimoToqueAgrupaMs`). El boton fisico ya no lo escribe: su toque corto sale
  //     600 ms despues de soltar (ventana del doble) y dos cortos nunca caen dentro de
  //     estos 400 ms, asi que lo unico que hacia era retrasar 400 ms CADA cambio de
  //     diapositiva pedido con el boton. Ver la nota de kAgrupaToquesMs.
  const bool toqueReciente = (ahora - gUltimoToqueAgrupaMs) < kAgrupaToquesMs;
  if (!gDirty && !cambioElContenido && !tocaPorTiempo) return;
  if (toqueReciente && !gMenuEditing && gDirty) return;   // espera a que dejes de tocar

  gUltimoPintado = ahora;
  gHuellaPintada = huella;
  gHuellaValida = true;
  gDirty = false;
  gRotacionAplicada = gRotacion;   // queda constancia de con cual se ha pintado

  // ★★ OJO CON EL ORDEN, QUE AQUI SE COLO UN FALLO (2026-09-15) ★★
  // `dibujaEscena()` dibuja en memoria y deja puesto `gDirty` ("hay algo que mandar").
  // `epdFlush()` es quien mira ese aviso y pinta de verdad. La version anterior de este
  // bloque borraba `gDirty` DESPUES de dibujar y ANTES de llamar a `epdFlush()`, asi que
  // `epdFlush()` se salia por su primera linea y **no mandaba nada al panel**.
  // Sintoma exacto que se midio en hardware: el carrusel avanzaba de escena (se veia el
  // mensaje "carrusel -> escena N" en el USB) pero `parciales=0` y `completos=2`: la
  // pantalla no cambiaba NUNCA. No se toca este orden sin entender esto.
  dibujaEscena();
  epdFlush();
}

