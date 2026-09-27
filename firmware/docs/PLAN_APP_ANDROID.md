# Plan de trabajo — App Android para configurar el nodo APRS-LoRa de EA2OY

**Estado**: propuesta, sin nada empezado. Este documento **no copia ni toca** ningún fichero:
dice *qué* se haría y en *qué orden*. El operador decide cuándo se empieza.

**Fecha**: 2026-09-16 · **Árbol**: `_trabajo_ea2oy\` · **Fuente de la mina (SOLO LECTURA)**:
`C:\Users\Jesus\Desktop\MeshKachoUtility` (repo `EA2OY/MeshNavarra-Utility`, app "MeshNavarra Utility").

**De dónde sale este plan**: de leer **el código** (no de las fichas). Se han leído enteros
`src\protocol.cpp`, `src\protocol.h`, `src\config.cpp`, `src\config.cpp` en su parte de validación,
`src\cli.cpp`, `src\tnc.cpp`, `src\kiss.cpp`, la parte de USB de `src\main.cpp`, `src\diag.cpp`,
`src\flog.cpp`, el `SCHEMA` del configurador (`web\index.html`) y `web\flasher.html`; y en la app
hermana `UsbConnectionManager.kt`, `AndroidManifest.xml`, `app\build.gradle.kts`, `device_filter.xml`,
la librería `com.hoho.android.usbserial` (19 ficheros) y las licencias. Las fichas **N-13, N-14 y N-15**
del Cerebro se han usado como guía, pero **donde el código dice otra cosa, manda el código** (§2).

---

## 1. Para qué esto (el problema, en una frase)

El configurador web **no puede funcionar en un móvil**: usa WebSerial, que sólo existe en Chrome/Edge de
escritorio, y la propia página lo detecta y bloquea el botón *Conectar* (`esMovil()` en `web\index.html`).
Pero el nodo **sí** habla por el cable con cualquier aparato que sepa abrir un puerto serie USB: el puerto
es USB-CDC a 115200 8N1, **una línea JSON por comando**, y contesta siempre.

O sea: **el firmware ya está preparado para una app**. Lo que falta es la app.

Y hay una app hermana que ya ha recorrido el camino difícil: permisos USB en Android 14/16, MIUI/HyperOS,
reconexión tras reiniciar el nodo, firma, publicación. Eso **no se vuelve a aprender**: se copia.

---

## 2. Lo que he verificado, y las tres cosas donde las fichas se equivocan

Todo lo de abajo está comprobado **en el código de este árbol**, no deducido.

| # | Dato | Fuente |
|---|---|---|
| 1 | El puerto es **USB-CDC 115200 8N1**, líneas terminadas en `\n`, y acepta `\r\n` | `protocol.h` cabecera; `protocol.cpp:113-114` |
| 2 | **Comandos JSON reales**: `get`, `status`, `set`, `beacon`, `msg`, `radio`, `diag`, `reboot` (alias `reset`), `factory_reset` | `protocol.cpp:307-376` |
| 3 | Una línea que **no** empieza por `{` va a la **consola de texto** (CLI) | `protocol.cpp:291-298` |
| 4 | Un byte **`0xC0`** abre una **trama KISS binaria**: desde ahí hasta el `0xC0` de cierre, todo es binario | `protocol.cpp:108-111`, `kiss.cpp:55-91` |
| 5 | El nodo **saluda al arrancar con texto suelto** ("Faketec_APRS_iGate_EA2OY v…", "config: loaded from flash", "radio: OK @…"), no con JSON | `main.cpp:176-208` |
| 6 | El nodo también manda **JSON espontáneo sin `ok`** cuando alguien pulsa el botón físico o al emitir una baliza automática | `main.cpp:92`, `main.cpp:142-148`, `main.cpp:409-413` |
| 7 | **`status` NO trae la configuración**; la trae `get` (y también la devuelve `set` y `factory_reset`) | `protocol.cpp:134-142`, `508-521` del configurador |
| 8 | El configurador **sondea `status` cada 2,5 s** y sólo pide `get` al conectar | `index.html:2704`, `index.html:2703` |
| 9 | **El límite de línea es 4096 bytes**, no 1024: se subió porque la configuración entera ocupa ~1320 caracteres y con 1024 el nodo contestaba `bad json` | `protocol.h:70-75` (y el comentario dice que se midió) |
| 10 | Un `set` **manda la configuración ENTERA**; el firmware valida campo a campo y **hace merge** sobre la actual (no borra lo que no llega) | `config.cpp:170-172`, `683` |
| 11 | Si una clave llega con el **tipo equivocado**, el nodo **no cambia ese ajuste y lo AVISA**: `ignorado (tipo incorrecto): <claves>`. **No es un fallo**: es un guardado parcial | `config.cpp:56-107`, `675-681` |
| 12 | El nodo **rechaza** (con `{"ok":false,"error":…}`) valores fuera de rango. Es la única validación que de verdad manda | `config.cpp` (todas las ramas con `errMsg`) |
| 13 | El `SCHEMA` del configurador tiene **12 secciones, 55 campos**, 4 de ellos con `reboot:true` (frecuencia, SF, CR, ancho de banda) y 1 sección marcada `inert` (Bluetooth) | `index.html:1077-1243` (contado) |
| 14 | `kissoff` / `kisson` **pausan y rearman KISS en RAM** (no cambian el ajuste guardado, se pierden al reiniciar) y el estado lo informa en `status.radio.kissPaused` | `cli.cpp:682-694`, `tnc.cpp:85-88`, `protocol.cpp:172` |
| 15 | El firmware **sigue contestando al JSON y a la consola aunque KISS esté activo** — es a propósito, para que el operador no se quede fuera | `protocol.h:3-7`, `tnc.cpp:143-151` |
| 16 | El **reinicio a modo grabación sólo ocurre si el puerto se CIERRA a 1200 baudios** con DTR bajo (`tud_cdc_line_state_cb`). A 115200 no pasa nada | `_pio_core\...\Adafruit_USBD_CDC.cpp:267-287` |
| 17 | La librería `usb-serial-for-android` es **MIT** (Copyright Google 2011-2013 + Mike Wakerly) | `MeshKachoUtility\third_party\usb-serial-for-android\LICENSE.txt` **y** el LICENSE.txt del repositorio de mik3y, descargado y comparado |
| 18 | Nuestro firmware y su app son **GPL-3.0** los dos: copiar código de allí aquí **es compatible**, conservando autoría | `LICENSE` de los dos árboles; `NOTICE` de la app hermana (Tai Soluciones) |

### Las tres correcciones a las fichas (aviso, como se pidió)

1. **`docs\protocol_config_v1.md` está desactualizado**: dice *"Max line: 1024 bytes"* (línea 13).
   El código dice **4096** (`protocol.h:75`) y el comentario explica por qué. **Si se hiciera caso al
   documento, la app partiría la configuración en trozos que el nodo no entiende.** Nuestra app usa 4096.
2. **La ficha N-15 dice que el `LICENSE` de la librería es *"MIT, NO MIT"*** — una errata de esa ficha.
   Está comprobado en dos sitios: es **MIT**, sin ambigüedad. Lo que **sí** está mal es el
   `third_party\usb-serial-for-android\README.md` de la app hermana, que la declara **LGPL-2.1**
   (contradice a su propio `LICENSE.txt`). Si copiamos ese README, **se corrige esa línea**.
3. **`THIRD_PARTY.md` de nuestro proyecto ya declara componentes LGPL-2.1** (el core de Adafruit) y explica
   que se pueden combinar en un trabajo GPL-3.0. Comprobado: **la combinación que proponemos es correcta**
   (MIT + GPL-3.0 → GPL-3.0). No hay problema de licencias en este plan; sí hay que anotar la autoría
   (§3.4).

---

## 3. Qué se copia tal cual y qué hay que reescribir

### 3.1 Se copia TAL CUAL (con la ruta exacta)

Las rutas de la izquierda son de `C:\Users\Jesus\Desktop\MeshKachoUtility\`; las de la derecha, de nuestro
árbol (módulo `app\`, que todavía **no existe**: se creará en la Fase 0).

**A) La capa de puerto serie — 19 ficheros Java, 162.825 bytes.** Origen
`app\src\main\java\com\hoho\android\usbserial\` → destino idéntico dentro de nuestro módulo:

```
driver\CdcAcmSerialDriver.java      driver\CommonUsbSerialPort.java    driver\UsbSerialPort.java
driver\UsbSerialDriver.java         driver\UsbSerialProber.java        driver\ProbeTable.java
driver\UsbId.java                   driver\Cp21xxSerialDriver.java     driver\FtdiSerialDriver.java
driver\Ch34xSerialDriver.java       driver\ProlificSerialDriver.java   driver\GsmModemSerialDriver.java
driver\ChromeCcdSerialDriver.java   driver\SerialTimeoutException.java
util\SerialInputOutputManager.java  util\UsbUtils.java                 util\MonotonicClock.java
util\HexDump.java                   util\XonXoffFilter.java
third_party\usb-serial-for-android\LICENSE.txt  (+ README.md, corrigiendo la línea de licencia: es MIT)
```

*Por qué se copia y no se pone como dependencia*: F-Droid no resuelve JitPack, y además así el proyecto
compila sin red. Es la copia que ellos ya probaron (versión 3.11.0, con **una sola** adaptación: quitaron
un `BuildConfig.DEBUG` que rompía la compilación desde fuentes vendidas — se copia **su** versión ya adaptada).

*Qué NO se usa de ahí*: `SerialInputOutputManager`, `HexDump`, `UsbUtils` y `XonXoffFilter` no los usa ni su
propia app (comprobado). Viajan igual: son parte del paquete y quitarlos complica la atribución sin ganar nada.

**B) Herramientas de trabajo (los `.ps1`), a una carpeta `tools_android\` nuestra:**

| Origen | Qué es | Qué hay que retocar |
|---|---|---|
| `download_jdk.ps1` | Se baja un **JDK 17 Temurin portátil** a `jdk-17\` si no está | Nada |
| `setup_gradle_wrapper.ps1` | Se baja Gradle 8.2 y genera el `gradlew` si falta | Nada (o subir la versión, es decisión) |
| `build_apk.ps1` | 7 líneas: fija `JAVA_HOME` al JDK del repo y lanza `assembleDebug` | **La ruta del JDK** |
| `backup.ps1` | Snapshots rodantes `snap-<fecha>.zip` (retención 30) + copias del cerebro (60) + **avisa si un fichero clave baja de 1000 bytes** | La lista `$items` (nuestros ficheros). Es **la herramienta más valiosa** de todo el lote |

**C) Patrones que se copian *como forma*, no como fichero** (el fichero es suyo y habla de otra cosa):

| Origen | Qué se aprovecha |
|---|---|
| `app\src\main\AndroidManifest.xml` | `uses-feature usb.host required="false"`, el `<intent-filter>` de `USB_DEVICE_ATTACHED` + `meta-data` al `device_filter`, el **`FileProvider`** (para compartir el JSON de configuración y los GPX), y la lista de permisos **sin INTERNET** |
| `app\src\main\res\xml\device_filter.xml` | El fichero se copia, pero **hay que decidir** si se le añade nuestro `VID 0x239A` (§5.5) |
| `UsbConnectionManager.kt` (373 líneas) | **El fichero estrella, pero se reescribe** (§3.2): su permiso USB (`FLAG_MUTABLE` + `.setPackage()`), su bucle de lectura tolerante (5 errores seguidos antes de dar el enlace por muerto), su *rediscover* tras reinicio, y su `setDTR(true)`/`setRTS(true)` |
| `app\build.gradle.kts` | El bloque `signingConfigs` que lee `local.properties` — **con los tres avisos de la ficha N-13 §C.5** (rutas con `/`, `keyPassword` = `storePassword`, contraseñas sólo alfanuméricas) |
| `RemoteControlReceiver.kt` | El patrón "mando la app desde el PC y me contesta por fichero" (broadcast **explícito**, que es lo único que funciona con la app en segundo plano). Su acción se renombra a nuestro paquete |
| `docs_pdf\plantilla_app_meshnavarra.tex` | La plantilla de manual en PDF (pandoc + xelatex). Sólo si al final queremos manual en PDF |
| `fdroid\metadata.yml`, `fastlane\metadata\android\{en-US,es-ES}\` | **Sólo si se publica** (§7): plantilla literal, con las reglas ya masticadas (un solo bloque, hash de 40 caracteres, `subdir: app`, sin `output:`, sin `Binaries`) |
| `jdk-17\` | **No se copia**: lo trae `download_jdk.ps1` solo |

### 3.2 Hay que REESCRIBIR (no existe nada parecido que copiar)

1. **Toda la capa de protocolo.** Su app habla **Meshtastic**: protobuf, entramado binario `0x94 0xC3` +
   longitud BE16, UUIDs GATT, PKI, y los **32 bytes `0xC3` de "despertar"** al conectar. De eso, aquí, **no
   sirve nada**. Lo nuestro son **líneas de texto** y, dentro del mismo puerto, tramas KISS. Es justo el
   transporte que la ficha N-15 recomienda **quitar** de su código.
   *Aviso*: esos 32 bytes `0xC3` **no se copian**. En nuestro firmware serían 32 caracteres basura metidos
   en el buffer de línea; el nodo respondería `bad json` a la primera orden y ensuciaría el registro.
2. **El entramado KISS de lectura.** La app hermana **no** tiene nada de esto. Y esta es la parte que más
   se puede estropear (§4).
3. **El modelo de configuración y el formulario, generados desde el `SCHEMA`** (§5).
4. **Toda la interfaz**, desde cero: **el suyo es un `MainActivity.kt` de 8.202 líneas** y la ficha N-13 lo
   llama *"su deuda aceptada"*. Nosotros, módulos separados.
5. **Todo lo de Bluetooth**: no se copia ni una línea (ver §6 y §8). Su capa BLE entera, su escaneo, su
   emparejamiento, su `NodeDB`… **fuera**.
6. **El flasher** (§5.6): su app no lo tiene y el nuestro del navegador no se puede portar.
7. **La matriz de iconos APRS**, que en el configurador **viaja dentro del HTML** (`index.html:3899-3930`):
   hay que extraerla a un recurso de la app, **conservando la atribución** (*juego de símbolos de aprs.fi,
   `github.com/hessu/aprs-symbols`, de OH7LZB*) y **los cuatro iconos retirados por derechos**
   (`ICONOS_RETIRADOS`, `index.html:2166`). No es copiar de la app hermana: es copiar **de nuestro propio
   configurador**, que ya lo tiene resuelto.
8. **Los datos que hoy viven dentro del HTML**: los valores recomendados (`recommendedValues()`,
   `index.html:2512-2556`) y las rutas APRS admitidas (`PATHS`, `index.html:1075`) también salen del
   `SCHEMA` a datos.

### 3.3 Se copia el **código** de nuestro propio configurador (con su prueba)

Esto es lo que evita rehacer trabajo que ya está hecho **y probado**:

| De `web\index.html` | Qué se lleva | Cómo se comprueba |
|---|---|---|
| `parseLog` / sesiones del registro | El parser del registro de viaje (líneas `TX TRK`, `RX`, `DG`, `EVT boot`), con el corte por sesión y la regla *"la fecha la pone sólo el GPS; si no hay hora, el punto va sin hora"* | **`tools\prueba_registro.js` ya existe, se ejecuta en Node y pasa** (comprobado) |
| `buildKml` / `buildGpx` / CSV y `nombreFichero` | Los tres exportadores | El mismo `prueba_registro.js`, **y además `tools\log2gpx.js`**, que hace lo mismo desde la línea de órdenes: **dos implementaciones independientes con las que comparar la tercera (la de la app)** |
| Proyección del mapa y pintado | La fórmula de teselas de OpenStreetMap | **`tools\prueba_mapa.js` ya existe, se ejecuta en Node y pasa** (comprobado) |
| `web_check.js` | La comprobación del `SCHEMA` (campos con ayuda y textos ES/EN): hoy dice **55 campos y 55 ayudas, todos los idiomas completos** | **`tools\web_check.js` ya existe y pasa** (comprobado) |
| `tools\log2gpx.js` | **Una tercera implementación** de la conversión registro → GPX/KML/CSV, desde la línea de órdenes | Se ejecuta en Node |

**Estos scripts se ejecutan hoy en Node** (v24.20.0, **comprobado en esta misma sesión**; sólo usan módulos
internos de Node, **ninguna dependencia que instalar**). Eso significa que **la lógica de la app se puede
probar antes de escribir una línea de Kotlin** — y que cuando se porte, hay con qué comparar.

**Medido hoy** (esto ya no es una suposición, es el resultado de ejecutarlos):

| Script | Resultado |
|---|---|
| `tools\web_check.js` | **TODO CORRECTO** — JS sin errores de sintaxis, **55 campos y 55 textos de ayuda**, 185 claves de idioma usadas y 243 definidas en los dos idiomas, 94 elementos que busca el JavaScript y 96 definidos (los dos de más son el propio `btnArriba` y el contenedor que se crean desde el script) |
| `tools\prueba_registro.js` | **TODO CORRECTO**, con **8 bloques de pruebas**: fechas imposibles, altitud negativa, 17 puntos del registro real, posiciones sin hora (que se perdían), y que el mapa no se quede con una ruta vieja |
| `tools\prueba_mapa.js` | **TODO CORRECTO** — proyección contra la fórmula de OpenStreetMap y pintado real sobre un lienzo falso (29 segmentos, etiquetas A/B) |
| `tools\prueba_avisos.js` | ⚠ **FALLA HOY**: `ReferenceError: callsignValido is not defined` |

**Sobre ese fallo, que es importante no malinterpretar**: **no es un fallo del configurador**. El script coge
de `index.html` **sólo el cuerpo de `updateNotes()`** (`prueba_avisos.js:25-53`) y lo ejecuta con
`new Function("collectConfig", …)`. Dentro, `updateNotes()` llama a `callsignValido()`
(`index.html:1847`, una función que existe y se usa en toda la página), pero **no está entre lo que el
script le pasa**, así que no la encuentra. **Es la prueba la que se ha quedado estrecha, no el formulario**
(la regla de este proyecto: *gana el código*). Se arregla en una línea (pasar también `callsignValido`), y
**conviene arreglarlo antes de portar los avisos a la app**: esos 18 casos son justo lo que queremos
reutilizar. **No es tarea de este plan** (prohibido tocar el configurador y las herramientas), así que queda
apuntado como pendiente para quien lo retome.

### 3.4 La licencia, en claro

- `usb-serial-for-android` es **MIT**: se puede usar, modificar y distribuir **citando la autoría**
  (se copia su `LICENSE.txt` al lado del código y se anota en `THIRD_PARTY.md`). **MIT no obliga a abrir
  nuestro código.** (Y si algún día se quiere, se puede poner como dependencia de Gradle en vez de vendida.)
- Su **app** es **GPL-3.0**, igual que nuestro proyecto. Copiar de allí a aquí **es legal y compatible**,
  pero la GPL exige **conservar la autoría y el aviso**: si se copia su `UsbConnectionManager.kt` (aunque sea
  adaptado), hay que dejar constancia de que viene de *MeshNavarra Utility, Tai Soluciones, GPL-3.0*.
  **Decisión de estilo recomendada**: en lugar de copiar ese fichero entero, **escribir el nuestro en Kotlin
  siguiendo su diseño** (§3.1-C), citando de dónde viene la idea. Queda más limpio y se entiende mejor.
- **Consecuencia para el operador**: como el proyecto es GPL-3.0, **si la app se reparte, el código hay que
  publicarlo**. Si es de uso propio y no sale del móvil del operador, no hay obligación de publicar nada.
  Es una de las preguntas de §7.

---

## 4. Cómo se comporta la app con el puerto (lo más delicado)

### 4.1 El puerto mezcla tres flujos, y la app tiene que saber cuál tiene delante

En el **mismo cable**, byte a byte:

```
   {"ok":true,"status":{…}}      <- JSON: respuestas a lo que pedimos + avisos espontáneos del nodo
   PONG v1.0alpha b9 | EA2OY-7…  <- TEXTO: consola (CLI), y TAMBIÉN el saludo de arranque y el registro
   C0 00 9C 82 … C0              <- KISS: tramas AX.25 binarias del TNC (el "módem" del operador)
```

El firmware separa los tres así (`protocol.cpp:100-123`):
`0xC0` ⇒ **todo** lo que venga hasta el `0xC0` de cierre es KISS; `\n` ⇒ línea completa, que va al CLI si
**no** empieza por `{` y al JSON si empieza por `{`.

**Trampa número uno, y es real:** una trama KISS **puede empezar en mitad de una línea de texto** sin
aviso. El configurador web **no lo contempla** (no hay ni una mención a `0xC0` en `index.html`, comprobado):
si el TNC está activo, su bucle de lectura mete bytes binarios en el mismo acumulador de líneas y la consola
se llena de basura hasta que aparece un salto de línea. **En la app esto no se puede permitir.**

### 4.2 Las cuatro reglas de convivencia con el TNC

**Regla 1 — Al leer, el `0xC0` manda.** El acumulador de líneas se vacía en cuanto llega un `0xC0`
(la media línea de texto que hubiera se descarta, igual que hace el firmware por su lado) y **todo** lo que
venga hasta el `0xC0` de cierre se aparta como *tráfico del TNC*, se cuenta y **no se interpreta, no se
parsea y no se enseña**. En la interfaz aparece un aviso discreto: *"El nodo está hablando KISS con otro
programa — 42 tramas"*. Nada más. Si esa basura entrara al parser de JSON acabaríamos con avisos de error
falsos y con la sensación de que el nodo está roto.

**Regla 2 — Al conectar, se MIRA quién manda: no se escribe nada.** El primer `status` (sólo lectura) trae
`status.radio.tnc` (0 apagado / 1 TNC2 / 2 KISS) y `status.radio.kissPaused`. **En cuanto llegue, la app
sabe si el puerto es suyo o es el módem del operador.** Esto es lo que el configurador web **tampoco hace**
(no lee esos dos campos, comprobado): un hueco que la app puede cerrar gratis, y encima a su favor, porque
el dato ya viaja en cada sondeo.

**Regla 3 — Con KISS activo (y sin pausar), la app se queda en modo mirón.** Sigue el sondeo de `status`
cada 2,5 s (es inofensivo: el nodo contesta una línea de texto entre tramas, y un programa KISS serio ignora
lo que no empieza por `0xC0`) pero **deja apagados** guardar, baliza, telemetría, meteorología, mensajes y
`radio`/`diag`. En su lugar, un cartel claro con la explicación y **un solo botón**:

> *Este nodo está haciendo de módem para otro programa de APRS (KISS). Si guardas ajustes ahora,
> tus órdenes y las respuestas van a mezclarse con las tramas del otro programa. ¿Quieres que el nodo
> atienda a esta app durante un rato? (El otro programa se queda sin módem hasta que lo devuelvas o
> reinicies el nodo. El ajuste guardado NO se toca.)*

**Regla 4 — Salir de ahí, sólo con confirmación escrita, y por la puerta que ya existe.**
El botón manda `kissoff` (consola). Efectos, comprobados en el código: el nodo **vuelve a obedecer las
órdenes normales** y **vuelve a mandar sus balizas**; el ajuste `tncProtocol` **no cambia**; y **se pierde
al reiniciar** (`tnc.cpp:81-88`). Se confirma viendo `kissPaused:true` en el siguiente `status`. Para
devolver el puerto: `kisson` (o reiniciar el nodo, que lo rearma solo). **La app no rearma KISS por su
cuenta al desconectar sin decirlo**: si lo hizo el operador, se le dice que sigue pausado y se le da el
botón; si no, la app lo rearma al irse. Nunca en silencio.

### 4.3 Cómo se abre el puerto

1. **115200, 8 bits, 1 de parada, sin paridad, sin flujo** (`UsbConnectionManager.kt:231`).
2. **DTR y RTS afirmados** (`:238-239`). Es inofensivo en USB-CDC (va fuera de banda) y hay nodos que sin
   eso ignoran al host. **Comprobado que NO reinicia la placa**: el único camino de reinicio a modo
   grabación es **cerrar a 1200 baudios** con DTR bajo (`Adafruit_USBD_CDC.cpp:271-286`). A 115200 no dispara.
3. **NO se mandan los 32 bytes `0xC3`** que manda la app hermana: son de su protocolo y en el nuestro son
   basura (§3.2-1).
4. **El "toque" a 1200 baudios** (entrar en modo grabación sin tocar el botón) **no se hace nunca solo**:
   sólo si el operador lo pide, avisando de que el nodo se va a reiniciar y de que puede que no funcione
   (el propio flasher web avisa de que en algunos PCs falla — `flasher.html:136-145`).
5. **Lectura binaria, no de líneas**: el trozo que llega se parte por `\n`, con un tope de 4096 y
   **descartando lo que pase del tope** (si no, una línea gigante se come la memoria del móvil).
6. **Cualquier excepción al abrir** (`openDevice`, `claimInterface`, `setParameters` lanzan
   `SecurityException`/`IllegalStateException`, no sólo `IOException`) se captura **genérica** y se cuenta
   por el listener. En Android 14+ es la diferencia entre un mensaje y un cierre de app.
7. **Un solo dueño del puerto.** Android **no deja** que dos apps usen el mismo USB a la vez. Si APRSdroid
   lo tiene, esta app **no puede** abrirlo y tiene que decirlo con esas palabras (*"otro programa tiene el
   cable; ciérralo"*), no con un error del sistema.

### 4.4 Órdenes y respuestas: sin adivinar

El firmware **no devuelve ningún identificador** de la petición, así que la app no puede "emparejar" por
número. Se empareja **por forma de la respuesta**, que es inequívoca:

| Lo que llega | A qué contesta |
|---|---|
| `{"ok":true,"config":{…},"persisted":…}` | a un `get` o a un `set` |
| `{"ok":true,"status":{…}}` | al sondeo `status` |
| `{"ok":true,"beacon":{…}}` | al `beacon` |
| `{"ok":true,"msg":{…}}` | al `msg` |
| `{"ok":true,"radio":{…}}` / `{"ok":true,"diag":{…}}` | a `radio` / `diag` |
| `{"ok":false,"error":"…"}` | al que estuviera en curso **de esa familia** |
| Cualquier JSON **sin `ok`** (`button`, `radio`, `power`, `diag`, `setcoords`, `aprs`) | **no es respuesta**: es un aviso espontáneo del nodo ⇒ se enseña en la consola |

*Referencia*: la web hace exactamente este despacho en `index.html:3550-3583` (`onJson`).

Consecuencias de diseño, que hay que respetar:
- **Una orden en curso a la vez**, y mientras hay una orden "de verdad" (guardar, baliza, mensaje) **se
  para el sondeo**. Al revés llegarían respuestas cruzadas.
- **El botón físico del nodo mete JSON en el cable en cualquier momento** (`main.cpp:142-148`). Si la app
  emparejara "el siguiente JSON" con "mi orden", un dedo en el botón rompería el guardado.
- El error del nodo **se enseña tal cual**, sin inventar texto: `beaconInterval >= 15`,
  `sleepWakeMv must be > sleepCutMv`, `profiles no pueden repetir el mismo SSID`…
  **Y cuando el mensaje empieza por `ignorado (tipo incorrecto):` NO es un fallo de guardado**: es
  *"guardado, pero estos campos no se han aceptado y siguen como estaban"*. Si la app lo pintara en rojo
  como un error, el operador volvería a creer que el nodo le miente (que es justo lo que ese mensaje
  existe para evitar).
- **Aviso al conectar**: el nodo saluda con texto suelto ("Faketec_APRS_iGate_EA2OY v…"). Eso **no** es
  respuesta a nada: va a la consola. Y si el operador acaba de reiniciar, el puerto **se vuelve a enumerar**:
  hay que **volver a descubrir** el aparato (el `UsbDevice` guardado queda obsoleto ⇒ *"No USB driver found"*)
  y pedir permiso otra vez. **No es un fallo**: en MIUI/HyperOS pasa en cada relanzamiento.

---

## 5. Reutilizar el `SCHEMA`: la decisión, y por qué

### 5.1 El problema

El configurador describe el formulario **como datos**: 12 secciones, 55 campos, con tipo, rango, paso,
lista de valores, si necesita reinicio, si es inerte, textos en ES y EN y ayudas largas
(`index.html:1077-1243`). Reescribir eso a mano en Kotlin sería **rehacer 55 campos y sus ayudas**, y
—peor— **crear una segunda versión de la verdad** que se iría separando de la web. La ficha N-14 lo dice
muy bien: *"lo que a ellos les costó una década de literales en un monolito"*.

### 5.2 La decisión propuesta: **generar un fichero de datos, y verificarlo por script**

```
  web\index.html  ──(script Node: tools\extrae_schema.js)──►  app\src\main\assets\schema.json
                              │                                        │
                              │                              (la app lo lee al arrancar)
                              ▼
                  tools\app_check.js comprueba que el JSON tiene EXACTAMENTE
                  las mismas secciones, claves y rangos que el SCHEMA del HTML
```

- **Un fichero de datos** (`schema.json`), no código generado. Motivo: se puede leer, comparar y corregir
  sin recompilar, y **si el nodo cambia un rango, se cambia el JSON y ya está**.
- **Generado por script, nunca a mano.** Aquí está la gracia: **no puede quedarse viejo** si el script
  forma parte de la comprobación. Es el mismo truco que ya usan las pruebas de esta casa, que **cogen el
  código tal cual del HTML** *"para que la prueba no pueda quedarse vieja respecto al configurador"*
  (`tools\prueba_registro.js:12`, `tools\prueba_avisos.js:5`). **Ya está inventado aquí**: sólo hay que
  hacerlo otra vez. Y `tools\web_check.js:33-38` ya sabe **localizar y contar** el `SCHEMA` y sus ayudas:
  ese trozo es exactamente el punto de partida del extractor.
- **La app no interpreta JavaScript**: sólo lee el JSON y pinta. El intérprete de verdad es la web.
- **Ventaja lateral**: `schema.json` sirve también para **documentar** y para que un test compruebe que
  ningún campo se ha quedado sin texto en uno de los dos idiomas (`web_check.js` ya hace eso con el HTML).

**Lo que NO se hace**: generar clases Kotlin una por campo (55 clases para 55 casillas: mucho código que
compilar y que mantener, y cada cambio del nodo obligaría a recompilar la app). Y **tampoco** meter el
JavaScript del configurador dentro de un WebView para "reutilizarlo": el configurador necesita WebSerial,
que no existe en el WebView de Android. Eso está muerto por diseño.

### 5.3 Qué se lleva el `schema.json`

De cada campo: `key`, `type`, `min`/`max`/`step`, `vals`/`opts`, `maxlen`, `list`, `reboot`, `extra`,
`apply`, y los textos `label`/`hint` en **es** y **en**. Además:
- Los tres bloques que hoy **no** están en el `SCHEMA` y que también son datos:
  **`PATHS`** (`index.html:1075`), **`recommendedValues()`** (`index.html:2512-2556`, con su variante
  según el módulo de radio: `power 12` en E22P, `22` en SX1262; y `sleepCutMv 3500/3400`) y el
  **`toggle:`/`opts` de los tres secciones `enum`**.
- En qué idioma poner las ayudas largas: **propuesta = el `hint` corto y la etiqueta viajan en el JSON (ES+EN);
  las frases largas de ayuda, en `strings.xml` + `values-es\`**, como cualquier app. Si no, se acaba
  manteniendo textos en un JSON en vez de en un recurso de Android, que es donde Android los espera.
- **La sección `bluetooth` se lleva con su marca `inert`** y la app muestra el mismo aviso rojo que la web:
  *"esto se guarda pero el nodo no lo usa"*. Sin eso, alguien creerá que ha encendido el Bluetooth.

### 5.4 La validación: dos capas, y la de arriba no decide

1. **La app** (mirando el `schema.json`): avisa mientras se escribe y **no deja escribir** lo que no cabe
   (los 63 caracteres de `sleepMsg`, los 64 de `comment`/`status`: el firmware **corta en silencio**, así que
   dejarlo escribir sería mentirle al operador).
2. **El nodo**: es **el único que decide**. Todo lo que diga el `schema.json` es cosmético; si el nodo
   contesta `{"ok":false,…}`, eso es lo que vale y así se enseña.
3. **Regla de oro** (ficha N-14, error nº 14, y nos pasó a nosotros el 2026-09-15): **los rangos del
   formulario salen del firmware, no de la imaginación**. Los 55 campos del `SCHEMA` ya están ajustados a
   `config.cpp` (mínimo de baliza 15, telemetría 0 o 15-720 de 15 en 15, `posAmbiguity` 0-4,
   `smartBeaconPreset` 0-3…). **No se toca ni uno sin mirar `config.cpp` primero.**
4. **Y lo que el firmware no sabe comprobar, se comprueba en la app** con un aviso *que no bloquea*:
   `sleepWakeMv > sleepCutMv` (lo rechaza el nodo), el indicativo vacío o de fábrica (`NOCALL-11`),
   las coordenadas en 0,0, un nodo al que le quede un `tocall` viejo (p. ej. `APLRG1`, que es el de **otro**
   firmware y aprs.fi lo atribuiría a otro; el bueno es `APL2OY` y ya no se edita desde el configurador),
   muchos saltos para un nodo fijo, y ajustes de GPS en un modo donde no hacen nada.
   Esos son los avisos de `tools\prueba_avisos.js` (**18 casos ya escritos**): el mismo catálogo, portado.

### 5.5 Qué se manda al guardar

**La configuración completa**, como hace la web: se pide `get`, se cambia lo que el operador haya tocado,
se manda **todo** en un `set`. Ventajas: encaja con el merge del firmware y con su contrato de
*"aplica lo que entiendas y no toques lo demás"*; **nunca borra una sección** (el fallo clásico que
describe la ficha N-13, error nº 10); y **la respuesta es la lectura de vuelta**, que es lo que el operador
ve como confirmación (incluidos los recortes que haya hecho el nodo, por ejemplo bajar la potencia al
máximo del módulo). **Antes de mandar hay que comprobar que la línea no pasa de 4096 bytes** y, si pasa,
decirlo (hoy no pasa: ~1320 caracteres medidos en el propio firmware).

### 5.6 El "flasher": qué se hace y qué no (y por qué)

**No entra, y no por pereza.** Comprobado en `flasher.html`:
- La grabación normal es **copiar un `.uf2` a una unidad de memoria** que la placa crea al entrar en modo
  grabación. **Android no puede montar esa unidad de memoria por USB** sin root: no hay API pública.
- El "toque" a 1200 baudios **sí** se puede hacer desde Android (`setParameters(1200,…)` y cerrar), pero
  el flasher web ya avisa de que **falla en algunos equipos**, y en los T-Echo la vía buena es la unidad de
  memoria, no el toque.
- **Lo que sí entra, y es lo útil de verdad**: la app **detecta y dice en qué estado está la placa**:
  *"está funcionando con el firmware"* o *"está en modo grabación (lista para recibir el fichero)"*, mirando
  el identificador USB (`239a:802a` = aplicación, `239a:00b3` = bootloader, `flasher.html:232-237`), y
  **explica qué hacer a continuación**. Eso responde a la duda que más tiempo hace perder, y es
  exactamente lo que el flasher web hace en su paso 3.
- Los `.uf2` **no se meten en la app** (la ficha N-13, error nº 51: 11,2 MB de APK por meter PDFs; y encima
  envejecerían).

---

## 6. Alcance: qué entra en la primera versión y qué no

### 6.1 Lo que hace hoy el configurador, y qué hacemos con cada cosa

| Función del configurador | ¿Entra? | Fase | Por qué |
|---|---|---|---|
| Conectar / desconectar por USB | **Sí** | 1 | Es el cimiento |
| Estado en vivo (versión, radio, RX/TX, GPS, sensores, batería, pantalla) | **Sí** | 1 | Sondeo `status` cada 2,5 s: sólo lectura, cero riesgo |
| Leer la configuración (`get`) | **Sí** | 2 | Sin escribir nada |
| Formulario completo desde el `SCHEMA` + avisos | **Sí** | 2 | Es el corazón del port |
| Guardar (`set`) con confirmación y aviso de reinicio | **Sí** | 3 | Es la razón de ser de la app |
| Exportar / importar la configuración en un fichero | **Sí** | 3 | Barato (hay `get`/`set` y ya existe el formato) y **es la red de seguridad**: antes de tocar un nodo ajeno, se guarda su JSON |
| Baliza manual, telemetría, WX, mute/unmute | **Sí** | 4 | Botones, y todos tienen respuesta con `ok` |
| Reiniciar, reset de fábrica, `wipe`, DFU | **Sí**, con puerta CONFIRMAR | 4 | El patrón ya está probado en la web (`typedConfirm`) |
| Consola (comandos de texto) | **Sí** | 4 | El nodo tiene un CLI muy bueno y es gratis enseñarlo. Modo avanzado, no en la pestaña principal |
| Mensajes APRS y "mensaje guardado" | **Sí** | 4 | Es mandar aire: la app avisa de que eso sale por radio |
| Aviso de modo TNC/KISS + pausa (`kissoff`) | **Sí** | 5 | **Es el requisito más delicado** (§4) |
| Registro de viaje (descargar `log dump`) | **Sí** | 6 | Sin esto no hay mapa ni exportaciones |
| Exportar GPX / KML / CSV | **Sí** | 6 | La lógica ya existe y está probada en Node |
| Mapa con la ruta | **Sí** | 7 | Necesita internet para las teselas: es **el único permiso nuevo** que hay que pedir |
| Detector de estado de la placa (aplicación / modo grabación) | **Sí** | 8 | Barato y muy útil |
| Flasher (grabar el `.uf2`) | **No** | — | Android no puede montar la unidad de memoria sin root (§5.6) |
| WebSerial en un WebView | **No** | — | No existe en Android: es el motivo de este proyecto |
| Bluetooth (KISS por BLE) | **No** | — | Está **fuera de la compilación** del firmware desde el 2026-09-13 (§8) |
| Configurar el iGate ESP32 de casa | **No** | — | Habla **otro** protocolo (`igate_conf.json`, su web) y **abrir su puerto serie reinicia la placa**. Sería otro trabajo, otro plan |

### 6.2 Qué NO entra en la primera versión, resumido

Mapa, registro de viaje y exportaciones **entran en el plan pero no en la primera versión**: son las tres
cosas que **más código** tienen (parser + dibujo + ficheros) y **no ayudan a lo único que hay que demostrar
primero**, que es que la app habla con el nodo. Primero el cable, después el mapa.

---

## 7. Fases: cada una se prueba en un teléfono de verdad

**Regla de cada fase**: no se empieza la siguiente hasta que la anterior **pasa su prueba en un teléfono
real**, con la app instalada por cable. Y cada fase sube `versionCode` + `versionName` y tiene su
`changelog`, como manda la norma del proyecto hermano (es una disciplina que sale gratis y evita el
*"¿qué versión llevo?"*).

No se ponen plazos en días ni semanas: no se pueden saber. Se pone **qué se prueba** y **qué tiene que
pasar** para dar la fase por buena.

---

### Fase 0 — Esqueleto que se instala (sin USB)
**Se construye**: el módulo `app\`, el `applicationId`, el toolchain (JDK 17 portátil, Gradle wrapper,
`build_apk.ps1`, `backup.ps1`), el tema, los dos idiomas (`values\` + `values-es\`), el log y el capturador
de cierres a fichero, y las pestañas vacías.
**Se prueba**: el APK se instala y arranca; se ven las pestañas; se cambia de idioma; el fichero de log
crece; **no hay ni un acento roto** (`Ã`, `ðŸ`, `â€`).
**Qué demuestra**: que el taller funciona. Sin esto, cualquier fallo posterior es ambiguo.

---

### Fase 1 — **LA MÍNIMA**: conectar, leer el estado y enseñarlo
**Se construye**: la capa USB (los 19 ficheros + el gestor propio), el permiso con `FLAG_MUTABLE` +
`.setPackage()`, el bucle de lectura con el **troceado por líneas y la Regla 1 del `0xC0`**, el cliente
JSON mínimo (`status`) y una pantalla con los datos en crudo.
**Se prueba (en el móvil, con el nodo enchufado)**: aceptar el permiso USB y ver aparecer, a los 2,5 s y
**solo**, la versión, el número de compilación, el indicativo, el modo, la batería, el GPS, RX/TX y el
módulo de radio. Después: **desenchufar y volver a enchufar**; **reiniciar el nodo** (el puerto se
re-enumera ⇒ la app tiene que redescubrirlo y volver a pedir permiso); y **pulsar el botón físico** del
nodo (tiene que salir como aviso, no romper nada).
**Qué demuestra**: **la app habla con el nodo**. Es la única pregunta que importa en este punto.
**Criterio de "hecho"**: los contadores `status.usb.bytes` y `status.usb.lineas` **suben** entre sondeos
(eso prueba que el cable va y que el nodo nos está oyendo, no sólo que nosotros lo oímos a él), y ninguna
línea binaria ha llegado al parser.

---

### Fase 2 — Leer la configuración y pintar el formulario (sin escribir)
**Se construye**: el generador `tools\extrae_schema.js` → `assets\schema.json`; `tools\app_check.js`; y el
formulario que se pinta **desde los datos** (los 6 tipos: `text`, `int`, `float`, `bool`, `enum`, `enumf`,
más la tabla de perfiles y los tres "extras": selector de iconos, botón del GPS y botón de la altura).
**Antes de esta fase, un arreglo de una línea** (fuera de este plan, porque toca `tools\`):
`tools\prueba_avisos.js` **hoy falla** por cómo extrae el código (§3.3). Se arregla pasándole también
`callsignValido`, y entonces sus **18 casos** sirven de red para portar los avisos a la app. Si no se
arregla, los avisos se portan a mano y **sin prueba**: justo lo que no queremos.
**Se prueba**: pulsar *Leer configuración* y que los 55 campos salgan **con los valores reales del nodo**,
incluida la tabla de los 4 perfiles y el aviso rojo de la sección Bluetooth. Se prueban los dos idiomas.
**Qué demuestra**: que el `SCHEMA` sirve, que es la apuesta del plan. **Si esta fase sale mal, el plan
cambia aquí**, no en la fase 5.
**Criterio de "hecho"**: `tools\app_check.js` pasa (mismas 12 secciones y 55 claves que el HTML, y ningún
campo sin ayuda en ES o EN) y **ningún campo del formulario se queda vacío o inventado**.

---

### Fase 3 — Guardar (la fase que toca el nodo, con red)
**Se construye**: `get` + cambios + `set` del bloque completo; la validación de la app; el aviso de los 4
campos que **necesitan reinicio**; el tratamiento del `ignorado (tipo incorrecto)` como **guardado parcial**
y no como error; la puerta CONFIRMAR; y **exportar/importar la configuración a un fichero** (que es la red
de seguridad antes de tocar nada).
**Se prueba, en este orden**:
1. Cambiar algo inofensivo (el mensaje de estado), guardar, y ver en la respuesta la lectura de vuelta.
2. **Cerrar la app, volver a abrirla, leer: el valor tiene que seguir.**
3. Guardar un decimal en un campo de minutos ⇒ la app lo tiene que parar **antes** de mandarlo.
4. Guardar un valor fuera de rango (por ejemplo baliza a 10) ⇒ el nodo contesta con su error y la app lo
   enseña **tal cual**.
5. Cambiar la frecuencia ⇒ la app tiene que avisar de que **no surte efecto hasta reiniciar**.
6. **Guardar con KISS activo ⇒ la app tiene que negarse** (§4.2, Regla 3).
**Qué demuestra**: que se puede configurar desde el móvil, que es el objetivo del proyecto.
**Criterio de "hecho"**: se exporta la configuración a un fichero, se cambian tres cosas, se importa el
fichero, se guarda, y el nodo vuelve **exactamente** a lo que había (el `get` devuelve el mismo JSON).

---

### Fase 4 — Acciones y consola
**Se construye**: baliza, telemetría, WX, mute/unmute, reintentos de mensaje, mensaje rápido, boletines,
objetos, reiniciar / reset de fábrica / `wipe` / DFU (los cuatro con CONFIRMAR escrita), y la consola de
texto con el catálogo de `help`.
**Se prueba**:
1. *Baliza* ⇒ en unos segundos aparece en aprs.fi con la hora y la posición.
2. *Mute* ⇒ el nodo deja de emitir y en su pantalla sale el aviso; *unmute* lo devuelve.
3. *Reiniciar* ⇒ el nodo se reinicia, **el puerto se re-enumera y la app se reconecta sola**.
4. Escribir en la consola `status` y `bat` y ver las respuestas de texto.
5. Escribir un comando que no existe ⇒ respuesta de error del nodo, sin que la app se aturda.
**Qué demuestra**: que la app hace todo lo que hace la web, salvo el registro y el mapa.

---

### Fase 5 — **La prueba de fuego**: convivir con el TNC del operador
Esta fase no añade funciones: **comprueba que no rompemos nada del operador**, y por eso va antes que el
registro y el mapa.

**Montaje**: el nodo en modo **KISS** (`tncProtocol = 2`), con **APRSdroid** ya conectado por OTG y
funcionando (recibiendo y transmitiendo). Entonces se abre **nuestra app**.

**Antes de ese montaje, un banco más barato**: existe ya **`tools\kiss_client.js`**, un cliente KISS que
habla con el nodo **desde el PC** (presenta las tramas como líneas APRS y sabe mandar tramas al aire). Sirve
para tener el puerto **lleno de tráfico binario de verdad** sin depender del móvil, y así afinar primero la
Regla 1 (el `0xC0` manda) con el cable en la mesa. Es el mismo camino que ya usó este proyecto para probar
KISS, y ahorra tener que perseguir dos fallos a la vez.
**Se prueba y se exige**:
1. Al arrancar, la app **detecta KISS** por `status.radio.tnc` y **enseña el cartel**, sin mandar nada más.
2. **APRSdroid no pierde el TNC ni ve basura** durante todo el rato que la app está mirando.
3. La app **no manda ni una orden** que no sea `status` (se comprueba en el registro de viaje del nodo,
   `log` en la consola, línea a línea).
4. Al pulsar *"dejar paso a la app"* y escribir `CONFIRMAR`: el nodo obedece a la app, la app guarda un
   cambio, y **el cambio se ve en el `get` siguiente**.
5. Al devolver el mando (`kisson`) o reiniciar: **APRSdroid vuelve a funcionar**.
6. **Y el caso peor, a propósito**: matar la app **en mitad de un comando** y volver a entrar. El nodo no
   se puede quedar sordo (el firmware aguanta media trama KISS abandonada con un temporizador de 1 s,
   `kiss.cpp:97-106`, y descarta media línea de texto con el `\n`). El operador tiene que poder seguir
   hablando con su nodo **sin desenchufar**.
**Qué demuestra**: que se puede convivir con el módem. Si esto no pasa, **el resto del plan no vale**:
una app que deja al operador sin TNC es peor que no tener app.

---

### Fase 6 — Registro de viaje y exportaciones
**Se construye**: descarga del registro (`log dump`), el parser portado, la separación por sesiones (un
arranque = un track), el resumen (puntos, distancia, repetidos, recepciones) y exportar a GPX/KML/CSV.
**Se prueba primero sin teléfono**: `tools\prueba_registro.js` corriendo sobre **el mismo registro real**
(`_trabajo_ea2oy\paseo_20260911.txt`, y también `tools\ejemplo_registro_crudo.txt`, que es un volcado
grande), y comparando el GPX/KML/CSV con el que saca hoy la web: **tienen que salir iguales**. Como hay
**dos** implementaciones ya hechas de esa conversión (la del configurador y `tools\log2gpx.js`), la de la
app sería **la tercera**: si las tres coinciden sobre el mismo registro, la portería está bien.
Después, en el móvil: descargar el registro de un paseo de verdad y exportar los tres formatos, abriendo
el GPX en otra app de mapas.
**Qué demuestra**: que los datos del viaje salen del móvil y sirven para Wikiloc/Google Earth/Strava.

**Dos detalles del registro que hay que respetar** (comprobados en `flog.cpp:317-334`):
1. **Entre `LOG BEGIN` y `LOG END` sólo se habla de eso.** El nodo está volcando el registro entero como
   líneas de texto, así que **mientras dura la descarga no se manda nada más** (ni el sondeo de `status`):
   si se cruzara una respuesta JSON en medio, el volcado se leería mal y el registro saldría con basura.
   La web ya lo hace así (`finishLog`, `index.html:3446`).
2. **Con KISS activo, el registro no se descarga.** Si una trama KISS entra a mitad del volcado, corta el
   flujo (es lo que manda el `0xC0`). La app **lo dice** en vez de enseñar un registro a medias: *"el nodo
   está haciendo de módem; para leer el registro, pausa KISS"*. Es la misma regla de convivencia del §4.2.

---

### Fase 7 — Mapa
**Se construye**: el mapa con teselas de OpenStreetMap y la ruta dibujada encima (misma proyección, ya
probada en `tools\prueba_mapa.js`).
**Se prueba**: ver la ruta de una salida real, encuadrada sola, con los marcadores A/B y los puntos; y
**sin datos móviles** (tiene que seguir dibujando la ruta aunque no haya fotos del mapa, como hace la web).
**Qué demuestra**: la última función del configurador que faltaba. **Ojo**: es lo único que necesita
**permiso de INTERNET**. Si el operador prefiere una app que no pida internet, esta fase se cae y se queda
en exportar el GPX y verlo en otra app (§9).

---

### Fase 8 — Cierre: estado remoto, manual y publicación (si toca)
**Se construye**: el `RemoteControlReceiver` (mando por `adb` que deja el estado en un fichero: es *"su gran
acelerador de pruebas"*, ficha N-13 error nº 49), la pantalla de *"acerca de / qué versión llevo"*, la
auditoría de secretos, y —**sólo si el operador decide publicar**— la ficha de F-Droid / GitHub con el
`metadata.yml` y el `fastlane`.
**Se prueba**: instalar en los dos teléfonos, revisar la interfaz en las dos densidades, y la publicación
(bajar el APK publicado y comprobar su SHA-256 contra el local, que es lo que hace el proyecto hermano).

---

## 8. Riesgos conocidos y cómo se esquivan

Los saco, en su mayoría, de los **51 errores ya cometidos** que están en la ficha N-13 y de las trampas de
la N-15. No se repiten: se esquivan por diseño.

### Riesgos que pueden hacer daño de verdad

| # | Riesgo | Cómo se esquiva |
|---|---|---|
| 1 | **Pisar el TNC del operador** (mezclar nuestras órdenes y respuestas con las tramas KISS de APRSdroid; el otro programa puede interpretar mal esa mezcla) | §4 entero: leer `status.radio.tnc`, quedarse en modo mirón, y salir de ahí **sólo** con confirmación escrita por la puerta que el firmware ya tiene (`kissoff`, que es en RAM y no cambia el ajuste). **Y probarlo en banco antes de seguir** (Fase 5) |
| 2 | **Reiniciar la placa sin querer** al abrir el puerto | Comprobado: el reinicio a modo grabación sólo pasa **al cerrar a 1200 baudios**. Se abre a **115200** y **no se toca el toque de 1200** salvo que el operador lo pida, avisando |
| 3 | **Mandar los 32 bytes `0xC3`** de la app hermana | **No se copian.** En nuestro firmware son 32 caracteres basura que ensucian el buffer y provocan un `bad json` en la primera orden |
| 4 | **Contestar `ok` sin haber guardado** (el fallo clásico: el operador se va tranquilo y el ajuste sigue igual) | Se enseña **la lectura de vuelta** que manda el nodo, no lo que el operador escribió. Y el `ignorado (tipo incorrecto)` se pinta como **guardado parcial**, nunca como éxito limpio |
| 5 | **Borrar una sección de la configuración** | Se manda **el bloque completo** (que es lo que hace el firmware bien: merge campo a campo). Nunca una actualización parcial |
| 6 | **Una línea JSON de más de 4096 bytes** (límite real del firmware, no los 1024 del documento) | Se comprueba el tamaño antes de mandar y, si no cabe, se dice. Y **se corrige `protocol_config_v1.md`** cuando se toque |
| 7 | **Dar el enlace por muerto cuando sólo ha hipado** | El nRF52840 da errores de lectura transitorios ("USB get_status request failed"): **5 errores seguidos** antes de declarar muerto el enlace, con 100 ms entre intentos (patrón ya probado) |
| 8 | **Bloqueo con la app en segundo plano** | Al pasar a segundo plano **se suelta el cable** (para que APRSdroid pueda usarlo) y se vuelve a conectar al primer plano, con una guarda `appEnBackground` para que la reconexión automática no pelee con la liberación |
| 9 | **Cierre de app al conectar** en Android 14+/16 | `openDevice`/`claimInterface`/`setParameters` lanzan `SecurityException`/`IllegalStateException`: **capturar `Exception` genérica** y contarlo, no sólo `IOException` |
| 10 | **Diálogo de permiso USB en cada relanzamiento** (MIUI/HyperOS) | **No es un fallo**: el enlace OTG se re-enumera. La app lo **explica en pantalla** en vez de parecer rota |
| 11 | **"No USB driver found" tras reiniciar el nodo** | El reinicio re-enumera el bus y el aparato recordado queda obsoleto: **redescubrir** y conectar al primero permitido, pidiendo permiso una vez por ciclo |
| 12 | **Mojibake** (acentos y emojis rotos) | Herramienta de edición nativa, **nunca PowerShell** para escribir UTF-8 (`Get-Content`+`WriteAllLines` decodifica como cp1252 y **doble-codifica el fichero entero**: así se corrompió un `.kt` de 8.000 líneas y el cerebro se vació dos veces). Todo literal visible a `strings.xml`, y el verificador **ampliado** (`C3 B0` y `C5 B8`, no sólo `C3 83`/`C3 82`) |
| 13 | **Fichero escrito y vacío** (fallo silencioso) | **Comprobar el tamaño inmediatamente** después de cada escritura, y `backup.ps1` con retención rodante (nunca un `.bak` único: *"un backup tomado después de la escritura corrupta es él mismo corrupto"*) |
| 14 | **Apóstrofo crudo en `strings.xml`** | Rompe `aapt2` con un error que **señala a un fichero y una línea que no son**. Regla: `\'` siempre, `</string>` cerrado, y **comprobar en limpio** (las builds incrementales lo enmascaran) |
| 15 | **`CalledFromWrongThreadException` / pantalla congelada** | Todo lo que toque la interfaz va dentro de `runOnUiThread`, **incluidas las ramas de error y de callback** (no sólo el camino feliz) |
| 16 | **Temporizadores rehenes del sistema** (con la escala de animación a 0, los avisos "pasan a toda velocidad") | Para cualquier cuenta atrás: **`Handler` + `SystemClock.uptimeMillis()`**, no `ValueAnimator` |
| 17 | **Secretos en el repositorio** | El `local.properties` de la app hermana **trae la contraseña del keystore en claro** (comprobado): **eso no se copia**. La contraseña va por variable de entorno o se pide en el momento. `local.properties` y el keystore, **gitignored**. Nada de secretos en el Cerebro ni en el chat |
| 18 | **El APK se hincha** | Los PDFs sin optimizar les llevaron el APK a 11,2 MB (con PDFs optimizados: 6,46 MB). **Cuidado con lo que se mete en `assets\`** — y los `.uf2` **no** entran |
| 19 | **Romper la app instalada al publicar** | **No se renombra el `applicationId`** después de la primera publicación: deja huérfana la app instalada. Y **la firma se elige ANTES** de la primera publicación: el dueño del APK viejo firmado con la clave de depuración **no puede actualizar** (les pasó de v1.0.0 a v1.0.1) |
| 20 | **Perder el tiempo con herramientas que en MIUI no van** | `input tap` y `uiautomator` **fallan** en MIUI sin el interruptor *"USB debugging (Security settings)"*, y la barra de pestañas ignora los toques en las flechas (hay que **deslizar**). Para capturas: `screencap` en el dispositivo + `adb pull` (con `>` desde PowerShell **se corrompen los binarios**) |

### Riesgos menores, pero que hay que apuntar

- **El volcado del registro y el sondeo no pueden coincidir**: mientras el nodo escupe `LOG BEGIN … LOG END`
  no se le puede mandar nada más, o el volcado se lee mal. La app **para el sondeo** durante la descarga
  (igual que la web) y **no descarga el registro si hay KISS activo** (§Fase 6).
- **Un solo dueño del cable**: Android no comparte el USB. Si APRSdroid lo tiene, la app no puede abrirlo
  y lo dice. **No se puede "convivir", sólo turnarse.**
- **`ProbeTable` por reflexión**: la librería reconoce nuestro nRF52840 **por clase de interfaz** (CDC-ACM),
  no por identificador — y `device_filter.xml` **no lleva `0x239A`**, así que **enchufar el nodo y que salte
  la app sola no ocurrirá**; el botón Conectar sí funciona. Es una decisión, no un fallo (§9.5).
- **El PID real de nuestra placa** (¿`0x00B3`? ¿`0x8029`?) no se sabe sin el aparato delante.

---

## 9. Qué NO hacer

1. **No tocar el Bluetooth.** Está **fuera de la compilación** del firmware desde el 2026-09-13 porque al
   enlazarlo el nodo se quedaba **sin pantalla, sin radio y sin USB**. La sección del configurador está
   marcada `inert` a propósito. **La app no lleva ni una línea de Bluetooth, ni permisos de Bluetooth**,
   ni aunque el firmware lo recupere algún día: eso sería otro trabajo con su propia prueba en banco.
   Si alguien pide "que también vaya por Bluetooth", la respuesta es no, y el motivo es que **hoy no
   funciona en el firmware**.
2. **No inventar rangos, listas ni valores por defecto.** Todo sale del `SCHEMA` y de `config.cpp`.
   El formulario no puede ofrecer nada que el nodo vaya a rechazar (el operador rellenaría todo el
   formulario para comerse un error al guardar: ya pasó con la baliza en 10 y con la telemetría).
3. **No filtrar los aparatos por identificador de USB.** El nRF52840 Adafruit (VID `0x239A`) **no está**
   en el filtro de la app hermana y **conecta igual**, porque el descubrimiento real es *"el primer
   aparato serie que aparece"* y el driver CDC-ACM es genérico por clase. Restringir por VID fue
   exactamente el error que les dejó un aparato invisible (ficha N-13, error nº 31).
4. **No mandar los 32 bytes `0xC3`**, ni auto-pedir la configuración como hace Meshtastic, ni copiar su
   entramado: **nuestro protocolo es otro**.
5. **No interpretar como respuesta cualquier JSON que llegue.** El botón físico del nodo mete JSON en el
   cable cuando le da la gana.
6. **No tocar las vistas desde el hilo de lectura.**
7. **No hacer nada destructivo sin la puerta CONFIRMAR**, y **no automatizar los destructivos** en las
   baterías de prueba: `wipe`, `factory_reset`, `dfu` y `reboot` se prueban **a mano**, uno a uno.
8. **No inventar datos que el nodo no manda.** Si el GPS no tiene hora, el registro va **sin hora** (no se
   rellena con la del móvil); si no hay sensor, ese dato no se pinta (ni un cero fingido). *"Nunca publica
   una posición que no sea real"* es norma del firmware, y la app no la va a estropear.
9. **No reescribir** el configurador web, `src\`, ni las fichas del Cerebro: este plan **no toca nada**.
10. **No copiar `MainActivity.kt` (8.202 líneas)** ni nada de su capa Meshtastic/BLE/protobuf.
11. **No prometer el flasher**: Android no puede montar la unidad de memoria de la placa sin root.
12. **No añadir el permiso de INTERNET hasta la fase del mapa**, y sólo entonces. Hoy la app no necesita
    internet para nada, y eso es un argumento (y una tranquilidad).
13. **No publicar en Play** sin decidirlo antes: la cuenta cuesta 25 $ y hacen falta 12 probadores durante
    14 días. F-Droid es gratis pero **la firma la hace F-Droid** y **no se puede cambiar después**.
14. **No meter secretos** en el repositorio, en el Cerebro ni en el chat.

---

## 10. Decisiones que tiene que tomar el operador

Estas no las decido yo. Las de arriba (§3–§9) son propuestas; **estas son suyas**, y algunas hay que
contestarlas **antes de la Fase 0** porque cambian el trabajo.

**Antes de empezar (bloquean la Fase 0):**

1. **¿Cómo se llama la app y qué icono lleva?** (el nombre que se ve en el móvil; se puede cambiar después,
   pero el icono y el nombre son lo que el operador ve cada día). *Propuesta provisional para poder
   empezar: "APRS EA2OY".*
2. **¿Cuál es el identificador del paquete?** (`applicationId`, por ejemplo `com.ea2oy.aprsconfig`).
   **Ojo: esto NO se puede cambiar después de la primera publicación** sin dejar huérfana la app instalada
   de quien ya la tenga. *Propuesta: `com.ea2oy.aprsconfig`.*
3. **¿Se publica o es de uso propio?** Esto cambia tres cosas de golpe:
   - Si **se publica**: hay que decidir **la firma ANTES del primer APK que salga a la calle** (no se puede
     cambiar después), y como el proyecto es **GPL-3.0**, **el código hay que publicarlo**. Habría que
     elegir también el canal: **GitHub Releases** (lo más simple y lo que ya hacen), **F-Droid** (gratis,
     pero su firma y sus reglas) o **Play** (25 $ + 12 probadores 14 días).
   - Si es **de uso propio** y no sale del teléfono del operador: no hay que publicar nada, la firma puede
     ser la de depuración, y `backup.ps1` + el APK en una carpeta es toda la distribución que hace falta.
   *Mi recomendación, si no hay una razón para lo contrario: **uso propio primero**, APK firmado con una
   clave propia guardada fuera del repositorio, y decidir la publicación cuando la app lleve un tiempo
   funcionando.*
4. **¿Quién guarda la contraseña del keystore y dónde?** (no en el repositorio). Y si **no** se va a
   publicar, ¿se firma igual con una clave propia —para poder actualizar sin desinstalar— o se queda en
   depuración?
5. **¿Qué versiones de Android hay que cubrir?** *Propuesta: **Android 8.0 (API 26)** como mínimo* (cubre
   ~99 % de los móviles) **y `targetSdk 35`**. Aviso de coste real: subir a 35 **obliga** a tratar los
   márgenes del sistema (edge-to-edge) en todos los paneles; **es un trabajo aparte, no un número**.
   Y aviso de que **hay que probar en los móviles que haya en casa**: la compatibilidad se mide, no se supone.

**Antes de la Fase 5 (la del TNC):**

6. **¿Vale la regla de convivencia propuesta?** Resumen: *la app mira pero no toca; para guardar hay que
   escribir `CONFIRMAR` y eso pausa KISS en RAM hasta que se devuelva el mando o se reinicie el nodo.*
   La alternativa sería **no permitir nada** mientras KISS esté activo (más seguro todavía, pero obliga a
   ir a la pantalla del nodo a apagarlo). *Recomiendo la propuesta: la pausa ya existe en el firmware y es
   reversible sin perder el ajuste.*
7. **¿Con qué programa TNC se hace la prueba de la Fase 5?** (APRSdroid es el caso normal; conviene tener
   el que use el operador de verdad, porque es el que hay que no romper).

**Antes de la Fase 7 (el mapa):**

8. **¿Se quiere mapa dentro de la app?** Es lo único que exige **permiso de INTERNET** y descargar teselas
   de OpenStreetMap (cuya política **no permite el uso sin conexión**, y pide que las peticiones lleven
   referente válido: la web ya lo tiene resuelto y avisado). **La alternativa** es no tener mapa en la app
   y exportar el GPX/KML y verlo en Wikiloc, Google Earth o el visor que el operador ya use. *Si el
   operador prefiere una app que no pida internet, esta fase se cae y el plan queda más corto.*

**Antes de la Fase 8:**

9. **¿Cómo se entera el operador de qué versión lleva?** (pantalla *"acerca de"* con versión y fecha de
   compilación visible; *propuesta: sí, como en la app hermana*).
10. **¿Se copia el diseño de la app hermana (colores, pestañas, ayuda a pulsación larga) o se hace una
    interfaz propia?** *Propuesta: mismo espíritu —textos claros, ayuda en cada casilla, avisos que no
    bloquean— pero sin copiar pantallas.*
11. **¿Hace falta manual en PDF?** (la app hermana lo genera con pandoc + xelatex y lo embebe; cuidado: los
    PDFs sin optimizar inflaron su APK hasta 11,2 MB).
12. **¿Se quiere el mando por `adb`** (el receptor de control remoto, para probar sin tocar la pantalla)?
    Es una ayuda para nosotros, no una función para el operador, pero es un acelerador grande.

**Y una que conviene contestar pronto, aunque no bloquee:**

13. **¿Se acepta que la app se quede con una copia local de la última configuración leída**, para poder
    comparar y volver atrás? (Ojo con la privacidad: ahí van coordenadas, indicativo y la lista de
    operadores autorizados; **no sale del móvil** y se puede borrar con un botón).

---

## 11. Lo que NO he podido comprobar (y por tanto no prometo)

Honestidad por delante. Esto **no** está verificado y no se debe dar por bueno hasta que haya un aparato
delante:

1. **No he ejecutado nada contra un nodo real.** Todo lo del protocolo está leído del código, no medido en
   el cable.
2. **Nada compilado ni instalado**: no he construido un APK ni he probado la librería serie en un móvil.
   Lo de la Fase 1 es un plan, no un resultado.
3. **El comportamiento real de APRSdroid cuando le llegan líneas de texto entre tramas KISS**: el firmware
   lo hace **a propósito** (`protocol.h:3-7`), y el documento dice que *"una app KISS debe ignorar todo lo
   que no empiece por FEND"*; **pero eso no lo he visto funcionar**. Es exactamente lo que comprueba la
   Fase 5, y por eso la Fase 5 no se salta.
4. **Si nuestra placa se reinicia al abrir el puerto**: comprobado que el reinicio a modo grabación **sólo**
   va por el cierre a 1200 baudios; a 115200 no debería pasar nada, pero **no lo he medido**.
5. **El PID exacto que presenta la placa** (los `boards\*.json` declaran `0x00B3`, `0x8029`, `0x0029`,
   `0x002A`, `0x802A`; el bootloader se presenta como `239A:000B`). Hace falta el aparato para saberlo.
6. ~~Los cuatro `.js` de comprobación: no los he ejecutado.~~ **Ya comprobado** (ver §3.3): tres pasan y
   uno (`prueba_avisos.js`) falla por cómo extrae el código, con el motivo identificado. Lo que **sigue sin
   comprobar** es que el Node de otro equipo los corra: aquí hay **Node v24.20.0** y funcionan, y **no
   necesitan ninguna dependencia instalada** (sólo módulos internos), así que deberían funcionar en
   cualquier Node moderno.
7. **Cuánto de la librería serie habría que retocar para el USB nativo del ESP32-S3** (`0x303A`): no aplica
   hoy (nuestro nodo es nRF52840), pero si algún día se configura el iGate ESP32 por USB, es otro trabajo.
8. **No he leído** `MainActivity.kt` entero (8.202 líneas) ni su capa BLE: he ido a los puntos de USB,
   permiso, `device_filter`, firma y publicación. Puede haber más lógica de USB en zonas no visitadas,
   aunque el rastreo por `usb|serial|baud|DTR|RTS` está agotado.
9. **El estado de su publicación** (si la app hermana está en Play o si F-Droid la ha aceptado) no consta:
   no afecta a este plan, pero conviene saberlo antes de copiar su `metadata.yml` como plantilla.

---

## 12. Resumen en una página

- **Se copia tal cual**: la **librería de puerto serie** (19 ficheros, MIT, 162.825 bytes), los cuatro
  `.ps1` del taller (JDK, Gradle, compilar, **backup**), el `device_filter.xml`, y **patrones** (permiso USB
  con `FLAG_MUTABLE` + `.setPackage()`, bucle de lectura tolerante, `setDTR`/`setRTS`, firma desde
  `local.properties`, receptor de control remoto, plantilla de F-Droid si se publica).
- **Se reescribe**: **toda** la capa de protocolo (líneas JSON, no protobuf), **el entramado KISS de
  lectura**, el modelo de configuración y el formulario **generados desde el `SCHEMA`**, toda la interfaz
  (nada de un `MainActivity` de 8.200 líneas), el flasher (no se puede), la matriz de iconos APRS (se saca
  de **nuestro** configurador) y los valores recomendados.
- **Se reutiliza de nuestra propia web, con sus pruebas ya escritas**: el parser del registro de viaje, los
  exportadores GPX/KML/CSV, la proyección del mapa y los avisos.
- **La regla que lo gobierna todo**: el mismo puerto lleva **JSON, consola de texto y KISS binario**. La app
  **lee los tres**, **no interpreta nunca el binario**, **mira `status.radio.tnc` antes de escribir nada**,
  y sólo sale del modo mirón **con confirmación escrita** y por la puerta que el firmware ya tiene.
- **Ocho fases**, cada una probada en un teléfono: esqueleto → **hablar con el nodo** → leer la
  configuración → guardar → acciones y consola → **convivir con el TNC** → registro y exportaciones →
  mapa → cierre.
- **Lo que decide el operador**: nombre y paquete de la app, si se publica y con qué firma, qué Android
  mínimo (propuesta: 8.0), si quiere mapa (es lo único que pide internet), y si acepta la regla de
  convivencia con el TNC.
- **Lo que no se hace**: Bluetooth (está apagado a propósito), inventar rangos, filtrar por identificador
  de USB, mandar los 32 bytes `0xC3`, copiar su capa Meshtastic/BLE, y prometer un flasher que Android no
  puede hacer.

*73 de EA2OY.*

---

# DECISIONES TOMADAS POR EL OPERADOR (2026-09-16)

Estas tres respuestas **desbloquean la fase 0** y condicionan el resto del plan:

1. **Se PUBLICA** (F-Droid y/o Play). Consecuencias que hay que respetar desde el primer dia:
   - **La firma se elige ANTES del primer APK.** Si se cambia despues, quien tenga la app instalada NO podra actualizarla (le paso a la app hermana con un cambio de firma debug->release).
   - La licencia es **GPL-3.0**: publicar la app obliga a **publicar su codigo**.
   - Hay que mantener ficha de tienda, ficha de privacidad y capturas. Las reglas de F-Droid ya estan masticadas en N-13.
2. **Nombre visible: "APRS LoRa EA2OY"**. El **identificador interno** (pplicationId) sigue PENDIENTE de decidir y **no se puede cambiar nunca** una vez publicado.
3. **CON MAPA desde el principio** (no en la fase 7). Consecuencia importante: es **lo unico que obliga a pedir permiso de INTERNET**, asi que la app deja de ser "todo por cable". Conviene decidir si el mapa se carga solo cuando el usuario lo pide, para que la app pueda vivir sin red si no se abre esa pantalla.

## Lo que sigue bloqueando la fase 0
- pplicationId definitivo (propuesta: `com.ea2oy.aprslora`).
- Quien guarda la contrasena del almacen de firmas y **como** (nunca en el repositorio).
- Version minima de Android (propuesta del plan: **Android 8.0**, con aviso de que apuntar a la 35 es trabajo aparte por el tema de los margenes de pantalla).
---

# ★ DOS NUMEROS DISTINTOS QUE NO HAY QUE CONFUNDIR (aviso del operador, 2026-09-16)

El plan los mezclaba en su seccion de decisiones. **No son lo mismo:**

- **`minSdk` = a que moviles soporta la app.** Propuesta: **26 (Android 8.0)**, que cubre practicamente todo lo que hay en uso y es lo que la app hermana tiene probado en tres telefonos.
- **`targetSdk` = a que version de Android se adapta la app.** **Google Play EXIGE un minimo para PUBLICAR**, no es opcional y **sube cada ano** (hacia agosto: las apps nuevas y las actualizaciones tienen que apuntar a una API reciente). **F-Droid no impone ese requisito**, asi que si solo se publicara alli habria mas libertad.

**★ SUBIR EL `targetSdk` NO ES CAMBIAR UN NUMERO: ES TRABAJO.** A la app hermana, subir del 34 al 35 le obligo a adaptar los margenes de pantalla (dibujar por detras de las barras del sistema y gestionar los recortes). Hay que presupuestarlo como una tarea, no como un ajuste.

**RECOMENDACION**: nacer ya con el `targetSdk` que exija Google en ese momento (a fecha de este plan, lo previsible es **API 36**), aunque cueste un poco mas al principio, **para no tener que rehacer la interfaz** en cuanto la tienda suba el liston.

**[NO VERIFICADO]**: no se ha podido leer el numero exacto que Google exige hoy (su pagina oficial carga el detalle con scripts y no se deja leer). **Hay que confirmarlo en la consola de Play al abrir la ficha** y anotarlo aqui antes de generar el primer APK de publicacion.

# DECISIONES CERRADAS (2026-09-16)
- **`applicationId`: `com.ea2oy.aprslora`** (aceptado por el operador). El plan proponia antes `com.ea2oy.aprsconfig`: **manda esta**.
- **La contrasena de firma NO va NUNCA al repositorio**, ni al publico ni al privado.
- Nombre visible: **APRS LoRa EA2OY**. Se publica en tienda. Con mapa.