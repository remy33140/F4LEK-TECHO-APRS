// Faketec_APRS_Igate_EA2OY — main
// APRS-LoRa digipeater/tracker firmware for Faketec V1-V6 (nRF52840) + SX1262
// License: GPL-3.0
//
// Phases: config v1 + WebSerial protocol + CLI, SX1262 @433.775, persistence,
// beacon (WX/battery), digi core, sensors (INA219/AHT20/BMP280), OLED UI
// (scenes/popups/menu), tracker mode (GPS + SmartBeaconing), sleep/resilience.

#include <Arduino.h>
#include <RadioLib.h>

#include "aprs.h"
#include "button.h"
#include "config.h"
#include "diag.h"
#include "display.h"
#include "flog.h"
#include "gps.h"
#include "haptic.h"
#include "pins_board.h"
#include "power.h"
#include "protocol.h"
#include "radio.h"
#include "sensors.h"
#include "store.h"
#include "tnc.h"
#include "tracker.h"

static DigiConfig gConfig;
static ConfigProtocol gProtocol(gConfig);

// Ultimo valor de "GPS n/N" que se ha enseñado de la sesion "Fijar coords".
// Vive aqui fuera del bucle para poder limpiarlo cuando la sesion termina (ver el
// comentario en loop()): dentro del `if` no habia forma de resetearlo.
static uint8_t gSetCoordsShown = 255;

// --- KISS mode: the host application drives the node ------------------------
// OPERATOR DECISION (explicit request, 2026-09-13): while the USB TNC bridge is
// in KISS (cfg.tncProtocol == 2) the node must NOT put its own automatic packets
// on the air. No scheduled position beacons, no telemetry, no weather, no
// periodic status, no node-sent notices: the APP decides what is transmitted and
// when, and the node only sends what the host hands it. Digipeating is NOT
// affected (that is reception-driven, not a packet of our own), and neither is
// TNC2 (protocol 1), which keeps its beacons exactly as it always did.
// This one flag is the single gate every automatic transmission asks first.
static bool tncHostDriven() {
  // KISS manda cuando el selector esta en KISS Y no se ha abierto con kissoff.
  // `kissoff` (tncKissPause) devuelve el mando al operador sin tocar el selector:
  // el nodo vuelve a obedecer sus ordenes normales por USB. Ver tnc.h.
  return gConfig.tncProtocol == CFG_TNC_KISS && !tncKissPaused();
}

// ===========================================================================
//  ★★ T-ECHO PROJECT BUTTER: COLA DE GESTOS (2026-09-15) ★★
//
//  POR QUE HACE FALTA UNA COLA: la pantalla de tinta tarda 1,5-3 s por refresco y el
//  driver ESPERA al panel dentro de la vuelta del bucle. Un toque que llega en ese
//  rato ya no se pierde (los flancos los coge la interrupcion, ver button.cpp), pero
//  el GESTO se resolvia tarde: si el operador tocaba al principio de un refresco de
//  1,5 s, la ventana del corto vencia con el bucle todavia pintando, y el gesto no se
//  atendia hasta 1,5 s despues. Medido: toque -> accion visible = ventana + resto del
//  refresco + OTRO refresco entero.
//
//  AHORA: el driver de la pantalla llama a `bombearBoton()` en sus esperas (ver
//  displaySetPumpBoton), asi que la maquina de gestos SIGUE CORRIENDO mientras el
//  panel pinta y deja el gesto ENCOLADO aqui. En cuanto el panel queda libre, el
//  bucle ejecuta la accion: el toque se atiende "despues", pero se atiende.
//
//  ★ `bombearBoton()` NO ejecuta acciones, solo encola: quien manda una baliza o abre
//    el menu es el bucle, nunca el driver de la pantalla (no se puede entrar a pintar
//    desde dentro de un pintado).
// ===========================================================================
static ButtonEvent gColaGestos[6];
static uint8_t gColaGestosN = 0;
// ★★ LOS TOQUES TAMBIEN SE ACUMULAN (2026-09-16) ★★
// Antes esto era un `bool`: si el operador tocaba cuatro veces mientras la pantalla
// pintaba, al bucle solo le llegaba UN toque y el menu bajaba UNA fila. Con el carrusel
// pasaba igual (cuatro toques, una diapositiva). El boton fisico ya tenia su cola; el
// tactil no tenia nada. Palabras del operador: «cuesta bastante esfuerzo y tiempo
// moverse por los menus, esto rompe la idea del Project Butter».
static uint8_t gToquesPendientes = 0;
constexpr uint8_t kToquesPendientesMax = 8;   // red de seguridad (una mano apoyada, no 40)

static void ponEnCola(ButtonEvent ev) {
  if (ev == BTN_NONE) return;
  if (gColaGestosN >= (uint8_t)(sizeof(gColaGestos) / sizeof(gColaGestos[0]))) {
    for (uint8_t i = 1; i < gColaGestosN; i++) gColaGestos[i - 1] = gColaGestos[i];
    gColaGestosN--;
  }
  gColaGestos[gColaGestosN++] = ev;
}

// Gancho de la pantalla (y del bucle): captura + resuelve + ENCOLA. Nada mas.
static void bombearBoton() {
  ButtonEvent ev;
  while ((ev = buttonPoll()) != BTN_NONE) ponEnCola(ev);
  // ★ EN UN `while`, PARA COBRAR LA RAFAGA ENTERA: buttonTouchPoll() devuelve un toque
  //   aceptado por llamada. Con un `if` se perdian todos menos uno (ver gToquesPendientes).
  while (buttonTouchPoll()) {
    if (gToquesPendientes < kToquesPendientesMax) gToquesPendientes++;
  }
}

// ===========================================================================
//  ★★ T-ECHO PROJECT BUTTER II: EL USB TAMBIEN SE LEE DURANTE EL REPINTADO
//     (2026-09-16) ★★
//
//  EL PROBLEMA: el parser del USB se alimentaba SOLO desde el bucle (la llamada a
//  `gProtocol.feed(Serial)` de mas abajo) y el bucle se pasa 1,5-3 s dentro del driver de
//  la tinta esperando al panel. En ese rato el puerto no lo leia nadie: el FIFO del CDC
//  son 256 bytes, asi que una linea larga (el `set` del configurador son ~1.4 KB) no
//  entraba de una vez y el nodo necesitaba VARIAS vueltas del bucle —cada una con su
//  repintado delante— para juntarla entera.
//
//  LA SOLUCION, Y ES EL MISMO PATRON QUE EL DEL BOTON: el driver de la tinta llama a este
//  gancho en sus esperas (cada 2 ms, ver displaySetPumpBoton / ePDBombea en
//  epaper_techo.cpp), y aqui se hacen DOS cosas y solo dos:
//    1) bombear el boton  -> captura + resuelve + ENCOLA el gesto (Project Butter).
//    2) bombear el USB    -> LEE el puerto y ENCOLA los bytes (gProtocol.bombea()).
//
//  ★★ LO QUE NO SE HACE AQUI, Y ES LO IMPORTANTE: EJECUTAR.  Ni un gesto ni un comando se
//     obedecen dentro del driver. Si un comando se ejecutara aqui podria disparar un
//     repintado (pintar dentro del pintado) o escribir la flash en medio de una
//     transaccion con el panel. Los gestos los cobra `handleButton()` y los comandos
//     `gProtocol.atiende()`/`feed()`, los dos EN EL BUCLE.
// ===========================================================================
static void bombeaEsperaPantalla() {
  // ★ RED DE SEGURIDAD: mientras corre este gancho estamos DENTRO del driver y no se puede
  //   ejecutar nada. La marca la quita el bucle en cuanto displayRefresh() vuelve (ver
  //   loop()). Si algun dia alguien ejecutara un comando aqui, `usb` lo diria: `dentro` != 0.
  gProtocol.marcaEnPantalla(true);
  bombearBoton();                          // gestos: captura + resuelve + ENCOLA
  gProtocol.bombea(Serial, true);          // USB: LEE Y ENCOLA (nunca ejecuta)
}

// ★ AVISO INMEDIATO AL PULSAR (2026-09-15). Se llama en el flanco de PULSACION
//   confirmado (~25 ms despues de poner el dedo), SIN esperar a saber si el gesto es
//   corto (600 ms), largo o doble. Es la unica forma de que "se note" al instante en
//   una pantalla que no puede repintar antes de 1,5 s: la luz se enciende y el Plus
//   pita en el acto, y luego llega la accion. Antes esto se hacia cuando el gesto se
//   resolvia, o sea hasta 800 ms despues de haber tocado.
static void feedbackPulsacion() {
  displayBacklightKick();   // instantaneo en las dos T-Echo (P1.11)
  displayBeep();            // ~60 ms, solo el Plus (en el T-Echo normal no hace nada)
}

// Accion del TACTIL CAPACITIVO (P0.11). Ya viene filtrado por button.cpp: un toque
// estable, fuera de una transmision y con su bloqueo, o sea UN toque = UNA accion
// (antes cualquier ruido o el RF propio daban varias acciones de golpe).
//
// ★ SIN EL AVISO, QUE SE DA UNA VEZ POR RAFAGA (2026-09-16): de este cuerpo se cobran
//   VARIOS seguidos cuando el operador toca en rafaga (ver handleButton), y no tiene
//   sentido repetir luz+pitido+vibracion por cada uno: sonarian pegados.
static void accionToqueAccion() {
  if (sotaIsOpen()) sotaNavigate();            // capacitivo navega en el asistente SOTA
  else if (menuIsOpen()) menuNavigate();       // capacitivo navega en el menu
  else if (!displayIsOn()) displayWake();
  else displayNextScene(true);                // true = venia del tactil (agrupa repintados)
}

static void despachaEvento(ButtonEvent ev) {
  if (ev == BTN_SHORT) {
    if (!displayIsOn()) {
      displayWake();
      if (!tncActive()) Serial.println(F("{\"button\":\"display\"}"));
    } else if (sotaIsOpen()) {
      sotaShort();   // confirmar paso / enviar el spot al llegar a DONE
    } else if (menuIsOpen()) {
      menuShort();
    } else {
      displayNextScene();   // false = boton fisico: no se aplaza el repintado
    }
    return;
  }

  if (ev == BTN_LONG) {
    if (!displayIsOn()) {
      displayWake();
    } else if (sotaIsOpen()) {
      sotaLong();   // volver un paso / salir del asistente si ya esta en el primero
    } else if (menuIsOpen()) {
      menuLong();  // enter / edit / execute / confirm
    } else {
      menuOpen();
    }
    return;
  }

  if (ev == BTN_DOUBLE) {
    // Double tap = manual beacon (its "field" gesture). Dentro del menu el doble toque NO
    // dispara nada descontrolado: si estas editando, solo cancela la edicion (vuelve a la
    // lista); si no estas editando, no hace nada. Antes hacia menuShort() dos veces, lo que
    // podia EJECUTAR DOS VECES una accion (dos balizas, reinicio, borrado) — incoherente.
    // ★ 2026-09-26: mismo criterio con el asistente SOTA abierto -- el doble toque no debe
    //   colarse y mandar una baliza de posicion a medio rellenar un spot.
    if (displayIsOn() && sotaIsOpen()) return;
    if (displayIsOn() && menuIsOpen()) {
      if (menuIsEditing()) menuEditCancel();
      return;
    }
    if (tncHostDriven()) return;  // KISS: the app sends, the node stays quiet
    // Doble toque = baliza de posicion a mano. Peticion del operador
    // (2026-09-13): si el GPS tiene fijacion se manda la posicion REAL; si no la
    // tiene, se manda la ULTIMA POSICION CONOCIDA (la del pico mas reciente,
    // recordada en la flash y con SU hora). Nunca se manda la posicion fija
    // configurada: esa no es una posicion real, y publicarla seria mentir en el
    // mapa. Si no hay nada recordado, no se manda nada y se avisa en pantalla.
    int16_t st;
    const char *what;
    if (gpsPowered() && gpsGet().fix) {
      st = aprsSendTrackerBeacon(gConfig, gpsGet());
      what = "tracker_beacon";
    } else if (trackerHasLastKnown()) {
      st = trackerBeaconLastKnown(gConfig);
      what = "last_known";
    } else {
      displayPopup("No known GPS position");
      st = -111;
      what = "none";
    }
    if (!tncActive()) {
      Serial.print(F("{\"button\":\""));
      Serial.print(what);
      Serial.print(F("\",\"tx\":"));
      Serial.print(st == RADIOLIB_ERR_NONE ? "true" : "false");
      Serial.print(F(",\"code\":"));
      Serial.print(st);
      Serial.println(F("}"));
    }
  }
}

// Servicio del boton, UNA sola puerta: captura + resuelve + ejecuta las acciones.
static void handleButton() {
  bombearBoton();
  for (uint8_t i = 0; i < gColaGestosN; i++) despachaEvento(gColaGestos[i]);
  gColaGestosN = 0;
  // ★★ LA RAFAGA DE TOQUES SE COBRA ENTERA, Y SE PINTA UNA VEZ (2026-09-16) ★★
  //   Cuatro toques = cuatro navegaciones seguidas (o cuatro diapositivas), y el
  //   repintado lo sigue agrupando la pantalla: mientras los toques son recientes
  //   (kAgrupaToquesMs, 400 ms) el driver NO pinta, asi que se paga UN refresco de 1,5 s
  //   y se ve la posicion FINAL, no las cuatro intermedias. Eso es justo lo que pide el
  //   Project Butter: que la tinta no marque el ritmo de la mano.
  //   El aviso (luz + pitido + vibracion) se da UNA vez por rafaga, no por toque: la
  //   vibracion del tactil existe (peticion del operador, 2026-09-15) para saber que el
  //   toque ha contado, y con cuatro toques seguidos lo que se notaria es un zumbido.
  if (gToquesPendientes) {
    const uint8_t n = gToquesPendientes;
    gToquesPendientes = 0;
    displayBacklightKick();
    displayBeep();
    hapticAviso(HAP_TOQUE);      // solo el Plus tiene motor; en el normal no hace nada
    for (uint8_t i = 0; i < n; i++) accionToqueAccion();
  }
}

/* ★★ T-ECHO PROJECT BUTTER: ESTA FUNCION YA NO ESPERA (2026-09-15) ★★
   QUE HACIA ANTES: se quedaba 12 ms en espera activa llamando a handleButton(), seis
   veces por vuelta (72 ms de reloj por vuelta) para "drenar" los toques que hubieran
   caido dentro de una operacion que bloquea (un borrado de flash son ~85 ms, una
   transmision son cientos de ms). El comentario decia que asi "los flancos que
   llegaron durante el atasco se procesan en cuanto el bucle vuelve" — pero eso es
   FALSO leyendo NIVELES: un toque que empieza y acaba dentro del atasco deja el pin
   en el MISMO nivel que estaba, y no hay flanco que procesar. Solo se recuperaba si
   el dedo seguia puesto al volver. De ahi "toques reales que nunca se obedecen".
   QUE HACE AHORA: nada de esperar. Los flancos los coge la INTERRUPCION (button.cpp),
   asi que no hay nada que recuperar; lo unico que hace falta es empujar la maquina de
   gestos por si ha vencido un plazo mientras el bucle estaba dentro de una operacion
   larga, y ejecutar lo que haya salido. Cuesta microsegundos, y se quitan 72 ms de
   espera activa por vuelta (latencia pura para la radio, el GPS y el propio boton). */
static void drainButton() { handleButton(); }



void setup() {
  Serial.begin(115200);
  /* ★★★ LA ESPERA DEL ARRANQUE: 500 ms, LOS MISMOS QUE LA VERSION QUE FUNCIONA ★★★
     (2026-09-21) Aqui se probo a poner 2500 ms copiando lo que hace el banco de diagnostico
     del Bluetooth (`diag_ble`), con la idea de que el SoftDevice necesita que el USB termine
     de enumerarse. DURANTE UNOS DIAS ESTUVO ASI... y se colo en el firmware publicado, que
     es el que dejo al operador SIN PANTALLA Y SIN TACTIL.
     QUE SE HA MEDIDO: el b13 (el que funciona) lleva `delay(500)`. Con 2500 ms, el tactil
     capacitivo no responde. Asi que se vuelve a los 500 ms y NO SE VUELVE A SUBIR SIN
     MEDIRLO: es una espera que se paga EN CADA ARRANQUE y que ya nos ha costado una tarde.
     (El Bluetooth necesita su espera, si algun dia vuelve: se hara DENTRO de su arranque,
     no aqui, para que no la pague todo el firmware.) */
  delay(500);
  configSetDefaults(gConfig);
  const bool hayConfig = storeLoad(gConfig);
  aprsBindConfig(&gConfig);
  tncBindConfig(&gConfig);
  diagBindConfig(&gConfig);
  displayBindConfig(&gConfig);
  // ★★ EL PROTOCOLO Y EL BLUETOOTH (2026-09-17) ★★
  //   El protocolo se engancha aqui para que los bytes del Bluetooth puedan entrar en el
  //   MISMO receptor de lineas que los del cable: el enlace Bluetooth escribe en ese objeto
  //   desde ble_kiss.cpp, y este fichero es el unico que lo conoce (es un objeto global de
  //   aqui). Sin este enganche, `bleLoop()` no tendria a quien dar los bytes del huesped.
  configProtocolBind(&gProtocol);

  /* ★★★ EL BLUETOOTH SE ARRANCA AQUI, LO PRIMERO DE TODO (2026-09-17, b35) ★★★
     Y esto es un ARREGLO, no un capricho de orden. El SoftDevice **se reserva los niveles de
     prioridad de interrupcion 0, 1 y 4** (`nrf_nvic.h`): si al llamar a `sd_softdevice_enable()`
     hay UNA interrupcion habilitada con uno de esos niveles, la llamada falla con
     `NRF_ERROR_SDM_INCORRECT_INTERRUPT_CONFIGURATION` (`0x1001`). Y como el `HardFault_Handler`
     de este core **reinicia** (`cores\nRF5\utility\debug.cpp`), un fallo ahi se ve como un
     **bucle de reinicios** con el USB montando y desmontando — que es justo lo medido en la
     placa con el b25, el b32 y el b33.

     Antes se arrancaba al FINAL del setup, con el reloj, los botones, el `Wire`, la pantalla,
     la radio (RadioLib) y el USB ya inicializados y con sus interrupciones puestas. Ahora se
     arranca aqui: **antes de `powerInit()` y de todo lo demas**, que es la unica diferencia
     funcional que le queda al firmware que SI hace funcionar el Bluetooth en este hardware
     (el del T-Echo del proyecto `cfr34k/t-echo-lora-aprs`: alli el stack sube antes de que la
     aplicacion encienda sus perifericos).

     Lo que NO cambia: si `bleEnabled` esta apagado no se toca el SoftDevice, la sonda estricta
     sigue decidiendo si se puede intentar, y el USB lo devuelve el propio arranque del stack
     (`usb_softdevice_post_enable`) en la linea siguiente a habilitarlo. */
  // 2026-09-21: AQUI ESTABA `bleLinkInit()` (el arranque del enlace Bluetooth). Se ha
  // QUITADO DE ESTE REPOSITORIO a proposito, por decision del operador: el Bluetooth
  // rompia el funcionamiento del nodo. Los ficheros del enlace (ble_kiss.cpp/.h) NO
  // estan en el arbol publicado: se quedan con extension .off, que es como ya se
  // publico la version probada. O sea: aqui no hay NADA de Bluetooth que enlazar ni
  // que se pueda encender por error. Si algun dia vuelve, se revierte esto.

  // ★★ EL SALUDO DEL ARRANQUE, DESPUES DE LEER LA CONFIGURACION (2026-09-16) ★★
  //   Antes salia ANTES de leer la configuracion, asi que no habia forma de saber si el
  //   puerto lo iba a usar un programa host, y en modo TNC (KISS o TNC2) esas lineas se
  //   le colaban al host. Ahora se sabe, y `diagTrazaArranque()` las calla en ese caso.
  //   Ojo: estas lineas NO dependen del modo diagnostico (que esta apagado al arrancar):
  //   si dependieran, no se verian nunca. El porque, en diag.h.
  if (diagTrazaArranque()) {
    Serial.println();
    Serial.println(APP_NAME_STR " v" APP_VERSION_STR);
    Serial.println("APRS-LoRa digipeater/tracker (433 MHz)");
    Serial.println("WebSerial config protocol v1 (get/set/factory_reset/reboot/status/radio/beacon/telemetry)");
    Serial.println(hayConfig ? "config: loaded from flash" : "config: defaults (none stored)");
  }
  radioSetMuted(gConfig.txDisabled);

  powerInit();              // POFCON 2.2 V
  buttonInit();
  // ★ T-ECHO PROJECT BUTTER (2026-09-15): las dos piezas que hacen que el toque "se
  //   note" y que no se pierda nada mientras el panel pinta.
  //   1) Aviso INSTANTANEO en el flanco de pulsacion (luz + pitido), sin esperar a que
  //      la maquina decida si el gesto es corto, largo o doble.
  //   2) El driver de la tinta llama a `bombeaEsperaPantalla()` en sus esperas: la maquina
  //      de gestos sigue corriendo durante el repintado y deja el gesto encolado para que
  //      el bucle lo ejecute en cuanto el panel quede libre. (Desde el 2026-09-16 ese mismo
  //      gancho LEE tambien el USB y lo encola: ver el bloque de arriba.)
  buttonSetFeedback(feedbackPulsacion);
  // ★ T-ECHO PROJECT BUTTER II (2026-09-16): el mismo gancho bombea AHORA TAMBIEN EL USB.
  //   Sigue sin ejecutar nada: el driver solo LEE Y ENCOLA (ver bombeaEsperaPantalla).
  displaySetPumpBoton(bombeaEsperaPantalla);
  sensorsInit(gSensorCache);  // Wire.begin + INA219/AHT20/BMP280 + divider P0.31
  displayInit();              // e-paper T-Echo (before boot popup)

  // Radio first: if the boot check decides to sleep, the SX1262 must already be
  // initialised so it receives the SPI sleep command (NavaTastic lesson:
  // sleeping before radio init left the SX1262 listening at 5-10 mA).
  if (radioSetup(gConfig)) {
    if (diagTrazaArranque()) {
      Serial.print("radio: OK @");
      Serial.print(gConfig.frequencyHz);
      Serial.println(" Hz (SF12/BW125/CR4/5)");
    }
    radioSetRxCallback(aprsHandleRadioPacket);
  } else {
    if (diagTrazaArranque()) {
      Serial.print("radio: init FAILED (err ");
      Serial.print(radioLastErr());
      Serial.println(")");
    }
  }

  // ★ SEGUNDA PARTE DE LA PANTALLA, Y AQUI ES DONDE TIENE QUE IR (2026-09-15).
  //
  // En este core el objeto global `SPI` de Arduino vive en SPIM3, el MISMO periferico que
  // usa la pantalla de tinta electronica del T-Echo. radioSetup() llama a SPI.begin(), que
  // reconfigura ese periferico con los pines de la radio. Si la pantalla se configura antes
  // (como estaba), el arranque de la radio le quita el periferico y la pantalla se queda sin
  // recibir un solo byte: su pin BUSY no se mueve nunca y parece "muerta" sin estarlo.
  // Se comprobo leyendo los registros PSEL: al configurarla, P0.31/P0.29; despues de la
  // radio, P0.19/P0.22. Por eso la parte del panel va DESPUES.
  displayInitTrasRadio();

  powerBootCheck(gConfig);    // sleeps itself when the battery is too low
  trackerInit();              // GPS off until needed

  flogInit();
  flogSetEnabled(gConfig.mode != 0);
  // El numero de compilacion va en la linea de arranque del registro: asi, mirando un
// volcado, se sabe EXACTAMENTE que firmware escribio esas lineas.
flogLine("EVT boot v%s %s mode=%u", APP_VERSION_STR, APP_BUILD_NUM,
           (unsigned)gConfig.mode);

  // ★★ BLUETOOTH: EL SEGUNDO PUERTO SERIE DEL NODO (2026-09-17) ★★
  //
  // POR QUE AQUI, AL FINAL: el aviso del PIN de emparejamiento se pinta en la PANTALLA, asi
  // que la pantalla tiene que estar lista antes; y cada suceso del enlace deja una linea
  // "BLE ..." en el registro de viaje, asi que el registro tambien. Es el mismo sitio en el
  // que arrancaba antes de apagarse (2026-09-13).
  //
  // QUE HACE Y QUE NO:
  //   - NO se arranca nada si `cfg.bleEnabled` esta apagado: ni se toca el SoftDevice. El
  //     ajuste dejaba de ser inerte aqui (antes se guardaba y no lo leia nadie).
  //   - Si arranca, el nodo se anuncia como "Kacho APRS <indicativo>" y el huesped que se
  //     conecte habla EXACTAMENTE el mismo protocolo que por el cable (JSON + CLI), porque
  //     sus bytes entran en el mismo receptor de lineas.
  //   - El Bluetooth NO toca la radio ni el modo del TNC: encenderlo no silencia las balizas.
  //
  // LA HISTORIA, EN UNA LINEA (el porque de las dudas al leer esto): el Bluetooth se apago
  // porque el nodo se quedaba bloqueado. ★ CORREGIDO EL 2026-09-17: la causa NO era el codigo
  // de Bluetooth ni un chip averiado — la sonda leia la ficha del SoftDevice 0x1000 bytes mas
  // abajo (`0x200C` en vez de `0x300C`), y lo que hay ahi es CODIGO del SoftDevice. El `fwid`
  // real de esta placa es `0x0100` y la placa tiene un S140 7.2.0 valido. Lo que sigue abierto
  // es POR QUE el arranque del stack se cuelga (el nodo reinicia en bucle): la hipotesis
  // principal es esta, la prioridad de una interrupcion ya configurada (`0x1001`), y por eso
  // el arranque se ha subido AL PRINCIPIO del setup (ver el bloque de arriba, junto a
  // `bleBindConfig`). El detalle completo, en `docs\SESION_20260917_BLUETOOTH.md` y en
  // `docs\INFORME_BLUETOOTH_ARRANQUE.md`.
  //
  // Y el arranque del Bluetooth NO se hace aqui: se hace al principio (ver arriba).

  displaySplash();  // 4 s boot splash (non-blocking)

  // ★ AVISO DE ARRANQUE por vibracion (2026-09-15). Va AQUI, al final del setup, y no
  //   cuando la pantalla esta lista: la pantalla de tinta tarda ~17 s (6 s de espera + dos
  //   refrescos completos), y el operador quiere saber YA que el nodo ha arrancado -- sobre
  //   todo desde que se despierta con el boton de RESET, que no da ninguna otra señal.
  hapticAviso(HAP_ARRANQUE);
}

void loop() {
  static uint32_t lastBeaconMs = 0;
  static uint32_t lastSensorsMs = 0;
  static uint8_t lastLogMode = 255;
  static bool pantallaArrancada = false;

  // ★ EL ARRANQUE DE LA PANTALLA DE TINTA ELECTRONICA, AQUI Y NO EN setup() (2026-09-15).
  //   Se hace a los 6 s de estar andando, cuando el USB ya esta enumerado y el nodo
  //   obedece. Si el panel se bloquea, el nodo sigue vivo y se puede diagnosticar (y volver
  //   a grabar por software) en vez de quedarse mudo. Ver displayArrancaPantalla().
  //   Desde el 2026-09-14 esta ACTIVADO: el driver ya no puede quedarse esperando para
  //   siempre (sus transferencias SPI llevan tope de tiempo y se abortan solas).
  if (!pantallaArrancada && millis() > 6000UL) {   // 6 s: el USB ya esta vivo de sobra
    pantallaArrancada = true;
    displayArrancaPantalla();
  }

  usbDiagTick(millis());   // latido del USB (diagnostico, ver protocol.h)
  gProtocol.feed(Serial);   // LEER EL USB: sin esta linea el nodo no obedece a nada
  drainButton();            // y drenar el boton despues

  // The trip log follows the working mode (on in tracker/both, off in digi).
  if (gConfig.mode != lastLogMode) {
    lastLogMode = gConfig.mode;
    flogSetEnabled(gConfig.mode != 0);
    flogLine("EVT mode=%u log=%s", (unsigned)gConfig.mode,
             gConfig.mode ? "on" : "off");
  }

  uint32_t now = millis();

  // ★ Aviso por VIBRACION al coger fijacion GPS (2026-09-15, idea del operador).
  //   No hace nada si el fix no ha cambiado desde la vuelta anterior, asi que se puede
  //   llamar siempre. Lleva dentro las dos guardas (solo al cogerla, y como mucho una vez
  //   por minuto): ver hapticTickGPS() en haptic.h.
  hapticTickGPS(gpsGet().fix);
  if (now - lastSensorsMs >= 2000) {
    lastSensorsMs = now;
    sensorsRead(gSensorCache);
    drainButton();   // I2C: puede tardar si un sensor no responde
    // Keep looking for an external probe while none is present (a module may
    // need time after power-up, or be connected later). Once found, its
    // readings take over automatically and the chip sensor stays as TINT.
    if (!sensorsHasExternal(gSensorCache)) {
      static uint32_t lastDetectMs = 0;
      if (lastDetectMs == 0 || now - lastDetectMs >= 60000u) {
        lastDetectMs = now;
        sensorsRetryDetect(gSensorCache);
      }
    }
    powerLoop(gConfig);  // anti-brownout monitor (2 s cadence, fix #1)
    drainButton();   // el powerLoop puede tardar leyendo la bateria
  }

  uint32_t intervalMs = (uint32_t)gConfig.beaconIntervalMin * 60000u;

  // GPS management: tracker modes use the tracker loop; in digipeater mode the
  // GPS is used only when "GPS en repetidor" is enabled (overrides fixed coords).
  // The tracker loop is an AUTOMATIC TRANSMITTER (first-fix, rate, distance,
  // corner and parked beacons), so while the host drives the node only the GPS
  // bookkeeping runs: in tracker modes gpsManage() takes that job (it is the
  // same call the digipeater makes). Hardware log, 2026-09-13: this call used to
  // sit outside the gate and the node still sent "TX TRK ... ok F" in KISS mode.
  if (gConfig.mode != 0 && !tncHostDriven()) {
    trackerLoop(gConfig, now);
  drainButton();   // el tracker puede TRANSMITIR aqui: cientos de ms sin mirar el boton
  } else {
    uint32_t elapsed = now - lastBeaconMs;
    uint32_t remain = (intervalMs > elapsed) ? (intervalMs - elapsed) : 0;
    gpsManage(gConfig, now, remain);
  }

  // Sesion "Fijar coordenadas actuales" del menu: avanza si esta en marcha
  // (enciende el GPS, espera fijacion, deja que se asiente, guarda y apaga).
  // Ver trackerSetCoordsStart()/Tick() en tracker.h.
  // ★ EL GPS LO GOBIERNA LA SESION, NO EL MODO (2026-09-15): por eso esto se llama
  //   siempre, tambien en modo repetidor, y es gpsManage() el que atiende a la
  //   sesion POR DELANTE del modo. Antes, en repetidor, no servia de nada salvo
  //   que estuviera activo "GPS en repetidor".
  trackerSetCoordsTick(now);
  if (trackerSetCoordsEstado() == TRK_COORDS_ASENTANDO) {
    // Progreso en pantalla: sin esto, tres minutos mirando una pantalla quieta
    // parecen un cuelgue. La OLED lo enseña tal cual; la tinta tiene su PROPIA
    // pantalla de sesion (con barra) y descarta este aviso a proposito, para no
    // repintar 20 veces un panel que tarda 1,5 s (ver displayPopup en
    // epaper_techo.cpp). El aviso se sigue mandando igual porque es tambien la
    // linea de estado por USB.
    // ★ El "ultimo valor enseñado" vive FUERA del bucle y se limpia al empezar la
    //   sesion (2026-09-15): antes era un static de dentro del `if` y se quedaba
    //   con el ultimo numero de la captura ANTERIOR, asi que si la nueva empezaba
    //   por el mismo numero no se veia el primer aviso.
    if (trackerSetCoordsDone() != gSetCoordsShown) {
      gSetCoordsShown = trackerSetCoordsDone();
      char b[32];
      snprintf(b, sizeof(b), "GPS %u/%u", (unsigned)gSetCoordsShown,
               (unsigned)trackerSetCoordsNeed());
      displayPopup(b);
    }
  } else if (gSetCoordsShown != 255) {
    gSetCoordsShown = 255;   // sesion terminada (o sin empezar): a cero para la proxima
  }

  // ===================== KISS mode: no automatic packets =====================
  // (Operator decision, see tncHostDriven() at the top of this file.) Everything
  // below, down to the matching comment at the end, is a packet the NODE sends
  // on its own: tracker beacons, the boot and periodic position beacon, the
  // status notices, the battery telemetry and the weather report. While the KISS
  // protocol is on, none of it may go out: the host app is the one that commands,
  // and a node beaconing on its own would publish a position the operator did not
  // ask for. Reception-driven work (digipeating in aprsHandleRadioPacket, the USB
  // KISS path in tnc.cpp) is deliberately OUTSIDE this gate, as are the CLI, the
  // web panel and the OLED menu, which are the operator asking for one packet.
  if (!tncHostDriven()) {

  // Baliza de arranque. CAMBIO DE COMPORTAMIENTO (operador, 2026-09-13):
  //   - modo repetidor (0): igual que siempre. Anuncia su posicion configurada
  //     poco despues de arrancar y luego cada intervalMs. Un repetidor fijo
  //     tiene que decir donde esta: esa posicion es su razon de ser.
  //   - modo rastreador (1) y dual (2): NUNCA se publica una posicion que no sea
  //     real. Sin fijacion no sale posicion: ni la fija configurada, ni una
  //     "ultima oportunidad" a los 10 minutos. Sale solo el aviso "En marcha,
  //     buscando satelites" (para que se sepa que el nodo esta vivo) y, en cuanto
  //     el GPS fija, el rastreador empieza a mandar posiciones de verdad.
  //     Si el operador quiere mandar la ultima posicion conocida a mano, tiene el
  //     doble toque del boton (ver handleButton).
  // NOTE: the first one waits a few seconds on purpose. Transmitting blocks the
  // loop for several seconds (listen-before-talk + SF12 airtime) and would eat
  // the 4 s boot splash, leaving a black screen.
  constexpr uint32_t kBootBeaconDelayMs = 7000;
  const bool trackerMode = (gConfig.mode != 0);
  bool trackerOwnsPosition = trackerMode && gpsPowered() && gpsGet().fix;
  static bool bootBeaconDone = false;
  // Aviso de estado "GPS OK" en modo rastreador: una sola vez por arranque (lo
  // enciende el bloque de abajo, en cuanto hay fix). Vive aqui, al lado del
  // one-shot del arranque, y en modo 0 nunca se toca.
  static bool gpsOkStatusSent = false;
  if (radioReady() && !gConfig.txDisabled && !trackerOwnsPosition &&
      intervalMs >= 600000u) {
    const bool bootDue = !bootBeaconDone && now >= kBootBeaconDelayMs;
    const bool periodicDue =
        !trackerMode && bootBeaconDone && now - lastBeaconMs >= intervalMs;
    if (bootDue || periodicDue) {
      bootBeaconDone = true;
      lastBeaconMs = now;
      bool sent = false;
      int16_t st = RADIOLIB_ERR_NONE;
      if (trackerMode) {
        // Sin fijacion (con fijacion este bloque ni se toca): NUNCA se adivina
        // una posicion. Solo se avisa de que el nodo esta en marcha y buscando.
        if (!gpsOkStatusSent && aprsCanBeacon(gConfig)) {
          st = aprsSendStatus(gConfig, "En marcha, buscando satelites");
          sent = true;
        }
      } else if (gConfig.mode == 0 && gConfig.gpsInDigi && gpsPowered() &&
                 gpsGet().fix) {
        st = aprsSendTrackerBeacon(gConfig, gpsGet());  // GPS overrides fixed
        sent = true;
      } else if (aprsCanBeacon(gConfig)) {
        st = aprsSendBeacon(gConfig);
        sent = true;
      }
      if (sent && !tncActive()) {
        Serial.print(F("{\"radio\":\"beacon\",\"auto\":true,\"tx\":"));
        Serial.print(st == RADIOLIB_ERR_NONE ? "true" : "false");
        Serial.print(F(",\"code\":"));
        Serial.print(st);
        Serial.println(F("}"));
      }
    }
  }

  // Aviso "GPS OK" del modo rastreador (1/2), UNA sola vez por arranque: sale
  // cuando ya hay fix al arrancar (entonces el bloque de arriba ni se toca) y,
  // si no lo habia, en cuanto el GPS fija. Motivo: los clientes APRS conservan
  // el ultimo estado recibido, asi que sin este "GPS OK" el aviso "En marcha,
  // buscando satelites" se quedaba horas en el mapa aunque la posicion ya fuese
  // correcta. Mismos guardas que el aviso de arranque (radio lista, sin mute,
  // aprsCanBeacon) y nunca se repite: con el fix en la mano no vuelve a salir.
  if (trackerMode && !gpsOkStatusSent && radioReady() && !gConfig.txDisabled &&
      gpsPowered() && gpsGet().fix && aprsCanBeacon(gConfig)) {
    gpsOkStatusSent = true;  // one shot: no se repite mientras se mantenga el fix
    aprsSendStatus(gConfig, "GPS OK");
  }

  // Periodic telemetry, the APRS way: channels + metadata every N minutes
  // (metadata every 10th sequence inside aprsSendTelemetry). The first one also
  // waits: transmitting blocks the loop for seconds and would swallow the boot
  // splash and the startup beacon.
  constexpr uint32_t kFirstTelemDelayMs = 13000;
  static uint32_t lastTelemMs = 0;
  if (gConfig.sendBatteryTelemetry && gConfig.telemetryIntervalMin > 0 &&
      radioReady() && !gConfig.txDisabled && now >= kFirstTelemDelayMs) {
    const uint32_t telemMs = (uint32_t)gConfig.telemetryIntervalMin * 60000u;
    if (lastTelemMs == 0 || now - lastTelemMs >= telemMs) {
      lastTelemMs = now;
      aprsSendTelemetry(gConfig);
    }
  }

  // Weather packet of its own: every wxIntervalMin minutes once the GPS provides the
  // stamp (retries every minute until then). This is what makes the station appear as
  // a weather station in aprs.fi / findu.
  // ★ ANTES IBA FIJO A 15 MINUTOS (900000 ms) escrito aqui dentro (2026-09-15): en un
  //   repetidor fijo eso es aire tirado. Ahora manda el ajuste del operador
  //   (wxIntervalMin, por defecto 55) y hay TRES intervalos desincronizados: baliza 30,
  //   telemetria 53 y meteo 55, sin factores comunes, para que no coincidan.
  //   0 = NO automatico: no se programa ningun envio y el paquete solo sale a mano
  //   (el comando `wx` del CLI). El reintento de 60 s de abajo NO cambia: sigue igual
  //   mientras el envio falle (todavia no hay sello de hora del GPS).
  static uint32_t nextWxMs = 0;
  if (gConfig.wxSensorActive && gConfig.wxIntervalMin > 0 && radioReady() &&
      !gConfig.txDisabled && (int32_t)(now - nextWxMs) >= 0) {
    int16_t wxSt = aprsSendWeather(gConfig);
    nextWxMs = now + ((wxSt == RADIOLIB_ERR_NONE)
                          ? (uint32_t)gConfig.wxIntervalMin * 60000u
                          : 60000u);
  }

  // Status packet: once at boot, then every 24 h (CA2RXU behaviour).
  static uint32_t lastStatusMs = 0;
  if (gConfig.status[0] != '\0' && radioReady() && !gConfig.txDisabled &&
      (lastStatusMs == 0 || now - lastStatusMs >= 86400000u)) {
    lastStatusMs = now;
    aprsSendStatus(gConfig);
  }

  }  // ============ end of the "no automatic packets in KISS" gate ============

  radioLoop();
  diagLoop();
  // ★★ EL BOMBEO DEL BLUETOOTH (2026-09-17) ★★
  //   Aqui y no antes: `bleLoop()` empuja al receptor de lineas lo que haya escrito el
  //   huesped, y esa misma vuelta del bucle (un poco mas abajo, en `gProtocol.atiende()`) es
  //   la que lo ejecuta. Se hace DESPUES de `displayRefresh()` a proposito: si el panel acaba
  //   de tardar 2 s pintando, esto es lo primero que se cobra en cuanto el panel queda libre.
  //   No bloquea: saca como mucho un aviso por vuelta.
  //   Y se cobra EN EL ACTO lo que el huesped haya escrito: `atiende()` es el unico sitio que
  //   ejecuta un comando. Sin esta linea, una orden por Bluetooth se quedaria en el anillo
  //   hasta la vuelta siguiente (y en modo KISS, que salta el `atiende()` del final del
  //   bucle, no se cobraria nunca: la app se quedaria muda por el aire).
  gProtocol.atiende();
  // Resending an unacknowledged message is a packet the node decides to send on
  // its own, so the same KISS rule applies (see tncHostDriven()).
  if (!tncHostDriven()) aprsMsgTick(gConfig, now);
  handleButton();
  drainButton();   // la pantalla se dibuja por I2C: unos ms sin mirar el boton
  displayRefresh(gConfig, radioRxCount(), radioTxCount(), aprsDigiCount(),
                 gSensorCache);
  // ★ FIN DEL REPINTADO: se quita la marca del driver (ver bombeaEsperaPantalla). A partir
  // de aqui ya se puede ejecutar: el panel esta libre y sus transacciones han terminado.
  gProtocol.marcaEnPantalla(false);
  // ★★ T-ECHO PROJECT BUTTER (2026-09-15) ★★
  // Aqui es donde se cobra el toque que llego DURANTE el repintado. El driver de la
  // tinta ha llamado a bombearBoton() dentro de sus esperas (1,5-3 s), asi que el
  // gesto ya esta resuelto y encolado: esta llamada lo ejecuta en cuanto el panel
  // queda libre, sin esperar otra vuelta entera del bucle. Es exactamente el
  // "que un toque durante el repintado no se pierda, sino que se atienda despues".
  handleButton();
  // ★★ T-ECHO PROJECT BUTTER II (2026-09-16): y aqui se cobra lo que llego POR EL USB
  // durante ese mismo repintado, con el mismo criterio que el gesto de arriba. El driver
  // ha ido LEYENDO el puerto y ENCOLANDO los bytes en sus esperas (bombeaEsperaPantalla),
  // asi que el comando ya esta entero en el nodo: esta llamada lo EJECUTA en cuanto el
  // panel queda libre, sin esperar a la vuelta siguiente del bucle. La ejecucion sigue
  // siendo cosa del bucle (nunca del driver): ni un comando se obedece dentro del pintado.
  gProtocol.atiende();
  // Retroiluminacion de la tinta (T-Echo): la enciende el boton y esto la apaga sola
  // cuando vencen los 5 s.
  displayBacklightTick(millis());
  delay(5);
}
