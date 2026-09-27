# T-ECHO PROJECT BUTTER II — el USB también se lee mientras el panel pinta

Fecha: 2026-09-16 · Placas: **LilyGO T-Echo** y **T-Echo Plus** (misma pantalla de tinta,
mismos dos botones) · Entornos: `techo_s140v7`, `techo_plus_s140v7` (y los dos Faketec, que
comparten todo este código).

Este documento es la continuación del **T-Echo Project Butter** (ver
`docs/PROYECTO_BUTTER_TECHO.md`), que arregló los toques. Aquel dejó el botón leyéndose
durante el repintado; este deja **el puerto USB leyéndose durante el repintado**. Todo lo
que se afirma aquí es «el código hace esto» o «el banco mide esto»: **no había ninguna placa
conectada**, así que en ningún sitio se dice «va más rápido».

---

## 1. El problema, con el número que lo explica

El parser del USB se alimentaba **solo desde el bucle** (`main.cpp` →
`gProtocol.feed(Serial)`) y el bucle se pasa **1,5-3 s** dentro del driver de la tinta
esperando al panel (`epdEsperaPintado()`). En ese rato el puerto no lo leía nadie.

Lo que hace que eso sea grave (y no solo un retraso) es el tamaño del FIFO del USB:

- El `Serial` de este core es un **CDC de TinyUSB**, y su FIFO de recepción son
  **256 bytes**: `CFG_TUD_CDC_RX_BUFSIZE 256` en
  `libraries/Adafruit_TinyUSB_Arduino/src/arduino/ports/nrf/tusb_config_nrf.h`
  (framework-arduinoadafruitnrf52 del core que compila este proyecto).
- El bucle vacía ese FIFO **una vez por vuelta**, en la cabecera (`while (s.available())`).
  Mientras el FIFO está lleno, el USB retiene al que escribe (el endpoint contesta NAK): el
  `write()` del ordenador se queda esperando.
- El `set` del configurador es **la configuración entera en una sola línea JSON**: ~1320
  caracteres medidos en este proyecto (de ahí que el tope de línea se subiera a 4096). Con
  los cuatro perfiles se va a **~1,4 KB**.

O sea: una línea de 1,4 KB **no entra en una vuelta del bucle**. Hacen falta
`1400 / 256 ≈ 6` vueltas para juntarla, y cada vuelta puede llevar por delante **otro
repintado de 1,5 s** (con tráfico APRS el panel está sucio a menudo: cada RX/TX son dos
refrescos). Eso es lo que el operador vive como «mando algo y el nodo tarda una eternidad».

## 2. Qué se ha cambiado

La idea es **el mismo patrón que el del botón**: **LEER Y ENCOLAR, NO EJECUTAR**. El driver
de la tinta, en sus esperas, ya bombeaba el botón (`bombeaEsperaPantalla`); ahora ese mismo
gancho **también lee el puerto y encola los bytes**. La ejecución la sigue haciendo el bucle,
como antes. Ni un comando se obedece dentro del driver: no se puede pintar dentro de un
pintado ni escribir la flash en medio de una transacción con el panel.

### Ficheros nuevos

| Fichero | Qué es |
|---|---|
| `src/usb_lector.h` | El **buzón de bytes** (anillo de 4096) y el **troceado en líneas**. C++ puro (ni Arduino ni `String`), para que el banco de pruebas compile **el mismo fichero**. |
| `src/usb_lector.cpp` | Las tres funciones: `bombea()` (LEE Y ENCOLA), `atiende()` (EJECUTA) y `meteByte()` (el ÚNICO sitio que interpreta bytes). |
| `tools/banco_usb/` | El banco de pruebas: `banco_usb.cpp` (el guion y el modelo del bucle), `lector_antes.cpp` (el `feed()` de git), `ejecuta_banco_usb.ps1`, `baja_zig.py`, `salida_banco_usb.txt`. |

### Ficheros tocados

| Fichero | Líneas | Qué |
|---|---|---|
| `src/usb_lector.h` | 62-101 | `class UsbLector`: anillo `kCap = 4096`, línea `kMaxLinea = 4096`, `bombea()`, `atiende()`, `marcaEnPantalla()` y los contadores. |
| `src/usb_lector.cpp` | 25-47 | `bombea()`: saca bytes del puerto y los deja en el anillo **en orden**. Si el anillo se llena, **deja de leer** (los bytes se quedan en el FIFO del CDC: no se pierde ni uno). |
| `src/usb_lector.cpp` | 49-71 | `atiende()`: saca del anillo y se los da a `meteByte()`. **Solo lo llama el bucle.** |
| `src/usb_lector.cpp` | 73-99 | `meteByte()`: el único sitio que interpreta bytes (regla del `0xC0`, CR, LF, tope de línea). |
| `src/protocol.h` | 73-84 | Las dos puertas: `feed()` (bucle: leer **y ejecutar**) y `bombea()` (driver: leer **y encolar**), `atiende()` y `marcaEnPantalla()`. |
| `src/protocol.h` | 97-112 | Aquí vivía `String lineBuf_`; ahora el acumulador de línea es el del lector (un solo acumulador para el bucle y para el driver). |
| `src/protocol.cpp` | 155-168 | `ConfigProtocol::bombea(Stream&, bool)`: los dos thunks del puerto y el bombeo. |
| `src/protocol.cpp` | 171-183 | `ConfigProtocol::feed()`: `tncUsbPoll()` + leer + ejecutar (idéntico orden que antes). |
| `src/protocol.cpp` | 188-209 | `lineaRecibida()` y los ganchos: primero el TNC (trama TNC2), después el parser JSON/CLI. |
| `src/protocol.cpp` | 25-41, 113-119, 245-255 | Contadores del bombeo y `usbBombeoResumen()`; se publican en `status.usb` (`bombeo`, `colaMax`, `cola`, `dentro`, `tirados`). |
| `src/main.cpp` | 93-123 | El gancho `bombeaEsperaPantalla()`: **botón + USB**, y el aviso de que aquí NO se ejecuta nada. |
| `src/main.cpp` | 272-274 | `displaySetPumpBoton(bombeaEsperaPantalla)`. |
| `src/main.cpp` | 569 | `marcaEnPantalla(false)`: el repintado ha terminado. |
| `src/main.cpp` | 577-583 | `gProtocol.atiende()` justo después de `displayRefresh()`: **aquí se cobra** lo que llegó durante el repintado (mismo sitio donde se cobra el gesto del botón). |
| `src/cli.cpp` | 22, 173, 228-229, 279-288 | Comando de taller **`usb`** (una palabra) con las cifras del bombeo. |
| `src/display.h` | 100-101 | El comentario del gancho, actualizado (ya no bombea solo el botón). |

**Lo que NO se ha tocado**: los tiempos del panel y su lógica (completo/parcial, huella de
contenido, aplazamiento del repintado), el menú, la sesión «Fijar coords», la confirmación de
15 s, el carrusel, la lectura del botón (`button.cpp`), `kiss.cpp`, `tnc.cpp` y los
borrados/escrituras de flash de siempre.

## 3. Cómo se evita la reentrada (el patrón, punto por punto)

1. **Dos puertas, y solo una ejecuta.**
   - `feed(Stream&)` — bucle: `tncUsbPoll()` → leer → **ejecutar**. Es la única puerta que
     obedece un comando, y la llama solo `main.cpp` (cabecera del bucle y después del
     repintado).
   - `bombea(Stream&, bool)` — driver: lee el puerto y **encola bytes**. No interpreta una
     sola línea.
2. **Un solo acumulador y un solo sitio donde se parten las líneas.** El anillo (`anillo_`),
   la línea a medias (`linea_`) y el troceado viven **en un único objeto** (`UsbLector`) que
   comparten el bucle y el driver. No hay dos parsers leyendo el mismo puerto: el byte que
   lee el driver por la mañana es el mismo byte que el bucle interpreta después, en orden.
3. **La ejecución no ocurre dentro del driver ni por accidente.** El gancho solo puede
   llamar a `bombea()`, y `bombea()` no llama a `meteByte()` ni a `atiende()`
   (estructuralmente: no hay ninguna ruta). Un comando que dispararía un repintado
   (`displayPopup`, cambio de escena, «Fijar coords»…) se ejecuta **fuera** del pintado, en
   `atiende()`, cuando el panel ya está libre y sus transacciones han terminado.
4. **Red de seguridad visible en la placa.** `marcaEnPantalla(true)` la pone el gancho y
   `marcaEnPantalla(false)` la quita el bucle en cuanto `displayRefresh()` vuelve. Si algún
   día alguien ejecutara un comando con la marca puesta, el contador `dentro` subiría y se
   vería en `status.usb.dentro` y en el comando `usb` (`REENTRADA`). **Tiene que ser 0.**
5. **El banco lo comprueba, y además comprueba que sabría detectarlo.** El banco se compila
   en tres versiones: el código anterior (`lector_antes.cpp`), el código nuevo **real**
   (`src/usb_lector.cpp` + `src/kiss.cpp`) y un **contra-ejemplo** que ejecuta desde el
   gancho (`-DBANCO_INGENUO`). En el bueno: `0` ejecuciones dentro del repintado en los siete
   escenarios. En el contra-ejemplo: **`1`** (y `1` trama KISS entregada dentro del
   repintado, que en el firmware de verdad sería **transmitir por radio mientras se pinta**).
   Sin ese contra-ejemplo, el `0` no valdría nada.

## 4. El modo KISS: la regla del `0xC0` no se ha tocado

- **El byte se le ofrece al TNC en el mismo sitio y en el mismo orden que antes**: la
  llamada a `tncHandleUsbByte()` está en `UsbLector::meteByte()`, antes que nada, y es
  literalmente la misma que hacía `feed()`. Si el byte entra en una trama KISS,
  **no se interpreta como texto** y la línea a medias muere con la trama
  (`nLinea_ = 0`, que era el `lineBuf_ = ""` de antes).
- **`kiss.cpp` y `tnc.cpp` no se han tocado**: ni el autómata, ni el abandono de media trama
  (`tncUsbPoll()` / `kissPoll()`, sigue en `feed()`), ni el manejador de tramas.
- **Lo que cambia es solo CUÁNDO se le entregan los bytes**: el driver los saca del puerto
  durante el repintado y el bucle los mete por el autómata al acabar. Como el autómata es el
  mismo y el orden es el mismo, el resultado es el mismo.
- **Medido con el `kiss.cpp` de verdad** (no una copia), con una trama que lleva **un `0x0A`,
  un `0xC0` y un `0xDB` dentro** (los dos últimos escapados):
  - la trama llega **INTACTA** (20/20 bytes del cuerpo), **una sola vez**;
  - **0 líneas de texto** salen de ese binario (el `0x0A` de dentro no parte nada);
  - se entrega **fuera** del repintado (t=1500 ms, al acabar), nunca durante;
  - y el escenario 7 (trama + línea JSON en el mismo golpe) da **exactamente lo mismo antes y
    después**: la máquina KISS reabre trama en el FEND de cierre, así que el texto que va
    detrás en el mismo golpe se lo come. Eso es comportamiento viejo, no se toca, y el banco
    demuestra que sigue igual.

## 5. Los números del banco (antes / después)

`tools/banco_usb/` (salida completa en `tools/banco_usb/salida_banco_usb.txt`). Compila el
código **real** del transporte y modela el bucle como `main.cpp`: cabecera (leer), cuerpo
(20 ms), repintado (parcial 1,5 s / completo 2,5 s), cobro y cola (5 ms). Los dos números
que **no** son invención: el FIFO del CDC = **256 B** (`CFG_TUD_CDC_RX_BUFSIZE`, citado
arriba) y el paquete USB = **64 B por milisegundo** (endpoint de full-speed).

| Escenario | ANTES (git) | AHORA |
|---|---|---|
| 1. `status` (17 B) con un parcial de 1,5 s en marcha | atendido en **t=1506 ms** · **0 B** leídos durante el repintado | atendido en **t=1500 ms** · **17 B** leídos durante el repintado |
| 2. **`set` de 1,4 KB** con el panel sucio en cada vuelta | atendido en **t=9136 ms** · el host **bloqueado 7490 ms** (tarda 7,6 s en soltar la línea) | atendido en **t=1500 ms** · host **0 ms** bloqueado · **1384 B** leídos durante el repintado, **1384 encolados** |
| 3. El mismo `set` con el nodo en reposo (control) | t=234 ms | **t=234 ms (igual)** |
| 4. `status` con un **completo** de 2,5 s | t=2506 ms | **t=2500 ms** |
| 5. Trama KISS durante el repintado | intacta, entregada t=1506 ms | intacta, entregada **t=1500 ms**, **0** dentro del repintado |
| 6. Línea JSON con el modo KISS puesto | t=1506 ms | t=1500 ms |
| 7. Trama KISS + texto detrás (comportamiento viejo) | texto comido, trama intacta | **idéntico** |
| **Comandos ejecutados DENTRO del repintado** | 0 | **0** (y el contra-ejemplo: **1**) |
| **Tramas KISS entregadas DENTRO del repintado** | 0 | **0** (y el contra-ejemplo: **1**) |

### Qué dicen estos números, sin adornos

- **El comando CORTO se ejecuta en el mismo instante que antes** (t=1500 vs t=1506): el
  techo lo pone el panel, y 1,5-3 s de tinta electrónica no se pueden quitar desde el parser.
  Lo que cambia es que **durante el repintado el puerto ya se vacía** (0 B → 17 B) y que el
  comando está **entero y en cola** en el momento en que el panel queda libre.
- **El comando LARGO es donde está la ganancia gorda**: 1,4 KB con el panel repintando pasa
  de **9,1 s a 1,5 s** (6× menos), y el programa del ordenador deja de quedarse **7,5 s**
  esperando para poder escribir. Ese es exactamente el síntoma que el operador describe.
- **Sin repintados no cambia nada** (escenario 3: 234 ms en las dos versiones): el cambio no
  añade trabajo por su cuenta.
- **Cero reentrada**, medido, y el banco demuestra que sabría verla.

## 6. Cómo se comprueba

Los cuatro entornos (los dos T-Echo son los que importan):

```powershell
cd _trabajo_ea2oy
$env:PLATFORMIO_CORE_DIR="<RUTA_DEL_PROYECTO>\_trabajo_ea2oy\_pio_core"
& "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" `
   run -e techo_plus_s140v7 -e techo_s140v7 -e faketec_sx1262_433 -e faketec_e22p_433
```

Los cuatro dan **SUCCESS**. Memoria (T-Echo): la RAM pasa de **35.588 a 43.972 bytes**
(`techo_s140v7`, 14,3 % → 17,7 % de 243 KB) y de **35.620 a 44.004** (`techo_plus_s140v7`):
son ~8,2 KB, los 4 KB del anillo más los 4 KB del acumulador de línea, que antes vivían en
el montón como `String`. Flash: 44,9 % → 45,2 % (`techo_s140v7`) y 44,8 % → 45,1 %
(`techo_plus_s140v7`).

El banco (no hace falta placa; necesita un compilador de C++ del ordenador):

```powershell
cd _trabajo_ea2oy\tools\banco_usb
.\ejecuta_banco_usb.ps1        # zig, g++ o clang++ (ver la cabecera del script)
```

## 7. Qué tiene que probar el operador (en la placa)

1. **Con la pantalla pintando, mandar un comando y mirar el reloj.** Lo que tiene que notar:
   que el nodo **contesta igual** (el panel sigue tardando lo suyo), pero que **deja de
   quedarse sordo**: sobre todo con el **configurador web**, que manda la configuración
   entera de una vez. Antes, con el panel repintando, guardar la configuración podía tardar
   **varios segundos** y el navegador se quedaba esperando; ahora tiene que entrar en el
   primer hueco.
2. **Comprobar que el bombeo funciona, con una palabra**: por el USB, escribir

   ```
   usb
   ```

   y mirar `bombeo` y `cola`. `bombeo` son los bytes que el driver ha leído **mientras
   esperaba al panel** (antes de este cambio tenía que ser 0 siempre): tiene que **subir**
   cada vez que la pantalla pinta. `cola` es lo máximo que ha llegado a acumularse de una
   vez: mandando la configuración (1,4 KB) mientras pinta tiene que acercarse a 1400, **no**
   quedarse en los 256 bytes del FIFO.
   **`dentro` tiene que ser 0 SIEMPRE.** Si alguna vez sale `REENTRADA`, algo está
   ejecutándose dentro del driver y hay que parar: eso sería pintar dentro de un pintado.
   (Lo mismo está en el JSON: `{"cmd":"status"}` → `status.usb.bombeo/.colaMax/.cola/.dentro`.)
3. **El táctil y el menú, como en la prueba del Project Butter**: que el táctil siga
   navegando de uno en uno, que el botón físico cambie de diapositiva, que el menú abra y
   vuelva, que «Fijar coords» se pueda cancelar, que la confirmación de 15 s no se
   descompense y que el carrusel siga avanzando. **Nada de esto se ha tocado**, pero es
   justo lo que comparte el bucle con lo nuevo.
4. **Y si usa una app KISS** (el puerto en modo KISS), mandar una trama y comprobar que
   **sale al aire igual que antes** (y que con el panel pintando no sale antes de tiempo ni
   se corrompe). El banco dice que la trama llega intacta y exactamente una vez; en la placa
   lo que se puede ver es el registro: `TNC TX ...` con los mismos bytes de siempre.

## 8. Lo que NO se ha podido comprobar

- **No hay ninguna placa conectada.** No se ha medido el tiempo real de un refresco, ni el
  FIFO del CDC en vivo, ni cuánto tarda de verdad el configurador del operador. Todo lo del
  panel son **los tiempos que declara el propio driver** (350 ms/2.000 ms de espera + 200 ms
  de tensiones + reset + trasvase), y todo lo demás es **el modelo del banco**, que está
  escrito con constantes reales y a la vista (sección 5).
- **El banco mide la LECTURA del puerto y CUÁNDO se ejecuta el comando**, no el panel: el
  rato de tinta es el mismo antes y después y por eso no se puede reducir desde aquí.
- **El modelo del host** (64 B por milisegundo y el `write()` que se queda esperando cuando
  el FIFO está lleno) es cómo funciona un CDC de full-speed, pero **no se ha medido en este
  ordenador**: lo que sí es un dato del core es el tamaño del FIFO (256 B) y el del
  endpoint (64 B).
- **El escenario 2 es el peor caso razonable** (el panel repintando en cada vuelta, como con
  tráfico APRS). Con el panel pintando de vez en cuando, la ganancia es menor; con el panel
  quieto, nula (escenario 3). El banco tiene las dos cosas para que se vea.
- **El comando `usb` y los contadores de `status.usb` no se han visto funcionar en hardware**:
  compilan y el banco ejercita EXACTAMENTE el mismo código (`lector()` es el objeto que
  usan), pero nadie los ha leído todavía en una T-Echo de verdad.
- **El tiempo que tarda el operador en notarlo**: eso solo lo puede decir él.
