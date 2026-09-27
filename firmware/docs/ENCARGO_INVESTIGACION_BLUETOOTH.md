# ENCARGO DE INVESTIGACION — ARRANCAR EL BLUETOOTH SIN COLGAR EL NODO
### Kacho System / APRS LoRa EA2OY — 2026-09-17

Este documento es el encargo completo para un subagente de investigacion. Contiene **lo que
funciona, lo que se probo, lo que rompio el nodo y las medidas exactas**. Nada de lo que hay
aqui es suposicion salvo donde se dice expresamente.

---

## 1. QUE HAY QUE CONSEGUIR

Que **el Bluetooth del nodo arranque sin colgar la placa**, y si no arranca, que **el nodo se
quede vivo** (cable, radio, pantalla, registro) diciendo por que.

Resultado actual: **al arrancar el SoftDevice (Bluetooth) el nodo entra en un bucle de
reinicios del USB (monta/desmonta) y solo se recupera con doble toque al reset + grabar por el
cargador UF2.** Esto se ha reproducido TRES veces con builds distintos (b25, b32, b33).

## 2. EL HARDWARE Y EL ENTORNO (medido, no deducido)

| dato | valor |
|---|---|
| placa | LilyGO **T-Echo Plus** (nRF52840) |
| USB de la aplicacion | `239A:00B3`, producto `KACHO_TECHO_433` |
| cargador UF2 | `TECHOBOOT`, volumen `0042-0042`, USB `239A:0029` |
| cargador (version) | `UF2 Bootloader 0.6.1-2-g1224915`, `lib/tinyusb (0.10.1-...)` |
| `INFO_UF2.TXT` dice | `Model: LilyGo T-Echo`, `SoftDevice: S140 version 7.2.0` (⚠ es **texto fijo** del cargador: NO es una lectura del chip) |
| entorno de compilacion | `techo_plus_s140v7` (PlatformIO + framework Arduino de Adafruit nRF52, SoftDevice S140) |
| app en flash | `0x27000` |
| app en RAM | `0x20004260` (CORREGIDO en el b33; antes `0x20006000`) |

### La ficha del SoftDevice que lleva la placa (LEIDA DE SU FLASH)

Leida en `0x3000` (direccion correcta: `MBR_SIZE 0x1000 + SOFTDEVICE_INFO_STRUCT_OFFSET
0x2000`; ver `nrf_sdm.h`, el offset es relativo al inicio del SoftDevice, **sin MBR**):

```
fwid    = 0x0100
id      = 0x0000008C   (identificador del SoftDevice S140)
tamano  = 0x27000
version = 0x006AD790
```

Ademas, del **firmware de fabrica del propio nodo** (`CURRENT.UF2`, 1.908.736 B, sacado del
cargador y guardado aparte) se extrajo su SoftDevice y **contiene un S140 valido con los mismos
`id` y tamano y `fwid = 0x0100`**. O sea: **la placa TIENE SoftDevice, y su firmware de fabrica
lo lleva dentro** (el UF2 de fabrica cubre `0x1000-0xEA000` entero con la familia del cargador).

Para comparar, de un **hex oficial de Nordic S140 7.3.0** (en
`C:\Users\Jesus\Desktop\Escritorio\guillermo\firmware-develop\bin\s140_nrf52_7.3.0_softdevice.hex`):
misma ficha en `0x3000`, `fwid = 0x0123`, `id = 0x8C`, `tamano = 0x27000`.

## 3. LO QUE **SI** FUNCIONA (no tocar)

- **El enlace BLE entero esta escrito y probado en escritorio**: receptor de lineas compartido
  con el USB, Nordic UART Service (`6E400001-...`, RX `...0002` escritura, TX `...0003` notify),
  MTU 247, PIN de 6 digitos + MITM, respuesta por donde vino el comando, veto de `dfu`/`wipe`
  por el aire. Ficheros: `src\ble_kiss.{cpp,h}`, `src\usb_lector.{cpp,h}`, `src\protocol.cpp`.
- **La aplicacion Android** habla ese protocolo por USB (y tiene el lado BLE escrito):
  `_app_android\`. El mando remoto del taller funciona por ADB:
  `_app_android\tools\remoto.ps1`.
- **La sonda del SoftDevice** (`softdevicePareceValido()` en `src\ble_kiss.cpp`) impide llamar a
  `Bluefruit.begin()` cuando la flash NO tiene un SoftDevice. **Esa proteccion es obligatoria:
  es lo unico que evita el bucle.** Cualquier propuesta debe conservarla.
- **La radio LoRa, la pantalla, el GPS y el registro de viaje** funcionan perfectamente.

## 4. LO QUE SE PROBO Y **NO** FUNCIONO (con la medida de cada cosa)

### 4.1 La sonda miraba la direccion equivocada  → CORREGIDO, no era la causa
- `SD_FWID_DIR` estaba en `0x200C`. El offset de `nrf_sdm.h` es **relativo a la base del
  SoftDevice** (`SOFTDEVICE_INFO_STRUCT_ADDRESS = 0x2000 + MBR_SIZE`), asi que la direccion
  absoluta es **`0x300C`**. En `0x200C` hay **codigo ARM** del propio SoftDevice.
- De ahi salieron los dos numeros que el proyecto tomo por chips averiados:
  `0xE002` (leido de un hex S140 6.1.1 en `0x200C`) y `0xD902` (de un hex S140 7.3.0 en
  `0x200C`). **Los dos son los mismos bytes de instrucciones, no una ficha.**
- Corregido en el b29 y verificado: la sonda pasa a leer `0x300C`.

### 4.2 El `fwid` esperado era inventado  → CORREGIDO, no era la causa
- `platformio.ini` declaraba `-D SD_ESPERADO_FWID=0x0101`. **`0x0101` no es el fwid de ninguna
  S140.** El del framework de Adafruit para S140 v7 es `0x0123` (`boards.txt`:
  `pca10100.menu.softdevice.s140v7.build.sd_fwid=0x0123`) y el de **esta placa es `0x0100`**.
- Ahora los entornos `s140v7` declaran `0x0100` (el real de la placa) y la sonda vuelve a ser
  **estricta**: si el fwid no coincide exactamente, **no se arranca**.

### 4.3 "Basta con que HAYA SoftDevice"  → **ESTO ES LO QUE ROMPIO EL NODO (b32)**
- En el b31/b32 se relajo la sonda para aceptar cualquier SoftDevice con `id 0x8C` y tamano
  creible, **dejando que se intentara arrancar aunque el fwid no fuera el esperado**.
- Resultado: **bucle de reinicios del USB**. Medido: `host_connected=true`, volumenes sin
  montar, el aparato se re-enumera (`/dev/bus/usb/001/00N` subiendo), la app dice "Han
  desenchufado el cable del nodo" una y otra vez. Recuperacion: **doble toque al reset** (desde
  el PC no se puede: no hay `TECHOBOOT` montado y el nodo no responde).
- **Conclusion: intentar arrancar un SoftDevice que no es el que la aplicacion espera cuelga el
  nodo.** La sonda estricta es la red de seguridad y se queda.

### 4.4 La RAM de la aplicacion  → CORREGIDO en el b33, **tampoco era la causa**
- El guion de enlazado `variants\techo\nrf52840_s140_v7.ld` ponia `RAM ORIGIN = 0x20006000`
  (copiado del guion de la **S140 v6**). El firmware del T-Echo que **si** arranca Bluetooth en
  este hardware (`_referencias\t-echo-lora-aprs\t-echo.ld`) usa **`0x20004260`**.
- Consecuencia teorica: las variables de la aplicacion caian dentro de la RAM del SoftDevice.
- Corregido en el b33 (`0x20004260`, verificado en el `.elf`: `.data` en `0x20004260`).
- **El b33 se grabo y SIGUE COLGANDOSE al arrancar el Bluetooth.** O sea: la RAM no era la
  causa (o no era la unica).

### 4.5 Lo que NO se ha probado todavia
- **Grabar un SoftDevice.** Se tiene el de fabrica del propio nodo (`CURRENT.UF2` guardado) y el
  hex oficial de Nordic 7.3.0. **No se ha grabado ninguno** (miedo razonable a dejar el nodo
  sin cargador).
- **Saber POR QUE falla**: nunca se ha leido el codigo de error de `sd_softdevice_enable()` ni
  se ha capturado el fallo duro. **Esto es lo primero que hay que resolver.**

## 5. REFERENCIAS QUE HAY EN DISCO (leerlas, son la mejor pista)

1. `_referencias\t-echo-lora-aprs\` — **firmware del T-Echo que SI hace funcionar el Bluetooth
   en este hardware** (proyecto `cfr34k/t-echo-lora-aprs`, en C con el SDK de Nordic, no con
   Arduino). Claves:
   - `Makefile`: `SOFTDEVICE_HEX := .../s140_nrf52_7.2.0_softdevice.hex`, `-DNRF_SD_BLE_API_VERSION=7`, `-DS140`.
   - `README.md`: *"When you first install the LoRa-APRS firmware, you must also install the
     correct SoftDevice"*; el objetivo `make uf2_sd` produce **un solo UF2 con el SoftDevice
     pegado delante de la aplicacion**. El firmware de fabrica del nodo es exactamente eso.
   - `t-echo.ld`: `FLASH 0x27000`, **`RAM 0x20004260`**.
   - `src\aprs_service.c`: servicio GATT propio (indicativo, comentario, simbolo, mensajes).
   - `tools\ble_client\`: clientes Python de referencia.
   - La carpeta `nrf5-sdk\` esta **vacia** (submodulo sin clonar): **no hay hex de SoftDevice ahi**.
2. `C:\Users\Jesus\Desktop\Escritorio\guillermo\firmware-develop\` — herramientas de Meshtastic:
   `bin\s140_nrf52_7.3.0_softdevice.hex` (oficial de Nordic), `bin\Meshtastic_7.3.0_bootloader-0.9.2_s140_7.3.0.hex`,
   y `src\platform\nrf52\nrf52840_s140_v7.ld` (guion de enlazado que usa Meshtastic en T-Echo con S140 v7).
3. `_BLE_DIAG\` — banco de pruebas viejo del Bluetooth con `CAUSA_RAIZ.md` (ojo: **su conclusion
   "el SoftDevice del chip no es valido" partia de leer `0x200C`, o sea que esta contaminada por
   el error 4.1**; usarla solo como historial).
4. `Cerebro_Faketec_APRS_Igate_EA2OY\ESTADO.md` — estado del proyecto (§4 Bluetooth, §4-ter sonda).
5. Herramientas propias ya escritas y probadas:
   - `_trabajo_ea2oy\tools\lee_hex.py` — lee un Intel HEX y saca la ficha del SoftDevice.
   - `_trabajo_ea2oy\tools\uf2.py` — lee/escribe UF2 (regiones, familias) y extrae trozos.
   - `_trabajo_ea2oy\tools\prueba_sonda_softdevice.py` — prueba de escritorio de la sonda (debe
     seguir dando **0 fallos**).
   - `_trabajo_ea2oy\tools\compila_tanda.ps1` — compila los CUATRO entornos y comprueba el numero.
   - `_trabajo_ea2oy\tools\verifica_memoria.ps1` — verificador del proyecto.

## 6. REGLAS DEL PROYECTO (no negociables)

1. **Idioma: ESPAÑOL**, y comentarios en el codigo **sin acentos**.
2. **PRIVACIDAD**: en el repositorio no entra ni un indicativo real, ni coordenadas reales, ni
   datos de terceros, ni contraseñas/PIN, ni ficheros de `data/`. Para hablar de la placa, di
   "el nodo del operador". Los logs del nodo contienen coordenadas reales: **no copiarlas a
   ningun fichero**.
3. **NO GRABAR NINGUN NODO.** Compilar y verificar en el PC; grabar lo decide el operador.
   Ademas, ahora mismo hay una **orden permanente de no tocar ni leer ningun puerto COM del PC**
   (hay un banco de medidas de Meshtastic en marcha). La grabacion se hace **desde el movil**:
   `adb push` del `.uf2` a `/data/local/tmp` y copiarlo a `/storage/0042-0042` cuando el nodo
   esta en `TECHOBOOT`.
4. **Cuidado con el arbol git**: NO hacer `git stash` en `_trabajo_ea2oy` (ya se perdieron
   ficheros nuevos una vez). No borrar ficheros de otros.
5. El **contador de compilacion es automatico** (sube solo al cambiar el codigo): que suba es
   correcto. Se compila con `powershell -NoProfile -File tools\compila_tanda.ps1`.
6. **La proteccion de la sonda se queda**: si la flash no tiene el SoftDevice que el build
   espera, **no se arranca el Bluetooth**. Un nodo sin Bluetooth es aceptable; un nodo en bucle
   no.

## 7. LO QUE SE PIDE (informe final)

### 7.1 Investigacion completa, con evidencia
Leer el codigo que de verdad interviene y explicar **que hace exactamente** el arranque del
SoftDevice en este proyecto:
- `cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\` y el codigo del framework de Adafruit
  (`bluefruit.cpp`, `Adafruit_nRF52_Arduino`), en
  `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\`.
- Que hace `Bluefruit.begin()` paso a paso: `sd_softdevice_enable()`, `sd_ble_enable()` con sus
  **opciones de RAM**, `sd_ble_cfg_set()`, prioridades de interrupcion, `sd_nvic_*`, vector
  table, reloj (LFCLK), y **que pasa si algo devuelve error** (¿se comprueba el retorno? ¿se
  cuelga?).
- **En que se diferencia** de lo que hace el firmware del aleman (`_referencias\t-echo-lora-aprs`)
  y el de Meshtastic, que SI funcionan en esta placa.
- El codigo del **cargador** (`framework-arduinoadafruitnrf52\bootloader\`) y del **MBR**: como
  reparte la flash y si algo de eso explica el bucle.

### 7.2 Varias posibilidades, ordenadas y con criterio
Entrega una **lista de hipotesis** (al menos 5 si existen), cada una con:
- que es,
- **por que encaja con las medidas** de la seccion 4,
- **como se comprobaria** (que habria que leer, medir o mirar),
- **que riesgo tiene** para el nodo (bucle / perdida del cargador / ninguno),
- y una estimacion de probabilidad, dicha como estimacion.

Sospechas que YO veo (para que las contrastes, no para que las repitas):
- **(a)** el `sd_ble_enable()` del core pide opciones de RAM incompatibles con ESTE SoftDevice
  (`fwid 0x0100`, mas antiguo que los 7.3.0 con los que enlazamos) y el nodo se cuelga por eso;
- **(b)** el reparto real de RAM del SoftDevice de esta placa **no es 0x20004260** y seguimos
  pisandolo (el b33 no lo descarta: solo prueba un valor);
- **(c)** el SoftDevice que lleva la placa **no es compatible** con el enlazado que hacemos
  (API 7 si, pero construido con otro reparto) y **hay que grabarle el de fabrica o el 7.3.0**;
- **(d)** el cuelgue no es del SoftDevice sino de la **aplicacion** justo despues (pantalla,
  `displayPopupWait`, reloj, interrupciones) y el bucle es un efecto secundario;
- **(e)** hay un problema de **relojes/prioridades** (LFCLK, `sd_clock_hfclk`, IRQ priority) que
  solo aparece con el SoftDevice arrancado.

### 7.3 Un plan de diagnostico SEGURO (lo mas valioso del encargo)
Disena **el build de diagnostico que hay que grabar UNA vez** (o el minimo numero de veces)
para que el nodo **cuente por que falla en vez de colgarse**, y con estas propiedades:
- **Bluetooth APAGADO de fabrica** (que arranque siempre estable, aunque no sirva para nada mas);
- que **capture y muestre el codigo de error** de `sd_softdevice_enable()` y de `sd_ble_enable()`;
- que si hay un **fallo duro**, deje registrado **que fallo y en que direccion** (por ejemplo
  capturando en el `HardFault_Handler` los registros y guardandolos en una zona que sobreviva al
  reinicio, o en la flash interna con `InternalFileSystem`), de modo que **tras el reinicio el
  operador lo pueda leer por el cable**;
- que **no toque el USB** en ningun momento (la leccion del b25/b32: si `Bluefruit.begin()`
  desmonta el USB y luego falla, el nodo queda en bucle);
- y que **respete la sonda estricta**.
Explica **paso a paso** como se graba y como se lee el resultado (el operador graba desde el
movil; el PC habla con el nodo por ADB y el mando `_app_android\tools\remoto.ps1`).

### 7.4 Que NO hacer
- **No grabes nada**, no toques puertos COM, no cambies la proteccion de la sonda para "ver que
  pasa": eso es exactamente lo que dejo el nodo en bucle tres veces.
- No inventes valores: si un numero no sale de una cabecera, de un hex oficial, de un guion de
  enlazado o de una medida, dilo como suposicion.

### 7.5 Entrega
Informe en español con: (1) como funciona el arranque del SoftDevice en este proyecto, fichero y
linea; (2) la tabla de hipotesis de 7.2; (3) el plan de diagnostico de 7.3 con el codigo exacto
propuesto (**sin grabar**), (4) los numeros y comprobaciones que hayas hecho tu mismo, y (5) lo
que no hayas podido averiguar. Si propones cambios de codigo, **dejalos escritos en el arbol** y
di exactamente que ficheros has tocado.
