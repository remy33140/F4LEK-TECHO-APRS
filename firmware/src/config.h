// config.h — digipeater configuration (schema v1)
// Fields follow the CA2RXU nRF52 digi catalog + ESP32 superset subset (N-12).
// Single source of truth for the WebSerial config protocol.
// License: GPL-3.0

#pragma once

#include <ArduinoJson.h>

// Nombre del firmware: lo pone platformio.ini (-DAPP_NAME_STR). Valor de socorro por
// si alguien compila a mano.
#ifndef APP_NAME_STR
#define APP_NAME_STR "F4LEK - T ECHO APRS"
#endif

// LoRa-APRS 433 MHz defaults (EU, CA2RXU ecosystem)
#define APRS_LORA_DEFAULT_FREQ_HZ 433775000UL

// USB TNC bridge protocol (cfg.tncProtocol). Three positions on purpose:
//   0 = off, 1 = TNC2 text (APRSdroid "TNC-2"), 2 = KISS binary (APRSdroid KISS,
//   APRSIS32, LoRa APRS App). It replaced the boolean cfg.tncMode: a saved
//   "tncMode: true" migrates to 1 (TNC2 as it was), never to 2, so an old
//   configuration cannot switch a node to binary KISS on its own.
enum {
  CFG_TNC_OFF = 0,
  CFG_TNC_TNC2 = 1,
  CFG_TNC_KISS = 2,
};

// Max/default TX (N-02): the T-Echo SX1262 has no external PA, 22 dBm.
#define CFG_MAX_TX_POWER_DBM 22
#define CFG_DEFAULT_TX_POWER_DBM 22

struct DigiConfig {
  // station
  char callsign[16] = "NOCALL-11";   // uppercase
  // Identificador de dispositivo (el "tocall": es el destino AX.25 de TODAS nuestras
  // tramas). Es lo que aprs.fi enseña en la casilla "Dispositivo".
  //
  // ★★★ EL TOCALL NO ES UN AJUSTE: ES UNA DECLARACION DEL FIRMWARE (2026-09-21) ★★★
  //
  // QUE ES: el destino AX.25 de TODAS las tramas del nodo. Es lo que aprs.fi y los mapas
  // enseñan en la casilla "Dispositivo": identifica al FIRMWARE, no al dueño del aparato.
  //
  // ★ NO ES MODIFICABLE, Y ESO ES A PROPOSITO (peticion del operador, EA2OY):
  //   - Hay UNA SOLA declaracion, aqui abajo (kKachoSystemTocall). Todo lo demas la lee.
  //   - NO existe ya un campo `tocall` en DigiConfig, asi que no hay NADA que guardar en
  //     la flash, ni NADA que el usuario pueda tocar.
  //   - Se ignora si llega por el cable (`set tocall ...`), por el configurador web, por
  //     la app o por cualquiera de los dos menus del aparato. El nodo manda SIEMPRE esta
  //     matricula, tenga lo que tenga guardado de antes.
  //   - Consecuencia buscada: **al grabar este firmware, el nodo queda con nuestra
  //     matricula**, aunque venga de otro firmware o de una version anterior.
  //
  // POR QUE APL: el mapa que se usa en el norte de España (lora.ham-radio-op.net, que es
  // APRS Track Direct) solo pinta estaciones cuyo tocall empieza por APL. Comprobado el
  // 2026-09-21 consultando APRS-Ish desde el servidor español: en 150 km alrededor de
  // Pamplona, todas las estaciones LoRa que ese mapa muestra llevan APL* (APLRG1, APLRT1,
  // APLOX1, APLG01, APLRFD), y las de otros ecosistemas no salen.
  // POR QUE "2OY": es el indicativo del autor dentro de la matricula; estaba libre (de las
  // 42 entradas APL* de la base oficial, ninguna empieza por APL2). Descartadas: APZFKT (la
  // F libre hoy, mas facil de chocar mañana) y APLETK (esa YA ES de DL5TKL, el firmware del
  // T-Echo del que copiamos la secuencia de pantalla).
  // ANTES PONIA APLRG1, Y ESTABA MAL: es la matricula de OTRO firmware (el ecosistema de
  // Ricardo, CA2RXU), asi que nuestros nodos firmaban como si fueran suyos.
  //
  // UNA SOLA MATRICULA PARA TODO EL FIRMWARE, con todos sus montajes (T-Echo, Plus,
  // Faketec, HT-RA62, E22P): lo manda el documento oficial de asignacion de
  // aprs-deviceid ("do not request multiple device identifiers; use different symbols to
  // identify the role of each station"). El papel de cada nodo se distingue con el SIMBOLO.
  //
  // PENDIENTE: pedir la asignacion oficial en github.com/aprsorg/aprs-deviceid
  // (class: tracker o network, os: embedded, vendor: EA2OY).
  //
  // SI ALGUN DIA HAY QUE CAMBIARLO: se cambia ESTA linea, se recompila y se graba. No hay
  // ninguna via por software, y es a proposito: asi no puede haber dos nodos del mismo
  // firmware diciendo cosas distintas.
  static constexpr const char *kKachoSystemTocall = "APL2OY";
  // Ruta de respaldo (compatibilidad). Las buenas son las tres de abajo.
  char path[32] = "WIDE1-1";
  // Ruta (saltos) POR MODO DE TRABAJO: se puede pedir distinto en cada uno y
  // cada modo usa la suya. "0" = sin saltos (no pido que me repita nadie).
  // Valores recomendados para la red: 1 salto en repetidor (estación fija) y 2
  // saltos en rastreador y en ambos (por si el iGate está lejos y solo lo oye
  // otro repetidor). Si una de las tres está vacía se usa "path" como respaldo.
  char pathDigi[32] = "WIDE1-1";            // modo 0
  char pathTracker[32] = "WIDE1-1,WIDE2-1"; // modo 1
  char pathBoth[32] = "WIDE1-1,WIDE2-1";    // modo 2
  char comment[64] = "";
  char status[64] = "";
  char msgText[48] = "";             // quick APRS message (menu / web)
  int msgRetries = 3;                // 0..5 resends while an ack is missing
  // Asistente SOTA: las 4 ultimas asociaciones usadas, la mas reciente primero,
  // separadas por comas ("F,EA2,EA1"). Van por CODIGO, no por indice, para que
  // regenerar sota_regions_data.h no las descoloque. Lo escribe el asistente al
  // guardar un plan de activacion (ver sotaGuardarPlan en epaper_techo.cpp).
  char sotaRecent[20] = "";
  // Asistente SOTA: la ultima cumbre spoteada y su prefijo de indicativo,
  // "F/PE-103,EA2" ("F/PE-103," sin prefijo). Alimenta la fila "Re ..." (primera
  // fila del asistente). Lo escribe el asistente al guardar un plan.
  char sotaLast[24] = "";
  // Plan de activacion SOTA (menu SOTA -> "Plan activation", 2026-09-27):
  // "F/PE-103,EA2,145500,SSB" (cumbre, prefijo o vacio, 6 digitos de
  // frecuencia, modo). Lo usan "SOTA spot" y la escena SOTA del carrusel.
  char sotaPlan[40] = "";
  char symbol = '#';                 // 1 char APRS symbol
  // Symbol table identifier / overlay: "/" = primary table, "\" = alternate
  // table, "0".."9" or "A".."Z" = alternate table with that overlay character
  // (APRS101 chapter 20). It goes right before the symbol code.
  char overlay[2] = "/";
  // ★ AMBIGUEDAD DE POSICION (0..4): digitos que se borran (con espacios) de los
  //   minutos y de los decimales. 4 es el MAXIMO que sigue dando una trama legal:
  //   el PUNTO DECIMAL NUNCA se borra (APRS101 exige campo de longitud fija).
  //   Ver encodePosition() en aprs.cpp y la validacion en config.cpp.
  int posAmbiguity = 0;              // 0..4 digits hidden (position privacy)

  // beacon (fixed position, no GPS on digi)
  // ★ 30 MINUTOS POR DEFECTO (2026-09-15, peticion del operador): en un repetidor
  //   FIJO la baliza de posicion no aporta nada nuevo y gasta aire. Los tres envios
  //   automaticos van DESINCRONIZADOS a proposito: baliza 30 / telemetria 53 / meteo 55.
  //   Los tres numeros NO comparten factores (30 = 2x3x5, 53 es primo, 55 = 5x11), asi
  //   que casi nunca coinciden en el mismo minuto y no se juntan en rafagas de paquetes.
  int beaconIntervalMin = 30;        // >= 15
  float latitude = 0.0f;             // -90..90
  float longitude = 0.0f;            // -180..180
  bool compressedPos = false;        // tracker beacon in compressed APRS form

  // digipeater
  uint8_t digiMode = 2;              // 0=OFF, 1=WIDE1-1, 2=WIDE1-1+WIDE2-n
  char blacklist[64] = "";           // space separated, '*' wildcard

  // LoRa radio (single shared profile in v1; RX/TX split = future)
  uint32_t frequencyHz = APRS_LORA_DEFAULT_FREQ_HZ; // 430e6..928e6
  uint8_t spreadingFactor = 12;      // 5..12 (SX1262)
  uint8_t codingRate4 = 5;           // 5..8 (CR 4/5..4/8)
  float signalBandwidthKhz = 125.0f; // 62.5/125/250/500
  uint8_t powerDbm = CFG_DEFAULT_TX_POWER_DBM; // clamped by radio
  bool cadActive = true;             // listen before talk

  // telemetry / sensors
  // ★ TELEMETRIA Y METEO: POR DEFECTO SI (2026-09-15, peticion del operador).
  //   Si el aparato tiene sonda de clima, lo logico es que la aproveche sin que nadie
  //   tenga que activarla; el que no la quiera la apaga en el menu o en el configurador.
  bool sendBatteryTelemetry = true; // voltage as APRS Base91 telemetry
  bool wxSensorActive = true;       // BME/BMP/AHT on I2C
  // Cada cuanto se manda la telemetria. 0 = solo a mano; si no, 15..720 minutos.
  // ★ 15 ES EL MINIMO A PROPOSITO (2026-09-15): con 10 minutos la red se satura, y 15 es
  //   lo que recomienda la comunidad. El maximo son 12 horas (720). Los pasos van de 15 en
  //   15 para que el menu no tenga medio millar de valores.
  // ★ 53 MINUTOS POR DEFECTO (2026-09-15): primo con el 30 de la baliza y el 55 de la
  //   meteo, para que los tres envios automaticos no coincidan (ver beaconIntervalMin).
  int telemetryIntervalMin = 53;    // 0 = only manual; else every N minutes (15..720)
  // Cada cuanto se manda el paquete METEOROLOGICO propio (el que hace que la estacion
  // salga con graficas en aprs.fi / findu). Misma regla que la telemetria: 0 = solo a
  // mano; si no, 15..720 minutos. ★ 55 MINUTOS POR DEFECTO (2026-09-15): antes iba FIJO
  // a 15 minutos dentro de main.cpp y no se podia cambiar (55 = 5x11, sin factores
  // comunes ni con el 30 ni con el 53, ver beaconIntervalMin).
  int wxIntervalMin = 55;           // 0 = only manual; else every N minutes (15..720)
  int heightCorrectionM = 0;
  float temperatureCorrectionC = 0.0f; // -5..5 (external probe only)
  // Offset applied to the nRF52 internal (chip) sensor: it measures the die, not
  // the air, so it needs a negative correction once measured on the bench.
  float chipTempOffsetC = -3.0f;       // -10..10

  // power / resilience (N-03): battery cut & LPCOMP wake (mV, ADC P0.04)
  // LilyGO T-Echo / T-Echo Plus: celda de litio de 1S (3,7 V). En una LiPo,
  // 3400 mV ya es casi el final util y 3710 mV es mas de media carga. Se toman
  // de la curva que publica el firmware del T-Echo: apagar a 3200 mV (≈3 %) y
  // despertar a 3400 mV. Se pueden cambiar desde el configurador o el menu.
  int sleepCutMv = 3200;
  int sleepWakeMv = 3400;

  // mute / remote control
  bool txDisabled = false;         // global TX mute (beacon+digi+test); RX alive
  bool remoteEnabled = false;      // accept remote commands over RF messages
  // USB TNC bridge: 0 = off, 1 = TNC2 text, 2 = KISS (see CFG_TNC_* above).
  // In KISS the host app drives the node: it does not send its own beacons.
  uint8_t tncProtocol = CFG_TNC_OFF;
  char managers[64] = "";          // space-separated callsigns (ACL)

  // Bluetooth LE (KISS over the Nordic UART Service). Operator decision: ON by
  // default, so a phone finds the node out of the box. It is a SECOND host link,
  // like the USB cable, and it is NOT tied to tncProtocol: with Bluetooth on and
  // the TNC selector off the node keeps beaconing exactly as before and a
  // connected host can still send and receive frames. Only the selector put to
  // KISS (2) silences the node's own packets (tncHostDriven() in main.cpp).
  // 2026-09-13: PUESTO A false DE MOMENTO. El firmware con Bluetooth encendido
  // de fabrica dejo el nodo muerto al arrancar (sin radio, sin pantalla y sin
  // USB). Hasta encontrar la causa, el valor por defecto es APAGADO: asi un
  // nodo recien grabado siempre arranca y se puede investigar con el puerto
  // serie delante. Volvera a true cuando el arranque con Bluetooth este probado.
  bool bleEnabled = false;
  // Pairing PIN, exactly 6 digits (BLE_GAP_PASSKEY_LEN): the phone asks for it
  // when it pairs, the node shows it on the OLED and the operator types it.
  // An unpaired device cannot read or write the KISS characteristics at all
  // (they need an encrypted link with MITM protection).
  char blePin[8] = "123456";

  // UI / mode (Phase A)
  // ★ MODO POR DEFECTO DE FABRICA: 2 = DIGI + TRACKER (decision del operador, 2026-09-15).
  //   Es el que llevara el firmware que se publique en GitHub; el usuario lo cambia luego
  //   desde el menu o desde el configurador web. Antes venia en 0 (solo repetidor).
  //   Y el INDICATIVO va SIN CONFIGURAR a proposito ("NOCALL-11", ver arriba): cada uno
  //   mete el suyo. Mientras no lo ponga, el nodo NO TRANSMITE NADA (norma en aprs.cpp).
  uint8_t mode = 2;                // 0=digipeater, 1=tracker, 2=both
  bool sceneAutoAdvance = true;    // auto-rotate OLED scenes (~5 s)
  int screenTimeoutSecs = 0;       // 0=never off (bench); >0 sleep screen after
  bool popups = true;              // event popups on OLED

  // ★ ORIENTACION DE LA PANTALLA DE TINTA ELECTRONICA.
  // POR QUE ES UN AJUSTE DEL USUARIO Y NO UNA CONSTANTE: el panel puede ir montado con
  // distinta orientacion segun la unidad o el lote, asi que fijar la rotacion en el codigo
  // es fragil. Aqui se elige (configurador web o `set epdRotation N`), el driver la lee de
  // la config y se aplica EN CALIENTE (sin reiniciar).
  //
  // QUE SIGNIFICA CADA NUMERO (confirmado a ojo por el operador):
  //     1 = DE PIE. Es la posicion natural y el valor de fabrica.
  //     2 = girada 90      3 = boca abajo (180)      0 = girada 270
  //
  // ★ OJO, Y ESTO ES DELIBERADO: el numero de la posicion de pie es el 1, no el 0, y NO se
  //   ha renumerado. El motivo es que la configuracion vive en la flash del nodo: cambiar el
  //   significado de los numeros haria que las unidades ya grabadas se pusieran torcidas
  //   solas al actualizar (un 1 guardado pasaria de "de pie" a "girada 90"). Es una
  //   migracion silenciosa. La trampa se evita DICIENDOLO CLARO en la interfaz: en el
  //   configurador la opcion se llama "1 - de pie (por defecto)" y las demas llevan su
  //   giro escrito. Ver docs/INTEGRACION_PANTALLA.md.
  uint8_t epdRotation = 1;         // 1 = de pie (natural, de fabrica)

  // Mensaje libre que se muestra en la pantalla de dormido (p. ej. un numero de telefono o
  // cualquier texto). Se edita en el configurador web; si esta vacio no se muestra.
  char sleepMsg[64] = "";

  // Tracker (Phase C)
  int trackerIntervalSecs = 120;   // fixed interval (smart presets may override)
  // ★ UNIFICADO CON EL BOTON DE RECOMENDADOS DEL CONFIGURADOR (2026-09-21): antes era 0.
  //   POR QUE: el plan de despliegue es que un nodo recien grabado salga YA listo, y el
  //   configurador pone 150 m con el boton de valores recomendados. Si de fabrica fuera 0,
  //   un nodo nuevo no tendria la misma configuracion que uno configurado con el boton, y
  //   habria dos "de fabrica" distintos. Regla: LO DE FABRICA = LO RECOMENDADO.
  //   Con un perfil activo (ver smartBeaconPreset) este valor no se usa: manda el del perfil.
  int trackerMinDistanceM = 150;   // "every X m" trigger (0 = off) / filter
  // Minimum spacing between beacons triggered by distance or corner pegging.
  // Each SF12 frame occupies ~4 s of air, so this protects the channel.
  int trackerMinSpacingSecs = 30;  // 10..120
  // ★ UNIFICADO CON EL BOTON DE RECOMENDADOS (2026-09-21): antes era 0 (ninguno), y asi un
  //   nodo recien grabado NO tenia perfil activo: ni SSID de perfil, ni su icono, ni su
  //   ritmo de baliza. El configurador recomienda 3 (coche) y ahora de fabrica tambien.
  //   El perfil activo es lo que hace que la pantalla, el mapa y el aire digan lo mismo
  //   (ver el comentario de profileSsid/profileSymbol mas abajo).
  uint8_t smartBeaconPreset = 3;   // 0=off(fixed/digi) 1=human 2=bike 3=car
  bool sendAltitude = true;
  bool gpsEco = false;             // GPS duty-cycle when idle (default OFF)
  bool trackerSleep = false;       // timed sleep between beacons
  bool gpsInDigi = false;          // digi mode: use GPS position (override fixed)

  // ★★ PERFILES DE USO (2026-09-15) ★★
  // Cada perfil (0=fijo/digipeater, 1=peaton, 2=bicicleta, 3=coche) aporta:
  //   - SSID propio: cuando el perfil esta ACTIVO, la baliza de RASTREADOR se emite con
  //     el indicativo del nodo + este SSID (p.ej. N0CALL-7). Asi los mapas crean un track
  //     separado por perfil. El perfil 0 (fijo) usa el indicativo del nodo tal cual
  //     (su SSID se ignora: es la identidad del digi/igate). Regla: cada perfil usa un
  //     SSID distinto (1..15), el sistema impide duplicarlo.
  //   - Tiempos (lento/rapido, segundos) y metros: 0 = por defecto de la comunidad.
  uint8_t profileSsid[4] = {0, 7, 8, 5};       // fijo, peaton, bici, coche
  int profileSlowSec[4] = {0, 0, 0, 0};        // 0 = default comunitario
  int profileFastSec[4] = {0, 0, 0, 0};        // 0 = default comunitario
  int profileDistM[4] = {0, 0, 0, 0};          // 0 = default comunitario

  // ★★ ICONO DEL MAPA POR PERFIL DE USO (2026-09-15) ★★
  // Hasta ahora el icono (`symbol` + `overlay`, ver arriba) era UN SOLO ajuste del
  // aparato: al cambiar de perfil cambiaba el SSID pero el icono del mapa no, asi
  // que un nodo en bici seguia saliendo con el icono del digi.
  //   profileSymbol[i] = CODIGO del simbolo APRS (1 caracter, el segundo de los dos
  //                      que forman un icono: p.ej. '#' o 'b').
  //   profileOverlay[i] = TABLA/overlay (1 caracter, el PRIMERO de los dos):
  //                      '/' = tabla primaria, '\' = tabla alternativa,
  //                      '0'..'9' / 'A'..'Z' = tabla alternativa con ese overlay.
  // Son los DOS caracteres que APRS101 cap. 20 exige en el campo de simbolo; se
  // guardan por separado porque el firmware ya tenia partido `symbol`/`overlay`.
  //
  // ORIGEN DE LOS CUATRO CODIGOS POR DEFECTO (comprobados en dos fuentes, 2026-09-15):
  //   - tabla de WA8LMF "APRSsymbolcodes.txt" (la que usa go-aprs y la que cita la
  //     comunidad): /# = Digi, /[ = Jogger, /b = Bike, /> = Car;
  //   - apuntes oficiales de overlays de aprs.org (WB4APR, "SYMBOL OVERLAY and
  //     EXTENSION TABLES in APRS 1.2"): "/> = normal car (side view)",
  //     "\[ = Wall Cloud ... overlays are humans" y "#[ = HUMAN SYMBOL".
  //   - coherencia interna del proyecto: _referencias/LoRa_APRS_iGate_HEAD/include/
  //     map_utils.h ya documenta "tabla + codigo APRS (ej \"/>\" = coche)".
  // Los cuatro van en la TABLA PRIMARIA ('/') a proposito: el icono de coche existe
  // tal cual en ella ('>'), asi que no hay que tocar la tabla/overlay del operador
  // para que un perfil se vea como un coche.
  // ★ PRECEDENCIA (quien manda si el usuario pone uno a mano): ver
  //   aprsProfileSymbol()/_aprsProfileIcon() en aprs.cpp. En una frase: si el
  //   ajuste manual `symbol` NO esta en su valor de fabrica ('#'), MANDA EL USUARIO
  //   y el icono del perfil no se usa; si sigue en '#' (nadie lo ha tocado), manda
  //   el icono del perfil activo. Lo mismo con `overlay` != "/".
  char profileSymbol[4][2] = {"#", "[", "b", ">"};
  char profileOverlay[4][2] = {"/", "/", "/", "/"};

  // APRS extras (Phase E)
  bool queriesEnabled = false;     // answer ?APRS? / ?APRSP? / ?APRSD?
};

// defaults + validation
void configSetDefaults(DigiConfig &cfg);

// deserialize full snapshot; returns false + errMsg on validation error.
// cfg is only modified when the whole input is valid.
bool configFromJson(DigiConfig &cfg, JsonVariantConst in, String &errMsg);

// serialize current config (for read-back / GET)
void configToJson(const DigiConfig &cfg, JsonObject out);
