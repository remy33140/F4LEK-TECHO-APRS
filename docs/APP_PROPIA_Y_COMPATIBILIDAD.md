# App propia y compatibilidad con las apps existentes

**Anotado por petición del operador, 2026-09-12.** Es la visión a medio plazo:
primero terminar el firmware, y después ir a por esto "a saco".

> **Idea del operador**: que el nodo sea **compatible con todas las apps** de APRS
> (APRSdroid, LoRa APRS App, APRSIS32...) **sin perder nuestro protocolo de
> configuración**; y que nuestra app propia, **APRS EA2OY System**, no sea solo un
> configurador, sino que haga **absolutamente todas las funciones, incluido el
> iGate**, por BT/BLE.

---

## 1. Se puede, y así es como se hace

La clave es separar dos cosas que hoy van juntas en `tncMode`:

1. **El entramado de las tramas** (cómo se escriben en el cable): TNC2 (texto) y
   **KISS** (binario AX.25 con entramado SLIP), este último ya por USB (§1.1) y
   por Bluetooth (§1.2).
2. **El canal de configuración** (nuestro CLI + JSON, en texto y por líneas).

Ambos pueden convivir en el mismo puerto porque **son inconfundibles a nivel de
byte**:

| Primer byte que llega | Quién es | Qué hacemos |
|---|---|---|
| `0xC0` (FEND de KISS) | Una app KISS (APRSdroid, LoRa APRS App...) | Lo tratamos como trama KISS: la transmitimos por radio |
| `{` | Nuestro protocolo JSON | Respondemos en JSON (como ahora) |
| Letra / dígito | Nuestro CLI de texto | Respondemos en texto (como ahora) |

Es la misma idea que ya usamos: `protocol.cpp` mira si la línea empieza por `{`
o no, y `tnc.cpp` intercepta antes las tramas TNC2. Solo hay que añadir **una
tercera vía por byte** para KISS.

### Cómo evitar que se pisen
- **Detección automática + bloqueo de sesión**: cuando entra una trama KISS
  válida, el nodo entra en "sesión KISS" y **deja de escribir texto** (así la app
  no ve basura); cuando entra una línea de texto válida, sale de la sesión y
  responde en texto. Se puede configurar como **automático** (recomendado) o
  forzado (solo KISS / solo TNC2 / solo consola), como hace el firmware de
  referencia.
- **Silenciar el cartel de arranque** mientras haya habido tráfico KISS reciente.

### Interfaces (cada una puede tener su modo)
| Interfaz | Estado | Para qué apps |
|---|---|---|
| **USB (CDC)** | TNC2 hecho; **KISS hecho** | APRSdroid (cable OTG) |
| **BLE** | **Escrito pero APAGADO** (corregido 2026-09-13): el servicio GATT NUS, el KISS binario y el emparejamiento con PIN **están escritos** en `ble_kiss.cpp/.h`, pero **fuera de la compilación** (renombrados a `.off`) porque **rompían el nodo**: se quedaba sin pantalla y sin USB. **No se puede decir "hecho" de algo que no está en el binario.** Lo que falta averiguar es por qué el SoftDevice no arranca (ver `Cerebro_Faketec_APRS_Igate_EA2OY/cerebro.md`, entradas 41-43, y `_BLE_DIAG/CAUSA_RAIZ.md`). | LoRa APRS App, futuro APRSdroid con BLE, **nuestra app** |
| **TCP/WiFi** | No aplica (el nRF52840 no tiene WiFi) | — (solo en el nodo ESP32 del ecosistema) |

### 1.1 KISS por USB (hecho, 2026-09-13)

El punto 1 de la hoja de ruta ya está en el firmware: la clave `tncProtocol` tiene
tres posiciones y se elige desde el configurador web, la consola o el menú de la
pantalla:

| Valor | Modo | Qué habla por USB |
|---|---|---|
| 0 | Apagado | Nada: el puerto es solo nuestro (JSON + consola) |
| 1 | TNC2 texto | Las tramas de texto de siempre (`SRC>DST,PATH:info`) |
| 2 | **KISS** | AX.25 binario con entramado KISS |

- **`tncProtocol` sustituye al antiguo `tncMode`** (verdadero/falso). Una
  configuración guardada con `tncMode: true` se convierte en `tncProtocol = 1`
  (TNC2, lo que hacía) y `false` en 0, **nunca en 2**: una config vieja no puede
  poner el nodo en binario sola. La clave vieja sigue valiendo al escribir
  (`set tncMode 1`) y `configToJson()` la sigue publicando como espejo.
- **En KISS manda la aplicación** (decisión del operador): el nodo **no envía sus
  propias balizas**, ni telemetría, ni meteorología, ni estado periódico. Solo
  repite (digipeating) lo que oye y transmite lo que le entrega la app. El modo
  TNC2 no cambia: ahí el nodo sigue con sus balizas como siempre. La puerta está
  en `tncHostDriven()` (`src/main.cpp`) y en el bloque "no automatic packets" del
  bucle principal.
- **El puerto sigue siendo nuestro**: JSON y consola continúan funcionando con
  KISS activo (un byte 0xC0 arranca el entramado KISS; cualquier otro byte es
  texto), así que el operador no puede quedarse fuera de su propio nodo. Las
  respuestas a un comando siguen saliendo en texto: la app KISS debe ignorar lo
  que no empiece por FEND. Además, una trama que la app deje a medias (aplicación
  cerrada a mitad de envío, reinicio) se abandona **al segundo sin recibir bytes**
  (`kissPoll()` en `src/kiss.cpp`): si no, se comería como binario todas las
  líneas de JSON/CLI que vinieran detrás y sí que dejaría el nodo bloqueado.
- **Se puede comprobar desde fuera**: cada trama recibida que sale por USB deja
  una línea `TNC TX <origen>><destino><ruta> <bytes> usb<bytes>` en el registro
  de viaje, y el diagnóstico (`diag on`) publica `"kiss":{"out":n,"bytes":n,
  "lastErr":c}`. Con eso se distingue «el nodo no sacó nada» de «el programa no
  lo vio», sin depender de la app. `node tools/kiss_client.js --raw` enseña
  además los bytes tal cual llegan (FEND = binario KISS, texto = respuesta a un
  comando).
- **Límites de AX.25 que hay que decir en voz alta**: el indicativo son **6
  caracteres** como máximo y el SSID va de **0 a 15**. Si la app manda algo que
  no cabe (`EA2OYLARGO`, `-16`), el nodo **no lo recorta**: lo rechaza y lo anota
  en el diagnóstico. La conversión vive en `src/ax25.cpp` (ida y vuelta entre
  AX.25 y el texto que usa la radio) y el entramado en `src/kiss.cpp`, que no
  sabe nada de USB ni de BLE: por eso el paso 2 (KISS por BLE) reutiliza el
  mismo núcleo sin tocarlo. Ojo con el separador: el `-` de `EA2OY-10` es el SSID,
  **nunca** parte del indicativo (de ahí salió el fallo de la primera versión, que
  rechazaba toda trama con SSID de dos cifras).
- **Herramienta de prueba sin móvil**: `node tools/kiss_client.js --list`,
  `--listen [--raw]`, `--send "EA2OY-7>APZFKT,WIDE1-1:>prueba"` y `--selftest`
  (comprueba codificador, decodificador y los casos de entramado sin abrir el
  puerto).

### 1.2 KISS por Bluetooth (BLE) — ESCRITO, PERO APAGADO (2026-09-13)

> **AVISO, lo primero**: esto **NO está funcionando**. El código está escrito y
> compila, pero **está fuera de la compilación** (`ble_kiss.cpp` y `ble_kiss.h`
> están renombrados a `.off`) porque **rompía el nodo**: al enlazar el Bluetooth,
> el nodo se quedaba **sin pantalla y sin USB**. La causa está acotada al
> **SoftDevice**, que se niega a arrancar, y hay un banco de pruebas montado en
> `_BLE_DIAG/`. Todo lo que sigue describe **cómo está diseñado**, no algo que
> puedas usar hoy. Se deja escrito porque el trabajo es bueno y hay que
> terminarlo, no porque esté listo.

El punto 2 de la hoja de ruta está **escrito** en el firmware. **El Bluetooth es otra
forma de hablar con el nodo, como el cable USB**, no un modo de trabajo: se
traía `bleEnabled` de fábrica puesto (aunque en el firmware de ahora da igual:
nodo transmite**. Lo único que calla al nodo sigue siendo el selector
`tncProtocol = 2` (KISS por USB), con su puerta `tncHostDriven()` intacta.

| Qué | Cómo |
|---|---|
| Servicio | **Nordic UART Service (NUS)**: servicio `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, RX (el móvil escribe) `6E400002-…`, TX (el nodo notifica) `6E400003-…` |
| Nombre anunciado | **`Faketec APRS <indicativo>`** (p. ej. `Faketec APRS EA2OY-7`), en la respuesta de escaneo, para distinguir nodos cuando hay varios cerca. Si el indicativo no cupiera, se anuncia solo el indicativo: nunca un nombre recortado |
| Tramas | KISS binario, el mismo entramado que por USB. Los bytes que llegan por RX entran en el **mismo núcleo KISS** (`src/kiss.cpp`); las tramas que el núcleo completa salen por TX en **notificaciones de 20 bytes como máximo**, con una pequeña pausa entre ellas |
| Núcleo KISS | Cada transporte tiene **su propia instancia** (`KissState`): USB y Bluetooth son dos flujos de bytes independientes y no pueden corromperse el uno al otro. La API global `kissXxx()` sigue siendo la de USB, byte por byte como estaba |
| Conversión de tramas | Compartida con el USB (`tncBuildKissFrame()` / `tncSendHostFrame()` en `src/tnc.cpp`): el texto `SRC>DST,PATH:info` ↔ AX.25 ↔ KISS existe **una sola vez** |

#### Emparejamiento (PIN en la pantalla)

- El nodo puede **mostrar** un PIN (`Bluefruit.Security.setIOCaps(true,false,false)`),
  exige protección *man in the middle* (`setMITM(true)`) y usa un **PIN fijo de 6
  cifras** (`cfg.blePin`, de fábrica `123456`, se cambia en el configurador web
  o en el menú de la pantalla). El teléfono lo pide la primera vez y después
  recuerda el enlace (hay *bonding*).
- **Al detectar un emparejamiento, el PIN sale en el OLED** (pantalla limpia,
  «EMPAREJAR BLUETOOTH» + las 6 cifras grandes + de dónde sale el PIN) y **se
  quita en cuanto el emparejamiento termina**, tanto si sale bien como si falla
  (eventos `BLE_GAP_EVT_PASSKEY_DISPLAY` y `BLE_GAP_EVT_AUTH_STATUS`, enganchados
  con `Bluefruit.setEventCallback()`). También se quita al desconectar y, por
  si acaso, **a los 60 s**: la pantalla no se queda clavada nunca. Si el stack no
  manda el evento de passkey, se enseña el PIN **configurado** (que es el que el
  teléfono está pidiendo) y se anota en el registro cuál de los dos se enseñó.
- **Un aparato sin emparejar NO puede leer ni escribir las características
  KISS**: las dos exigen enlace cifrado con MITM (`SECMODE_ENC_WITH_MITM`), así
  que el stack contesta error de autenticación a cualquier lectura, escritura o
  intento de activar las notificaciones. Sin PIN no se ve ni una trama.
- **Un solo anfitrión a la vez**: el nodo se anuncia para **una** conexión
  periférica (`Bluefruit.begin(1, 0)`). Mientras hay una conectada deja de
  anunciarse, así que un segundo teléfono no encuentra el nodo ni puede
  conectarse.

#### Apagarlo

- **Cuando el Bluetooth vuelva a funcionar**: menú de la pantalla → Bluetooth → apagado (o quitar «Bluetooth encendido»
  en el configurador web, o `set bleEnabled 0` por consola). El cambio **se
  aplica al instante, sin reiniciar**: el anuncio se para y, si había alguien
  conectado, se le desconecta. Al encenderlo otra vez vuelve a anunciarse.
- Con el Bluetooth apagado en la configuración, el nodo **ni siquiera arranca
  la pila Bluetooth** al encender: no se anuncia y no gasta batería en ello (el
  código sigue ocupando su sitio en la memoria, pero no se ejecuta).
- Apagado **no** es lo mismo que el modo KISS por USB: el nodo sigue igual
  (balizas, telemetría, repetidor) en los dos casos.

#### Comprobarlo desde fuera, sin móvil

- Cada evento deja una línea **`BLE ...`** en el registro de viaje y en el puerto
  serie (arranque, parada, conexión, desconexión, petición de emparejamiento,
  resultado, cambio de PIN) y cada trama una línea `BLE in ...` / `BLE out ...`.
  Mientras el puente TNC está activo esas líneas no salen por USB, para no
  ensuciar el flujo KISS.
- El diagnóstico (`diag on`) publica `"ble":{"on","state","adv","conn","ready",
  "name","in","out","bytes","drop","pairReq","pairOk","pairFail","pairStatus"}`:
  con eso se distingue «el nodo no sacó nada» de «el móvil no lo vio».
- **Herramienta de prueba**: `node tools/ble_kiss_server.js` sirve
  `tools/ble_kiss.html` en `http://localhost:8099/` y esa página habla Web
  Bluetooth (Chrome/Edge; hay que pulsar «Conectar», el navegador no deja
  hacerlo solo). Todo lo que llega y lo que se envía queda en `logs/`.
- **Coste**: el Bluetooth añade código y memoria (la pila BLE y sus búferes). En
  la compilación de referencia del 2026-09-14 el firmware pasó de **351.416 a
  434.360 bytes de flash** (43,1 % → 53,3 %) y de **13.716 a 22.120 bytes de RAM**
  (5,5 % → 8,9 %), con 223,8 KB libres todavía en la zona de la aplicación y la
  región del registro de viaje intacta. Las cifras exactas de cada versión salen
  en el informe de la compilación.
- **Detalle interno**: con el Bluetooth encendido la pila BLE también reserva el
  termómetro del chip (TEMP, reservado por el SoftDevice), así que la temperatura
  interior se pide a la pila en vez de leer el registro a pelo; el valor es el
  mismo. Con el Bluetooth apagado se sigue leyendo como siempre.

## 2. Nuestra app APRS EA2OY System (visión)

No un configurador: **el mando completo del nodo**, por BT/BLE (y USB cuando
interese). Debe poder hacer:

- **Configuración** completa (todo lo del configurador web, con las ayudas y los
  valores recomendados).
- **Consola** con nuestro CLI y nuestro JSON (status, get/set, diagnóstico).
- **Mensajería APRS** con acuses, boletines, objetos y consultas.
- **Monitorización**: mapa de estaciones oídas, telemetría, meteorología,
  registro de viaje (y exportación GPX/KML desde el propio móvil).
- **iGate completo por el móvil**: el móvil pone Internet (WiFi/4G) y **la app
  guarda las credenciales de APRS-IS**, de modo que:
  - lo que el nodo oye por radio sube a APRS-IS, y
  - los mensajes y objetos que llegan de APRS-IS bajan a la radio.
  - El **passcode lo calcula la app a partir del indicativo** (algoritmo público,
    como hacen Dire Wolf o APRSdroid) o se pide una vez.
- **Diagnóstico en vivo** (lo mismo que el modo `diag` por USB).

### Lo que la app tendrá que cuidar (para que el iGate esté bien hecho)
- No devolver a APRS-IS lo que vino de APRS-IS (evitar bucles).
- No pasar a radio lo que lleve `RFONLY` ni `NOGATE`.
- No repetir sus propias tramas de vuelta.
- Descarte de duplicados (≈30 s) y control del ritmo de subida.
- Constructos `q`: los añade el servidor al recibir de un cliente verificado
  (`qAC`/`qAS`), no la app.

### Reparto de responsabilidades (importante)
| Función | Nodo (firmware) | App |
|---|---|---|
| Radio, modulación, CAD, digipeating | **Sí** | No |
| Balizas, telemetría, WX, registro de viaje | **Sí** | Solo lo muestra |
| Entramado KISS y nuestro protocolo | **Sí** | Lo habla |
| Internet y credenciales de APRS-IS | No | **Sí** |
| Mapas, historial, avisos del teléfono | No | **Sí** |

Así el nodo sigue siendo autónomo (funciona solo, sin teléfono) y el teléfono
solo aporta lo que el nodo no puede tener: Internet y pantalla grande.

## 3. Orden de trabajo propuesto (cuando el firmware esté cerrado)

1. **KISS por USB** (con modo automático/KISS/TNC2). Verificable con un cliente
   KISS de prueba en Node, sin móvil. → **Hecho: `tncProtocol` (0/1/2) y
   `tools/kiss_client.js`; ver el apartado 1.1.**
2. **KISS por BLE** (servicio GATT tipo NUS). Verificable con un script de prueba
   BLE y, en su caso, con la LoRa APRS App. → **ESCRITO, PERO APAGADO (2026-09-13): `bleEnabled` + `blePin`
   `tools/ble_kiss.html` / `tools/ble_kiss_server.js`; ver el apartado 1.2.**
3. **Protocolo de configuración por BLE** (nuestro JSON sobre otra característica
   o multiplexado), para poder configurar sin cable.
4. **APRS EA2OY System**: app Android (Kotlin) con KISS + config + mensajería +
   mapa + iGate. iOS solo si algún día hace falta.

## 4. Apps con las que hay que probar (lista de aceptación)

| App | Transporte | Modo | Qué demuestra |
|---|---|---|---|
| **APRSdroid** | USB OTG | KISS | Compatibilidad clásica y mensajes con acuse |
| **LoRa APRS App** (SQ2CPA) | BLE | KISS | Compatibilidad con el ecosistema LoRa APRS |
| **APRSIS32** (Windows) | USB serie | KISS | Compatibilidad con programas de escritorio |
| **APRS TNC Web** | — | KISS | TNC en el navegador |
| **Página de prueba BLE** (`tools/ble_kiss.html`) | BLE (Web Bluetooth) | KISS | Comprobar el KISS por Bluetooth desde el propio PC |
| **Terminal serie USB** (Android) | USB OTG | Nuestro CLI | Configuración y mandos desde el móvil (funciona ya) |
| **Configurador web** | USB | WebSerial | Solo escritorio (Chrome/Edge); Android no soporta WebSerial |

---

**Nota**: esto **no se toca hasta que el operador diga**. Primero se cierra el
firmware actual (pendiente de flashear y probar varias cosas) y después se
empieza por el punto 1 de la hoja de ruta (ya hechos: KISS por USB y KISS por
Bluetooth).
