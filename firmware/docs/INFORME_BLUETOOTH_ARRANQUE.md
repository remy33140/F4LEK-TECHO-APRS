# INFORME — POR QUE SE CUELGA EL NODO AL ARRANCAR EL BLUETOOTH
### Kacho System / APRS LoRa EA2OY — investigacion del 2026-09-17

**Encargo**: `_trabajo_ea2oy\docs\ENCARGO_INVESTIGACION_BLUETOOTH.md` (+ ampliacion:
buscar fuera del arbol y disenar un ejemplo minimo).

**NO se ha grabado ningun nodo. NO se ha tocado ningun puerto COM.**
Todo lo que digo del nodo viene de **las imagenes de su flash** y de **las medidas que ya
estaban escritas en el encargo**. No he hablado con la placa en vivo.

## COMO LEER ESTO

| etiqueta | significa |
|---|---|
| `[MEDIDO]` | lo he comprobado yo. **Debajo va el comando o el fichero+linea para repetirlo.** |
| `[DEDUCIDO]` | consecuencia logica de lo medido. No es una medida. |
| `[SUPUESTO]` | encaja, pero **no lo he podido comprobar**. |
| `[FUERA]` | dato de una fuente externa, con URL. |
| `[DEL OPERADOR]` | medida que ha hecho el operador y que **yo no puedo verificar** (no toco el nodo). |

## COMO SE REPRODUCE TODO LO QUE DIGO (los tres comandos)

```powershell
# 1) La ficha del SoftDevice de un hex oficial (o de cualquier Intel HEX)
cd C:\Users\Jesus\Desktop\LoRa_APRS_iGate-main\_trabajo_ea2oy
python tools\lee_hex.py "C:\Users\Jesus\Desktop\Escritorio\guillermo\firmware-develop\bin\s140_nrf52_7.3.0_softdevice.hex" 0x3000 32

# 2) El mapa REAL de un UF2 (regiones, ficha, tablas de vectores) -- herramienta nueva, solo lectura
python tools\mapa_memoria.py "C:\Users\Jesus\Desktop\techo plus ea2kr\CURRENT.UF2"

# 3) Los simbolos del firmware compilado (aqui se ve el numero que se le pasa a sd_ble_enable)
& "C:\Users\Jesus\.platformio\packages\toolchain-gccarmnoneeabi\bin\arm-none-eabi-nm.exe" `
  ".pio\build\techo_plus_s140v7\firmware.elf" | Select-String "__data_start__|__bss_start__|__bss_end__"
```

---

## 0. RESUMEN (y cuanto pesa cada frase)

1. **El "cuelgue" no es un cuelgue: es un reinicio.** `[MEDIDO]`
   Fichero `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\cores\nRF5\utility\debug.cpp`,
   **lineas 60-64**: `void HardFault_Handler(void) { NVIC_SystemReset(); }`.
   No esta marcada `weak` (`weak` esta en `gcc_startup_nrf52840.S:310`), asi que **es la
   que manda**: cualquier fallo duro = reinicio inmediato = bucle.
2. **Cuando falla, nadie cuenta nada.** `[MEDIDO]` `libraries\Bluefruit52Lib\src\bluefruit.cpp`,
   **lineas 118-140**: el manejador de errores que se le pasa al SoftDevice solo imprime
   `#if CFG_DEBUG`. Y `CFG_DEBUG` vale 0: `_pio_core\packages\framework-arduinoadafruitnrf52\platform.txt`,
   **linea 58** (`build.debug_flags=-DCFG_DEBUG=0`). Sin definir, la funcion **es vacia**.
3. **El numero que hace falta no hay que adivinarlo: lo contesta el SoftDevice.** `[MEDIDO]`
   `cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\include\ble.h`, **lineas 387-433**:
   `@param[in, out] p_app_ram_base ... On return, this will contain the minimum start
   address of the application RAM region required by the SoftDevice for this configuration.`
4. **La RAM de `0x20004260` NO tiene margen** (es exactamente el borde de la RAM del
   SoftDevice), **pero probablemente NO es la causa**. `[MEDIDO]` con el comando (3) de
   arriba: `__data_start__ = 0x20004260` en `.pio\build\techo_plus_s140v7\firmware.elf`, y
   `bluefruit.cpp` **linea 348** hace `uint32_t ram_start = (uint32_t) __data_start__;`.
   `[FUERA]` El PDF oficial de release notes de la S140 7.2.0 dice que su RAM **minima** es
   **`0x1678`** (5,6 KB) `[+ 0x600 de pila en el peor caso]`: el proyecto le da
   `0x20004260`, o sea **~10-11 KB mas**. Y los ejemplos oficiales del SDK 17.x para este
   chip dan `0x20002AE8`, `0x20002BE0` y `0x200018D8`. **Los tres por debajo.**
   **Asi que la RAM es hoy la hipotesis MENOS probable de las grandes** (seccion 3, H2).
5. **La placa lleva S140 7.2.0** (`fwid 0x0100`, `id 0x008C`, `size 0x27000`) y compilamos
   contra cabeceras 7.3.0. `[MEDIDO]` para los bytes y las direcciones; `[FUERA]` para que
   `0x0100` y `0x27000` sean los de la 7.2.0 (PDF oficial de release notes de Nordic).
6. **El sospechoso numero uno es una prioridad de interrupcion ilegal** (codigo `0x1001`):
   es la unica causa conocida de fallo del SoftDevice que aparece **despues** de que la
   aplicacion grande haya arrancado radio, pantalla, sensores y botones. `[DEDUCIDO]`
7. **El bucle tiene firma medida en el propio proyecto**: `[MEDIDO]` el marcador "empieza
   el Bluetooth" aparece **399 veces** en un registro de viaje de 2243 lineas, con el
   tiempo reiniciandose a `0s`/`1s`. Detalle y limite de este dato, en la seccion 6.1.

---

## 1. COMO ARRANCA DE VERDAD EL SOFTDEVICE (fichero y linea)

### 1.0 ANTES DE NADA: LOS TRES FIRMWARES NO SON COMPARABLES `[MEDIDO]`

Esto es importante porque la pregunta natural es *"si Meshtastic arranca el Bluetooth en
este cacharro, por que nosotros no"*. **Meshtastic NO lo arranca con el SoftDevice que
tiene la placa.** Medido:

| firmware | app en flash | app en RAM | SoftDevice que asume | fuente de la medida |
|---|---|---|---|---|
| **Meshtastic `t-echo-plus`** (binario oficial) | **`0x26000`** | **`0x20006000`** | **v6** (`fwid 0x00B6`) | `readelf -S` sobre `.pio\build\t-echo-plus\firmware-t-echo-plus-2.7.26.54e0d8d.elf` -> `.text` en `0x26000`, `.data` en `0x20006000` |
| aleman (`cfr34k/t-echo-lora-aprs`) | `0x27000` | `0x20004260` | **v7.2.0** | `_referencias\t-echo-lora-aprs\t-echo.ld` |
| **el nuestro** | `0x27000` | `0x20004260` | v7 (`fwid 0x0100`) | `variants\techo\nrf52840_s140_v7.ld` |

Por que Meshtastic va en v6: `C:\Users\Jesus\Desktop\firmware\variants\nrf52840\t-echo-plus\platformio.ini`
dice `extends = nrf52840_base` + `board = t-echo` y **no** pone `board_build.ldscript`; y
`boards\t-echo.json` declara `"ldscript": "nrf52840_s140_v6.ld"` con
`"sd_version": "6.1.1"`, `"sd_fwid": "0x00B6"`. El `.ld` de la v7
(`src\platform\nrf52\nrf52840_s140_v7.ld`) **solo lo usan otras placas**: aparece en 13
ficheros de `variants\nrf52840\` y **en ninguno de los cuatro del T-Echo**.

**Consecuencias, y las dos importan:**

1. **El unico firmware v7 conocido que arranca el Bluetooth en este hardware es el
   aleman, y usa EXACTAMENTE nuestro reparto** (`0x27000` + `0x20004260`). No hay un
   segundo. Eso hace la comparacion con el aleman **mas** valiosa, no menos.
   **Y el `0x20004000` "de Meshtastic" no aplica al T-Echo**: ese numero es del
   `nrf52840_s140_v7.ld` de Meshtastic, que **ninguna variante del T-Echo usa**.
2. **AVISO OPERATIVO, con la aritmetica bien hecha** (me equivoque al decirlo la primera
   vez y lo corrijo): el SoftDevice ocupa **`0x1000`-`0x27000`** y **por eso la aplicacion
   empieza EN `0x27000`**, que es lo que hacemos nosotros. Un build de Meshtastic enlaza
   la aplicacion en **`0x26000`**: o sea que **su primer 4 KB caeria DENTRO de la zona del
   SoftDevice v7** (`0x26000`-`0x27000`), y ademas el SoftDevice que ese build espera es el
   **v6**, no el v7. **Conclusion: no grabar el Meshtastic oficial del T-Echo en un nodo
   con SoftDevice v7** — no es una prueba que se pueda hacer "para ver que pasa".

### 1.1 La cadena completa

Todo esta en `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\`.

| # | que pasa | donde (fichero:linea) |
|---|---|---|
| 1 | `bleLinkInit()` se llama **al final** de `setup()`, despues de radio, pantalla, sensores, botones y registro | `_trabajo_ea2oy\src\main.cpp:378` |
| 2 | La **sonda** lee el `fwid` en `0x300C`; si no coincide con `SD_ESPERADO_FWID`, **no se llama a `Bluefruit.begin()`** | `_trabajo_ea2oy\src\ble_kiss.cpp:194-212` (la sonda) y `:654` (la puerta) |
| 3 | `Bluefruit.configPrphConn(247, ..., 4, 4)`: MTU 247, 4 avisos en cola | `_trabajo_ea2oy\src\ble_kiss.cpp:552` |
| 4 | `Bluefruit.begin(1, 0)`: 1 enlace, sin central | `_trabajo_ea2oy\src\ble_kiss.cpp:554` |
| 5 | **`usb_softdevice_pre_enable()`** = `nrfx_power_usbevt_disable()` + `uninit()` + `nrfx_power_uninit()`. **AQUI SE DESMONTA EL USB** | `bluefruit.cpp:288-290` llama a la de `:67-72` |
| 6 | Reloj LFCLK: con `USE_LFXO` -> **cristal de 32 kHz, 20 ppm** | `bluefruit.cpp:293-301` + `_trabajo_ea2oy\variants\techo\variant.h:18` |
| 7 | **`sd_softdevice_enable(&clock_cfg, nrf_error_cb)`** | `bluefruit.cpp:319` |
| 8 | `usb_softdevice_post_enable()`: `sd_power_usbdetected_enable()` etc. **Devuelve el USB** | `bluefruit.cpp:322-324` llama a la de `:76-92` |
| 9 | `uint32_t ram_start = (uint32_t) __data_start__;` (= `0x20004260`) | `bluefruit.cpp:347-348` |
| 10 | 7 x `sd_ble_cfg_set(...)`: UUID, roles, service_changed, tabla de atributos `0x1000`, MTU 247, event length, colas HVN y write-cmd | `bluefruit.cpp:352-433` |
| 11 | **`sd_ble_enable(&ram_start)`** | `bluefruit.cpp:438` |
| 12 | `sd_ble_opt_set`, `Periph.begin()`, `Security.begin()`, `sd_ble_gap_device_name_set` | `bluefruit.cpp:449-463` |
| 13 | `NVIC_EnableIRQ(SD_EVT_IRQn)`, tareas FreeRTOS, `bond_init()` | `bluefruit.cpp:473-488` |

**El orden importa y es el dato clave**: el USB se devuelve en el **paso 8** (linea 323) y
`sd_ble_enable` es el **paso 11** (linea 438). O sea:

- Si falla el **paso 7** (`sd_softdevice_enable`): el USB **no vuelve nunca**. El nodo se
  queda mudo. `[MEDIDO]` — la linea 322 (`#ifdef USE_TINYUSB`) esta **despues** del
  `VERIFY_STATUS` de la 319.
- Si falla del **paso 10 en adelante**: el USB **ya estaba devuelto**; el nodo se queda
  **vivo pero sin Bluetooth**.

### 1.2 Que pasa si algo devuelve error

`VERIFY_STATUS` esta en `cores\nRF5\verify.h`, **lineas 77-108**:

```c
#define VERIFY_ERR_DEF(_status, _ret, _funcstr) \
    if ( 0 != _status ) { VERIFY_MESS(...); return _ret; }
```

O sea: **`Bluefruit.begin()` devuelve `false` y se sale ahi mismo**, sin deshacer nada.

### 1.3 Una trampa del framework que he medido (y que importa para reproducirlo)

`[MEDIDO]` En `bluefruit.cpp`, **lineas 58-94**, las dos funciones del USB estan dentro de
`#ifdef USE_TINYUSB` pero **el `extern "C"` no las cubre**: la linea 63 es
`extern "C" void tusb_hal_nrf_power_event(uint32_t event);` y las funciones de las lineas
67 y 76 quedan con **enlace de C++ (mangled)**.

**Como se comprueba**:

```powershell
& "C:\Users\Jesus\.platformio\packages\toolchain-gccarmnoneeabi\bin\arm-none-eabi-nm.exe" `
  "_trabajo_ea2oy\diag_ble\.pio\build\techo_diag_s140v7\lib3dc\libBluefruit52Lib.a" | Select-String "usb_softdevice"
```
Sale: `T _Z25usb_softdevice_pre_enablev` y `T _Z26usb_softdevice_post_enablev`.

**Consecuencia practica**: llamarlas por su nombre "normal" desde otro fichero **no
enlaza** (me paso, y esta documentado en el propio banco). Esto **no es la causa del
cuelgue**: es una dificultad para reproducir el paso 5 fuera del core.

---

## 2. LA FICHA DEL SOFTDEVICE: TODOS LOS BYTES, Y LO QUE NO CUADRA

### 2.1 El hex OFICIAL de Nordic, byte a byte `[MEDIDO]`

```powershell
python tools\lee_hex.py "C:\Users\Jesus\Desktop\Escritorio\guillermo\firmware-develop\bin\s140_nrf52_7.3.0_softdevice.hex" 0x3000 32
```
```
0x3000: 2C FF FF FF | DB E5 B1 51 | 00 70 02 00 | 23 01 FF FF | 8C 00 00 00 | 78 DB 6A 00
        tam.ficha    | uuid        | size=0x27000 | fwid=0x0123 | id=0x008C    | ver=0x006ADB78
```

**Este reparto se sostiene por aritmetica**: `SD_FLASH_SIZE = 0x26000`
(`nrf_sdm.h:144`) + MBR `0x1000` (`nrf_mbr.h:68`) = **`0x27000`**, que es exactamente el
`size`. Y el formato de Nordic se ve a la legua: **un valor de 16 bits con su complemento
a uno al lado** (`00 01 FF FF`, `23 01 FF FF`).

Los offsets que usa el proyecto (`+0x08` size, `+0x0C` fwid, `+0x10` id, `+0x14` version
desde `0x3000`) **coinciden exactamente** con este reparto. `[MEDIDO]`:
`_trabajo_ea2oy\src\ble_kiss.cpp:153-178`, contrastado con `nrf_sdm.h:107-123`.

### 2.2 Las imagenes del nodo `[MEDIDO]`

```powershell
python tools\mapa_memoria.py "C:\Users\Jesus\Desktop\techo plus ea2kr\CURRENT.UF2"
```
```
NODO (S140 7.2.0)  0x3000: DB E5 B1 51 | 00 70 02 00 | 00 01 FF FF | 8C 00 00 00 | 90 D7 6A 00
                           uuid        | size=0x27000 | fwid=0x0100 | id=0x008C    | ver=?
FABRICA v6         0x3000: DB E5 B1 51 | 00 60 02 00 | B6 00 FF FF | 8C 00 00 00 | 69 91 5B 00
BOOTLOADER LILYGO  0x2000: 2C FF FF FF | DB E5 B1 51 | 00 60 02 00 | B6 00 FF FF | 8C 00 00 00 | ...
```

He mirado **las siete copias** del firmware de fabrica que hay en el disco
(`Desktop\techo plus ea2kr\CURRENT.UF2`, `Desktop\EA2MKR\CURRENT.UF2`,
`Desktop\Respaldo_TEcho_EA2KR_20260914-1131\...`, `Desktop\Escritorio\NavaTastic_Audit\CURRENT_UF2_FABRICA_015a.uf2`
y los dos `_trabajo_ea2oy\data\rescue\`). **Todas coinciden.**

**Lo que cuadra, y es lo que importa**: `fwid` en `base+0x0C` y `id` en `base+0x10`, con el
valor que corresponde a cada version (`0x0100` la v7, `0x00B6` la v6), y el formato de
Nordic a la vista (**valor de 16 bits + su complemento a uno al lado**: `00 01 FF FF`,
`B6 00 FF FF`, `23 01 FF FF`).

**La confirmacion definitiva la trae la fuente oficial, no yo.** `[FUERA]` El PDF oficial
de release notes de la S140 7.2.0 dice literalmente **"The Firmware ID of this SoftDevice
is 0x0100"** y **"Flash: 156 kB (0x27000 bytes)"**
(https://raw.githubusercontent.com/greenlsi/nrf5-sdk/main/components/softdevice/s140/doc/s140_nrf52_7.2.0_release-notes.pdf).
O sea: **el `fwid 0x0100` y el `size 0x27000` que hay en `0x300C` y `0x3008` de las
imagenes del nodo son los de la S140 7.2.0, confirmado por Nordic.** Las tablas que he
medido yo (en `0x3008`/`0x300C` de los hex oficiales) coinciden: 6.1.1 = `0x26000`/`0x00B6`,
7.2.0 = `0x27000`/`0x0100`, 7.3.0 = `0x27000`/`0x0123`.

**Conclusion: la sonda del proyecto lee el `fwid` en la direccion correcta y su numero
esperado (`0x0100`) es el bueno.** Los dos diagnosticos falsos de este proyecto venian de
leer `0x200C` (seccion 2.3), y eso ya esta corregido.

**Y no todas las fotos son iguales** `[MEDIDO]` — esto hay que decirlo porque se ha
prestado a confusion (otro investigador de esta misma sesion concluyo lo contrario):

| imagen | `size` (en `base+0x08`) | `fwid` (en `base+0x0C`) | quien es |
|---|---|---|---|
| `respaldo_nodo_antes_de_flashear_20260913.uf2` | `0xFFFF00B6` -> **0x00B6** | `0x008C` | **S140 6.1.1** grabada por este proyecto en algun momento |
| `techo_EA2KR_original_..._20260914-1118.uf2` y `CURRENT.UF2` | `0xFFFF0100` -> **0x0100** | `0x008C` | **S140 7.2.0**, lo que hay hoy |

O sea: **el 13-sep la placa llevaba la 6.1.1 y el 14-sep la 7.2.0.** Alguien grabo un
SoftDevice v7 entre esas dos fechas (el firmware aleman o Meshtastic). El error en que se
puede caer —y en el que cayo el otro investigador— es leer el campo `size` **como si fuera
un `uint16`** y quedarse con sus dos bytes altos: entonces sale `0xFFFF` en vez de
`0x0100`, y parece que la placa lleva otra cosa. **Los dos bytes del `fwid` estan en
`0x300C` y son `8C 00` en las dos imagenes; el `0x0100` esta en `0x300C - 4 = 0x3008`,
dentro del campo `size`.** Por eso el banco imprime **los dos juegos de valores** y ademas
pregunta el fwid al propio SoftDevice.

**Lo unico que NO he sabido explicar**: el **byte de tamano de ficha**. El hex oficial de
Nordic y el bootloader de LilyGO traen `2C FF FF FF` (44 = tamano de la ficha) **ocho
bytes antes** del uuid; **en las imagenes del nodo ese byte no aparece**. Ademas hay una
incoherencia de 4 bytes entre lo que dicen las **cabeceras** (`SD_SIZE_OFFSET = +0x08`) y
lo que se ven en las **release notes** ("a struct with size, fwid, id and version fields
[...] the size field is located at offset 4"). Son **dos discrepancias pequenas que no
afectan a lo que se lee** (el `fwid` esta donde esta en las tres fuentes y su valor es el
correcto en las tres), pero **NO RESUELTAS**: las dejo escritas en la seccion 8.

### 2.3 Por que este proyecto leyo `0x200C` durante dias `[MEDIDO]`

`cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\include\nrf_sdm.h`, **linea 65**:
```c
#define MBR_SIZE 0
```
El valor bueno esta en **otro** fichero: `...\include\nrf52\nrf_mbr.h`, **linea 68**:
`#define MBR_SIZE (0x1000)`.

Y `nrf_sdm.h:103` define
`SOFTDEVICE_INFO_STRUCT_ADDRESS (SOFTDEVICE_INFO_STRUCT_OFFSET + MBR_SIZE)`.
**Quien incluya solo `nrf_sdm.h` calcula `0x2000 + 0` y se equivoca por `0x1000`.** Eso es
exactamente lo que paso aqui: se leia `0x200C`, que es **codigo del SoftDevice**, y de ahi
salieron `0xE002` y `0xD902`, que se tomaron por chips averiados.

---

## 3. LAS HIPOTESIS (ordenadas; probabilidades = ESTIMACIONES mias, no medidas)

**Tabla de probabilidades** (estimaciones; suman ~98 %, el resto es "no lo se"):

| | hipotesis | estimacion |
|---|---|---|
| H1 | prioridad de interrupcion ilegal (`0x1001`) | **~35 %** |
| H2 | el `app_ram_base` de `0x20004260` | **~8 %** (bajada: el minimo oficial de la S140 7.2.0 es `0x20001678`) |
| H3 | desajuste cabeceras 7.3.0 / SoftDevice 7.2.0 | **~20 %** |
| H4 | el fallo esta en la aplicacion **despues** del SoftDevice | **~18 %** |
| H5 | el cristal de 32 kHz (LFCLK) | **~12 %** |
| H6 | el SoftDevice de la placa esta danado | **~3 %** |

### H1 — Prioridad de interrupcion ilegal (codigo `0x1001`) — ~35 %

- **Que es**: `sd_softdevice_enable()` devuelve `NRF_ERROR_SDM_INCORRECT_INTERRUPT_CONFIGURATION`
  = `0x1001` = 4097 si hay una IRQ habilitada con una prioridad que el SoftDevice no
  permite. **El SoftDevice se reserva los niveles 0, 1 y 4.** `[MEDIDO]`
  `cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\include\nrf_nvic.h`, **lineas 80-83**
  (`__NRF_NVIC_SD_IRQ_PRIOS` = bits 0, 1 y 4).
  `[FUERA]` Nordic DevZone 52215 (respuesta de Øyvind): *"Your error 4097 = 0x1001 where
  NRF_ERROR_SDM_BASE_NUM = 0x1000"*, causa = IRQ con prioridad ilegal,
  https://devzone.nordicsemi.com/f/nordic-q-a/52215/error-4097-unknown-error-code-at-nrf_sdh_enable_request
- **Por que encaja**: es la **unica** causa conocida de fallo del SoftDevice que depende de
  lo que la aplicacion haya hecho **antes**. Y este proyecto llama a `bleLinkInit()` al
  **final** de `setup()` (`main.cpp:378`), despues de radio (SPI/SPIM3), pantalla de tinta
  (SPIM2 o bit-bang), GPS, sensores I2C y los dos botones con interrupcion
  (`src\button.cpp:335` y `:355`).
- **En contra**: `[MEDIDO]` que las dos IRQ que si se ven en el arbol estan **bien**:
  `USBD_IRQn` a 2 (`Adafruit_TinyUSB_nrf.cpp:69`) y `GPIOTE_IRQn` a 3 (`WInterrupts.c:54`).
  Los sospechosos son RadioLib, el driver de tinta y el I2C, que **no he auditado**.
- **Como se comprueba**: (1) el banco imprime el codigo exacto de
  `sd_softdevice_enable()`; si sale `0x1001`, esta es. (2) En el firmware del nodo, leer
  `NVIC->ISER[0]` y `NVIC->IP[]` con `NVIC_GetEnableIRQ()`/`NVIC_GetPriority()` **antes**
  de `Bluefruit.begin()` y buscar prioridades 0, 1 o 4. (3) Arrancar el Bluetooth **lo
  primero** del `setup()`: si asi arranca, demostrado.
- **Riesgo para el nodo**: **bajo**. El banco no toca el firmware. La prueba 3 es un cambio
  de orden (no se graba hasta que el operador lo decida).

### H2 — El `app_ram_base` de `0x20004260` se queda corto — ~8 % (BAJADA: ver el dato oficial)
- **Que es**: hay que darle a `sd_ble_enable()` una RAM >= la que pide para esta
  configuracion (MTU 247, tabla `0x1000`, 4+4 en cola). El proyecto le da **exactamente el
  borde**, sin margen.
- **Por que encajaba**: `[FUERA]` el numero `0x20004260` **no lo valido nunca
  `sd_ble_enable()`**: sale del `t-echo.ld` del firmware aleman, y ese proyecto parte de un
  SDK **sin SoftDevice** (`cfr34k/nrf5-sdk`, submodulo del repo
  `_referencias\t-echo-lora-aprs`; su `.ld` incluye `nrf_common.ld` y **no hay
  softdevice.hex**). O sea: **valor ajustado a mano, no medido**. Y el margen es **cero**:
  `[MEDIDO]` `__data_start__ = 0x20004260` (comando (3) de la cabecera).
- **POR QUE LA HE BAJADO, y esto es lo importante**: `[FUERA]` el PDF oficial de release
  notes de la S140 7.2.0 dice **"RAM: 5.6 kB (0x1678 bytes). This is the minimum required
  memory. The actual requirements depend on the configuration chosen at time
  `sd_ble_enable()`"**. O sea: minimo **`0x20001678`**, y el proyecto le da **`0x20004260`**
  -> **`0x2BE8` = 11,2 KB por encima del minimo** (o 9,6 KB por encima de `0x20001FC0`,
  que es el minimo + el peor caso de pila del SoftDevice, `0x600`). Y `[FUERA]` los valores
  de los ejemplos oficiales del SDK 17.x para este mismo chip (`pca10056/s140`,
  `FLASH 0x27000`) son **`0x20002AE8`** (`ble_app_uart`), **`0x20002BE0`**
  (`ble_app_hrs`) y **`0x200018D8`** (`ble_app_beacon`): **todos por debajo**. Y las
  mediciones publicadas de firmware real (Meshtastic `0x200038A0`, Adafruit #624
  `0x200034A0`, #567 `0x20003768`, #812 `0x20004730`) tambien.
  **Conclusion honesta: la RAM probablemente NO es la causa**, y lo digo aunque era la
  hipotesis favorita del operador.
- **Como se comprueba de todas formas**: **el banco lo mide** en una sola grabacion (le da
  una base baja a proposito y lee lo que contesta el SoftDevice). Es la unica forma de
  tener el numero **de esta placa** en vez de los de otros.
- **Riesgo**: **bajo**. Es una llamada que devuelve un numero.

#### H2-bis — La RAM va AL REVES de como lo cuenta el comentario del guion `[MEDIDO]`

Esto es una **correccion al proyecto**, y va aparte porque no es una hipotesis sino un
hecho de la documentacion:

`[MEDIDO]` `cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\include\ble.h`, **lineas
412-414**:
> *"At runtime the IC's RAM is split into 2 regions: The SoftDevice RAM region is located
> between `0x20000000` and `APP_RAM_BASE-1` and the application's RAM region is located
> between `APP_RAM_BASE` and the start of the call stack."*

O sea: **SUBIR `APP_RAM_BASE` le da MAS RAM al SoftDevice. BAJARLO se la quita.** Y el
comentario del guion del proyecto
(`_trabajo_ea2oy\variants\techo\nrf52840_s140_v7.ld`, **lineas 27-43**) dice lo contrario:
*"El SoftDevice v7 de esta placa se queda menos RAM, y su zona empieza mas abajo"*.

**Consecuencia**: el cambio del **b33** (de `0x20006000` a `0x20004260`) **no le dio mas
RAM a la aplicacion "respetando" al SoftDevice: le recorto `0x1D40` = 7.488 bytes de RAM al
SoftDevice.** La conclusion del b33 ("la RAM no era la causa") **no queda demostrada por
ese experimento**: lo que se probo fue *un valor distinto*, no *el valor correcto*.

**PERO eso no resucita la hipotesis**: `[FUERA]` el minimo oficial de la S140 7.2.0 es
`0x1678` (+`0x600` de pila en el peor caso = `0x1C78`), y `0x20004260` le da **`0x4260`**.
Sigue habiendo **~9,6 KB de margen** sobre el minimo. Y Meshtastic funciona en el mismo
T-Echo con **menos** (`0x20004000`), o sea `0x4000`. **Las dos cosas son verdad a la vez:
el razonamiento del comentario esta mal Y el numero probablemente vale.** Por eso H2 se
queda en ~8 % y no la borro: **solo la medida del banco lo cierra**.

**El comentario del guion hay que corregirlo** (yo no lo he tocado): dice lo contrario de
lo que dice la documentacion.

### H3 — El desajuste cabeceras 7.3.0 / SoftDevice 7.2.0 — ~20 %

- **Que es**: compilamos contra las cabeceras de la 7.3.0 y la placa corre la 7.2.0. Si el
  SoftDevice se cae con `NRF_FAULT_ID_SD_ASSERT` en un `sd_ble_cfg_set()` o en
  `sd_ble_enable()`, el manejador vacio lo convierte en reinicio.
- **A favor (es lo que la sube)**: `[FUERA]` Nordic **recomienda alinear**: Einar Thorsrud
  (Nordic), *"You are using the header files for the SoftDevice you are actually using"*
  (DevZone 82849), y Hung Bui: usar una SD mas vieja que el SDK *"may work ... but there is
  no guarantee"* (DevZone 63771). Ademas `[FUERA]` el **delta que se ha medido** entre las
  dos cabeceras es **aditivo** (`BLE_GAP_SLAVE_LATENCY_*` nuevos en `ble_gap.h`; un miembro
  nuevo en la union `ble_gatts_cfg_t` en `ble_gatts.h`; `sd_ble_enable` **intacto**), o sea
  que **no cambia los offsets** de lo que usa el core de Adafruit.
- **En contra**: `[FUERA]` Nordic tambien dice que **dentro de la misma version mayor la
  aplicacion no necesita cambios**: *"Between minor versions of the softdevice, the
  application doesn't need to be modified to work with the new softdevice (with same major
  version)"* (Hung Bui, DevZone 20724, respuesta verificada). Y la release notes de la
  7.2.0 dice ser **binaria compatible** con la 7.0.1 y que *"memory requirements have not
  changed"*.
- **Como se comprueba**: el banco pasa **su propio manejador de fallo** a
  `sd_softdevice_enable()` y apunta `id`, `pc` e `info`. Si sale `id = 1`
  (`NRF_FAULT_ID_SD_ASSERT`) con un `pc` en `0x1000..0x28000` (la zona del SoftDevice),
  esta es. **Y el arreglo seria grabar la S140 7.3.0** (`fwid 0x0123`), con el riesgo de la
  seccion 5.
- **Riesgo**: **bajo** el diagnostico; **ALTO** el arreglo si se decide grabar un SoftDevice.

### H4 — El fallo no es del SoftDevice sino de la aplicacion justo despues — ~18 %

- **Que es**: el SoftDevice arranca, `Bluefruit.begin()` devuelve `true`, y el nodo se cae
  en lo siguiente.
- **Por que encaja**: `[MEDIDO]` `bleArrancaStack()` tiene **cuatro** salidas:
  `Bluefruit.begin()` (`ble_kiss.cpp:554`), `setPIN()` (`:575`), `gServicio.begin()`
  (`:594`) y `Advertising.start()` (`:686`). Solo la primera toca el USB.
  Y `BLEUuid::begin()`/`BLEService::begin()` son exactamente donde el foro de Adafruit vio
  cascadas de errores derivados de un `begin()` fallido `[FUERA: PR #812]`.
- **Como se comprueba**: **el banco NO llama a `Bluefruit`**: hace los pasos a mano y para
  despues de `sd_ble_enable()`. Si el banco termina con `errBle2 = 0`, el SoftDevice y la
  configuracion BLE quedan **descartados** y el fallo esta en lo que el firmware grande
  hace despues.
- **Riesgo**: **ninguno**. Es precisamente lo que el banco separa.
- **En contra**: `[DEL OPERADOR]` el b33 se grabo y estuvo **36 segundos estable** con
  `bleEnabled=false` y consola contestando; el cuelgue empezo **al encender el Bluetooth**.
  Eso apunta a **antes** del advertising, o sea a H1/H2/H3. **No puedo verificar esa
  medida** (no toco el nodo); me la ha dado el operador y la uso como tal.

### H5 — Relojes: el cristal de 32 kHz no arranca — ~12 %

- **Que es**: `Bluefruit.begin()` pide **cristal** (`USE_LFXO`, `variant.h:18`) y el
  SoftDevice se queda esperando a que arranque el LFCLK.
- **Por que encaja**: nadie ha medido nunca que el `sd_softdevice_enable()` del nodo
  **devuelva**.
- **En contra, y es fuerte**: `[FUERA]` Nordic: *"The only 'wait state' in the softdevice
  enable is the wait for the lfclk started event... the external crystal do not start as
  expected"* (DevZone 35917) y *"lock up in the SoftDevice code, waiting for the LFCLOCK to
  start"* (DevZone 53888). **Un problema de LFCLK da CUELGUE, no bucle de reinicios.** Y
  lo medido en la placa es un bucle. Ademas `[MEDIDO]` que el nodo usa el mismo cristal
  para el RTC2 del sueno y funciona (`src\power.cpp:62`, `NVIC_EnableIRQ(RTC2_IRQn)`).
- **Como se comprueba**: el entorno `techo_diag_s140v7_rc` del banco hace lo mismo con el
  **RC interno** (lo que usa el cargador de Adafruit). Si con RC tampoco arranca, el reloj
  queda descartado.
- **Riesgo**: **bajo** (una grabacion mas, solo si hace falta).

### H6 — El SoftDevice de la placa esta danado o es de otra version — ~3 %

- **Que es**: la flash del SoftDevice no es la que el firmware espera, o esta corrupta.
- **Por que encaja poco**: `[MEDIDO]` la ficha esta **coherente y completa** en las cuatro
  imagenes: `fwid 0x0100`, `id 0x008C`, `size 0x27000`, y `0x27000` cuadra con
  `MBR 0x1000 + SD_FLASH_SIZE 0x26000`.
- **Como se comprueba**: `sd_ble_version_get()` (contesta el SoftDevice) y el codigo de
  error de `sd_softdevice_enable()`.
- **Riesgo: AQUI SI HAY RIESGO, y hay que decirlo**: si esta hipotesis fuera cierta, el
  arreglo es **grabar un SoftDevice**, y eso puede dejar el nodo sin cargador si se corta a
  mitad (habria que recuperarlo por SWD). Ademas `[FUERA]` actualizar el SoftDevice
  **borra la aplicacion** (Hung Bui, Nordic, DevZone 20724), o sea que hay que volver a
  grabar el firmware despues. **No se propone grabarlo hasta descartar las otras cinco.**
- **Probabilidad estimada**: baja. Es la hipotesis que mas veces ha resultado falsa aqui
  (dos diagnosticos falsos ya).

---

## 4. EL BANCO DE DIAGNOSTICO

### 4.1 Que es y por que no puede estropear el firmware

`_trabajo_ea2oy\diag_ble\` — **proyecto aparte**. `[MEDIDO]` que PlatformIO solo compila lo
que hay debajo de la carpeta del proyecto: el entorno del nodo (`platformio.ini` de
`_trabajo_ea2oy`) tiene su propio `src_dir`, y el banco tiene el suyo. **No comparten ni un
fichero.** Ademas el banco **no usa `sube_buildnum.py`**, asi que **no gasta numero de
compilacion** del firmware.

### 4.2 Que mide (y cual es el comando para verlo en el codigo)

| # | que mide | donde esta en el codigo |
|---|---|---|
| 1 | los **dos juegos** de valores de la ficha | `diag_ble.cpp`, `leeFicha()` y `leeFichaDesplazada()` |
| 2 | **el fwid que contesta el SoftDevice** (`sd_ble_version_get()`) | `diag_ble.cpp`, justo despues de `sd_softdevice_enable()` |
| 3 | el codigo de error de `sd_softdevice_enable()` | idem |
| 4 | el error de **cada uno de los 7 `sd_ble_cfg_set()`** | `diag_ble.cpp`, paso 4 |
| 5 | **EL NUMERO**: `sd_ble_enable()` con base baja -> lee lo que contesta -> vuelve a llamar con ese valor | `diag_ble.cpp`, pasos 5 y 6 |
| 6 | si hubo fallo duro: `CFSR`, `HFSR`, `MMFAR`, `BFAR`, `PC`, `LR`, `R0-R3` | `diag_ble.cpp`, `diagHardFault()` |

### 4.3 Como sobrevive a un reinicio (y como lo he verificado)

- **`.noinit`**: RAM que el arranque del chip no toca. Definida en
  `diag_ble\variants\techo\nrf52840_s140_v7_diag.ld`.
  **Comprobacion**:
  ```powershell
  & "C:\Users\Jesus\.platformio\packages\toolchain-gccarmnoneeabi\bin\arm-none-eabi-readelf.exe" `
    -S "_trabajo_ea2oy\diag_ble\.pio\build\techo_diag_s140v7\firmware.elf" | Select-String "noinit|bss"
  ```
  Sale `.noinit NOBITS 200045c8 0000dc` y `.bss NOBITS 200046a4 ...`: **`.noinit` esta por
  debajo de `__bss_start__`**, asi que el borrado de `.bss` no la toca.
- **Vector del fallo duro**: **comprobacion**
  ```powershell
  & "...\arm-none-eabi-nm.exe" "_trabajo_ea2oy\diag_ble\.pio\build\techo_diag_s140v7\firmware.elf" |
    Select-String "HardFault"
  ```
  Sale `0002dcfc T HardFault_Handler` y `0002dcfc T diagHardFault`: **el vector apunta a mi
  manejador**. (Se hace con `-Wl,--defsym=HardFault_Handler=diagHardFault` porque el del
  core **no es weak** y no se puede redefinir con el mismo nombre.)
- **Copia en el fichero interno**: sobrevive incluso a quitar la corriente. **Comprobado
  que no pisa nada del nodo** `[MEDIDO]`: el sistema de ficheros interno vive en
  `0xED000..0xF3FFF` (`libraries\InternalFileSytem\src\InternalFileSystem.cpp:29` con
  `LFS_FLASH_ADDR 0xED000`, y `...\src\flash\flash_nrf5x.c:33` con `BOOTLOADER_ADDR 0xF4000`),
  y el nodo usa `0xC8000..0xE7FFF` (registro, `src\flog.cpp:37` `kLogBase = 0xC8000`) y
  `0xE8000`/`0xE9000` (configuracion, `src\store.cpp:20` `kPageAddr0 = 0xE8000`).
  **Sin solape.**

### 4.4 COMO SE LEE EL ACTA (el comando, para que lo pueda leer cualquiera)

El banco **ensena el acta por el cable** en el arranque siguiente a la medida (se reinicia
el solo una vez, a proposito, y ese reinicio **no vuelve a medir**: solo ensena). O sea:

- **Con el cable y un terminal** (o la app): abrir el puerto serie del nodo
  (`KACHO_DIAG_BLE`, 115200) y leer. Sale el acta entera, con el titulo
  `############ ACTA DEL DIAGNOSTICO DEL BLUETOOTH ############`.
- **Sin cable**: contar los parpadeos del **LED azul** (tabla en
  `diag_ble\COMO_SE_GRABA_Y_SE_LEE.md`, seccion 4.2).
- **Para repetir la medida sin volver a grabar**: escribir `rep` por el puerto serie.
- **La copia que sobrevive al corte de corriente**: el fichero `/diag_ble.bin` del sistema
  de ficheros interno. **No hay comando para leerlo desde el PC sin abrir el puerto**; se
  lee por el mismo cable.

**Comando exacto (desde el PC, si algun dia se puede usar el puerto):**
```powershell
# NO ejecutado en esta sesion: hay orden permanente de no tocar puertos COM del PC.
# Se deja escrito para cuando esa orden se levante.
python -m serial.tools.miniterm COM<N> 115200
```
Y **sin abrir ningun COM**: el acta se puede volver a leer **grabando otra vez el mismo
banco** (vuelve a arrancar con el acta ya en `.noinit`, la ensena y no mide) o pulsando
RESET (el acta vive en RAM que no se borra).

### 4.5 SI EL SOFTDEVICE FALLA, EL BANCO **REINICIA** (y por que, y que se ve)

Esto hay que decirlo con precision, porque **no** es "se queda colgado":

- **`HardFault_Handler` propio** (`diag_ble.cpp`, `diagHardFault()`): apunta
  `CFSR/HFSR/MMFAR/BFAR/PC/LR/R0-R3` en el acta, hace `__DSB()` y **`NVIC_SystemReset()`**.
  Es decir: **deja el rastro y reinicia**, igual que hacia el core, pero contando **que**
  fallo y **en que direccion**. *(El `for(;;)` que hay detras del reset es codigo
  inalcanzable, no la salida normal.)*
- **Manejador de fallo del SoftDevice** (`diagFaultSD()`): **NO reinicia**. Solo apunta
  `id`, `pc` e `info` y retorna; si el SoftDevice no puede continuar, es el **SoftDevice**
  quien reinicia (documentado: *"If the application returns from the fault handler the
  SoftDevice will call `NVIC_SystemReset()`"*).
- **Cuando el diagnostico TERMINA**, el banco **se reinicia a proposito UNA vez**
  (`NVIC_SystemReset()` al final de `ejecutaDiagnostico`): es lo que hace que el acta salga
  por el cable con el SoftDevice ya apagado. **Como el acta esta cerrada y marcada, el
  arranque siguiente NO vuelve a medir: solo ensena.** El contador `arranque nº N` del acta
  deja ver si el nodo se ha reiniciado mas de la cuenta.
- **PARA DISTINGUIR "COLGADO" DE "APAGADO"** (esto es lo que de verdad protege): antes de
  cada paso peligroso el LED **late** un numero fijo de veces (`ledLatido()`):
  **3** antes de `sd_softdevice_enable()`, **4** antes de los `sd_ble_cfg_set()` y **5**
  antes de la primera `sd_ble_enable()`. Si el nodo se queda **mudo y sin reiniciarse** (el
  caso del LFCLK, que espera y no vuelve), el operador **ve el LED latir, luego pararse**, y
  sabe **en que paso se quedo**. Sin el latido, "colgado" y "apagado" son indistinguibles.

**Y lo que el banco NO hace `[MEDIDO]`**, con el comando para comprobarlo:

- **No llama a `Bluefruit.begin()`.** Se comprueba en el BINARIO:
  ```powershell
  & "C:\Users\Jesus\.platformio\packages\toolchain-gccarmnoneeabi\bin\arm-none-eabi-nm.exe" `
    "diag_ble\.pio\build\techo_diag_s140v7\firmware.elf" | Select-String "_ZN17AdafruitBluefruit5begin"
  ```
  **No devuelve nada**: el simbolo `AdafruitBluefruit::begin(uint8_t, uint8_t)` **no esta en
  el binario**. Lo unico que sale de `AdafruitBluefruit` es su **constructor** (el objeto
  global `Bluefruit` existe, porque la libreria se enlaza por las dos funciones del USB) y
  `printInfo` (`-DCFG_DEBUG=1`). **Un constructor no arranca el SoftDevice.**
- **El banco tiene SU PROPIA `usbRearma()`** (`diag_ble\src\diag_ble.cpp:267`, llamada en la
  `:825`). **NO es la del firmware del nodo** (`src\ble_kiss.cpp:339`): es una copia local
  que **vuelve a montar** el USB despues de apagar el SoftDevice, y es lo que hace posible
  que el arranque siguiente pueda hablar. *(Aclaracion: en el informe del otro investigador
  se dijo que el banco "no llama a `usbRearma()`". **Eso no es exacto**: llama a la suya, que
  hace lo contrario de lo que alli se temia — rearma, no desarma.)*
- **No deja el SoftDevice arrancado**: lo apaga en el paso 7 (`sd_softdevice_disable()`).
- **USA las dos funciones del USB del core** (`usb_softdevice_pre_enable()` y
  `usb_softdevice_post_enable()`): son **los mismos pasos que hace el core** alrededor del
  SoftDevice, y su proposito es **volver a montar** el USB, no desmontarlo para siempre.
  *(Comprobado con `nm`: `_Z25usb_softdevice_pre_enablev` esta en el binario; la de
  `post_enable` queda en linea.)*
- **No relaja la sonda**: si la ficha no pasa la comprobacion, **no llama a nada** y se
  para (codigo 1).

### 4.6 Por que el banco usa `0x20004260` y NO `0x20004000`

**Decision razonada [DEDUCIDO]**: el banco usa **el mismo `0x20004260` que el firmware
grande**, para que lo que mida sea **trasladable**. El `0x20004000` de Meshtastic es una
**hipotesis de arreglo**, no una condicion del experimento: si se cambian dos cosas a la
vez (el origen de RAM **y** el diagnostico) y funciona, **no se sabe por que**.

**Que pasa si el numero sale por encima de `0x20004260`**: el acta lo dice en una linea
(`>>> LA RAM ERA EL PROBLEMA: pide 0x... y el guion le da 0x20004260 (faltan N bytes)`) y el
arreglo es poner `RAM ORIGIN` en el guion al valor que contesto el SoftDevice, redondeado
hacia arriba **con margen**. Candidato bueno: **`0x20004000`** (2 KB de margen,
documentado por Meshtastic para la S140 v7). `0x20006000` (Adafruit) tambien vale pero
desperdicia 8 KB.

**Que pasa si sale por debajo**: la RAM queda descartada; a mirar H1 y H4.

---

## 5. PLAN DE GRABACION, CON EL RIESGO ESCRITO

### 5.1 Que se graba

**Un solo fichero**: `_trabajo_ea2oy\diag_ble\.pio\build\techo_diag_s140v7\firmware.uf2`
(**217.600 bytes** `[MEDIDO]`). Instrucciones completas para el operador (el `adb push`, la
tabla de parpadeos del LED, y como repetir sin volver a grabar con el comando `rep`) estan
en **`_trabajo_ea2oy\diag_ble\COMO_SE_GRABA_Y_SE_LEE.md`**.

### 5.2 Que puede pasar en el peor caso (y como se sale)

| escenario | probabilidad | como se sale |
|---|---|---|
| Arranca, mide y ensena el acta | la normal | nada que hacer: ya esta |
| **Se cuelga o entra en bucle al probar el SoftDevice** | posible (es justo lo que investiga) | **doble toque al reset** -> sale `TECHOBOOT` -> grabar el UF2 del firmware bueno. El banco **no toca el cargador** (esta en `0xF4000`, el banco acaba en `0x41880`), asi que el cargador **siempre** esta ahi |
| Ni arranca ni da USB, pero el LED parpadea | posible | **contar los parpadeos**: es el resultado. Codigo 4 = "se reinicio a mitad" |
| No arranca nada, ni LED | improbable | doble toque al reset. Si no responde, **quitar y poner la bateria** y volver a intentarlo |
| **Se pierde el registro de viaje o la configuracion** | **descartado** `[MEDIDO]` | el banco no toca `0xC8000..0xE9000` (seccion 4.3) |

**Lo que el banco NO hace, por diseno**: no llama a `Bluefruit`, no arranca la radio, no
toca la pantalla, no escribe en el registro, no relaja la sonda, no graba nada por su
cuenta y **no deja el SoftDevice arrancado** (lo apaga en el paso 7).

**Lo que hay que saber antes**: el acta **sobrevive a un reinicio, no a un corte de
corriente**. Si hay que leerla, **no quitar la bateria** entre la medida y la lectura. (La
copia en el fichero interno si sobrevive al corte, pero la via comoda es la RAM.)

### 5.3 Despues de leer el acta, que hacer (segun lo que diga)

| lo que diga el acta | hipotesis | siguiente paso |
|---|---|---|
| `sd_softdevice_enable()` = `0x1001` | H1 | buscar la IRQ con prioridad 0/1/4 antes de `Bluefruit.begin()`, o arrancar el Bluetooth lo primero del `setup()` |
| `PRIMERA sd_ble_enable()` = `4` y la RAM que contesta **> `0x20004260`** | H2 | subir `RAM ORIGIN` a ese valor con margen (`0x20004000` es el candidato) |
| FALLO DURO con `PC` en `0x1000..0x28000` | H3 | asercion del SoftDevice: revisar la configuracion BLE |
| Todo bien hasta el paso 7 (`errBle2 = 0`) | H4 | el fallo esta **despues** de `sd_ble_enable()`, en el firmware grande |
| `sd_softdevice_enable()` no vuelve nunca | H5 | probar el entorno `techo_diag_s140v7_rc` (reloj RC) |
| `fwid` que contesta el SoftDevice **no es `0x0100`** | H6 | replantear todo: la placa no lleva lo que creemos |

**Y mientras tanto**: el nodo se queda como esta, con `bleEnabled` apagado y la sonda
estricta puesta. **Un nodo sin Bluetooth es aceptable; un nodo en bucle no.**

---

## 6. COMPROBACIONES QUE HE HECHO YO (con el comando para repetirlas)

| # | que | comando o fichero:linea | resultado |
|---|---|---|---|
| 1 | Los 4 entornos del firmware compilan | `powershell -NoProfile -File tools\compila_tanda.ps1` | **TANDA CORRECTA: los cuatro en b34** (917.504 / 917.504 / 911.360 / 908.288 B). Empezo en b33 y subio a b34 (el contador sube solo; ver seccion 7) |
| 2 | El banco compila (3 entornos) | `$env:PLATFORMIO_CORE_DIR="$PWD\_pio_core"; platformio run -d diag_ble` (+ `-e techo_diag_s140v7_rc -e techo_diag_s140v6`) | **SUCCESS** los tres. UF2 de **217.600 B**, llega a `0x41880`, 673,9 KB libres antes del cargador |
| 3 | El fallo duro reinicia | `cores\nRF5\utility\debug.cpp:60-64` | `HardFault_Handler -> NVIC_SystemReset()` |
| 4 | El manejador de errores del core esta vacio | `bluefruit.cpp:118-140` + `platform.txt:58` | con `CFG_DEBUG=0` no imprime nada |
| 5 | `sd_ble_enable()` contesta la RAM que necesita | `ble.h:387-433` | *"On return, this will contain the minimum start address..."* |
| 6 | `__data_start__` = el numero que se le pasa | comando (3) de la cabecera | **`0x20004260`** (`__bss_start__` 0x20004A50, `__bss_end__` 0x200131C8) |
| 7 | `.noinit` esta fuera de `.bss` | `readelf -S` sobre el `.elf` del banco | `.noinit` en `0x200045C8` (0xDC B), `.bss` empieza en `0x200046A4` (**por encima**) |
| 8 | El vector del fallo duro apunta a mi manejador | `nm` sobre el `.elf` del banco | `diagHardFault` y `HardFault_Handler` **en `0x0002DCFC`** |
| 9 | El `extern "C"` del framework esta mal puesto | `nm` sobre `libBluefruit52Lib.a` | `T _Z25usb_softdevice_pre_enablev` (**mangled**) |
| 10 | La ficha del SoftDevice en 4 imagenes | comando (1) y (2) de la cabecera | 7.3.0 oficial: `0x27000 / 0x0123 / 0x8C`; nodo: `0x27000 / 0x0100 / 0x8C`; fabrica v6: `0x26000 / 0x00B6 / 0x8C` |
| 11 | El sistema de ficheros interno no pisa al nodo | `InternalFileSystem.cpp:29`, `flash_nrf5x.c:33` vs `flog.cpp:37`, `store.cpp:20` | interno `0xED000..0xF3FFF`; nodo `0xC8000..0xE9000`: **sin solape** |
| 12 | La proteccion (sonda estricta) sigue en el binario | `Select-String` de "BLUETOOTH NO: SOFTDEVICE" dentro del `.uf2` | **presente** |
| 13 | Firma de bucle en un registro del proyecto | `Select-String -Path "tools\ejemplo_registro_crudo.csv" -Pattern "EVT ble 2/11" \| Measure-Object` | **399 apariciones** del marcador "empieza el Bluetooth" en 2243 lineas. Detalle en 6.1 |
| 14 | La proteccion por bateria baja **no** explica el bucle | `src\power.cpp:244-286` | `powerBootCheck` solo duerme tras `kLowReadingsNeeded` lecturas **bajas seguidas**, y en el arranque hace **una**. No puede reiniciar en cada arranque |
| 15 | Donde esta el `fwid` en las TRES fuentes | `_tmp_final2.py` (script de un uso, ya borrado; se rehace con `lee_hex`/`uf2` + `struct.unpack_from`) | hex oficial 7.3.0 `0x3000`: `size 0x27000 fwid 0x0123 id 0x8C`; imagen del nodo `0x3000`: `size 0x27000 fwid 0x0100 id 0x8C`; fabrica v6: `0x26000 / 0x00B6 / 0x8C`. **El `fwid` sale en la misma direccion (`base+0x0C`) en las tres** |
| 16 | Los numeros del nodo son los OFICIALES de la 7.2.0 | `[FUERA]` PDF de release notes de la S140 7.2.0 | *"Flash: 156 kB (0x27000 bytes)"*, *"RAM: 5.6 kB (0x1678 bytes)... minimum"*, **"The Firmware ID of this SoftDevice is 0x0100"** |
| 17 | El bootloader que publica Meshtastic baja el SoftDevice | `[MEDIDO]` sobre `...\firmware-develop\bin\lilygo_techo_bootloader-0.6.1.zip` -> `sd_bl.bin` | `size 0x26000`, `fwid 0x00B6` = **S140 6.1.1**. **No grabarlo** (seccion 8.8) |
| 18 | Meshtastic T-Echo es un build de **v6**, no de v7 | `arm-none-eabi-readelf.exe -S` sobre `C:\Users\Jesus\Desktop\firmware\.pio\build\t-echo-plus\firmware-t-echo-plus-2.7.26.54e0d8d.elf` | `.text` en `0x26000`, `.data` en `0x20006000` -> v6. Y `boards\t-echo.json`: `nrf52840_s140_v6.ld`, `sd_version 6.1.1`, `sd_fwid 0x00B6`. Detalle en la seccion 1.0 |

### 6.1 Sobre la comprobacion 13 (y su limite, que hay que decir)

`[MEDIDO]` `tools\ejemplo_registro_crudo.csv` tiene **2243 lineas** y en el aparecen **399
veces** la marca `EVT ble 2/11: Bluefruit.begin()`, con el tiempo reiniciandose a `0s`/`1s`
una y otra vez, y `EVT boot v0.3.0-dev mode=2` repetido. Esa es la **firma de un nodo
reiniciandose en el arranque del Bluetooth**.

**LIMITE, y lo digo claro**: ese fichero dice `v0.3.0-dev`, o sea que **es de una version
ANTERIOR del proyecto**, no del b34. Lo uso como **indicio de que el bucle existe y tiene
esa forma**, no como prueba de lo que pasa hoy.

### 6.2 Lo que el operador me ha dicho y NO puedo verificar `[DEL OPERADOR]`

- El **b33 grabado en el nodo arranco bien y estuvo 36 segundos estable** con
  `bleEnabled=false` (USB abierto, consola contestando), y se colgo **al encender el
  Bluetooth a mano**. `[DEL OPERADOR]` **No lo puedo verificar**: no toco el nodo.
  Si es cierto (y no tengo motivo para dudarlo), **separa dos cosas**: el firmware grande
  con el Bluetooth apagado es estable, y **el cuelgue esta en el arranque del SoftDevice**,
  no en llevar ese firmware. Eso es exactamente lo que el banco va a medir.
- Que la placa contesta `fwid 0x0100` en `0x300C`. `[DEL OPERADOR]` Coincide con lo que yo
  he medido en **todas** las imagenes de su flash, asi que lo doy por bueno.

---

## 7. FICHEROS QUE HE TOCADO (y que NO)

**Nuevos (todo dentro de `diag_ble\` y `docs\`, nada del firmware del nodo):**

| fichero | que es |
|---|---|
| `_trabajo_ea2oy\diag_ble\platformio.ini` | los 3 entornos del banco + el `--defsym` del fallo duro |
| `_trabajo_ea2oy\diag_ble\src\diag_ble.cpp` | el motor del diagnostico |
| `_trabajo_ea2oy\diag_ble\src\diag_ble.h` | el acta: que se apunta y por que |
| `_trabajo_ea2oy\diag_ble\boards\techo-nrf52840-s140v7-diag.json` | definicion de placa del banco |
| `_trabajo_ea2oy\diag_ble\variants\techo\nrf52840_s140_v7_diag.ld` | guion de enlazado con `.noinit` |
| `_trabajo_ea2oy\diag_ble\variants\techo\variant.{h,cpp}` | **copia** de los del proyecto (el `variants_dir` es relativo al proyecto) |
| `_trabajo_ea2oy\diag_ble\extra_scripts\uf2_diag.py` | empaquetado UF2 (copiado del que funciona) |
| `_trabajo_ea2oy\diag_ble\COMO_SE_GRABA_Y_SE_LEE.md` | instrucciones para el operador |
| `_trabajo_ea2oy\tools\mapa_memoria.py` | herramienta nueva, **solo lectura**: mapa real de un UF2/HEX |
| `_trabajo_ea2oy\docs\INFORME_BLUETOOTH_ARRANQUE.md` | este informe |

**NO he tocado nada del firmware del nodo**: ni `src\`, ni `platformio.ini`, ni
`variants\techo\nrf52840_s140_v7.ld`, ni la sonda. **Tampoco**
`tools\prueba_sonda_softdevice.py` (la corrigio el agente principal) ni ningun fichero de
otros.

**El contador de compilacion subio de b33 a b34.** Eso es correcto y esperado: el contador
sube cuando cambia el codigo del firmware, y `src\ble_kiss.cpp` figura como **fichero
nuevo sin commitear** en el arbol (`git status` lo lista como `?? _trabajo_ea2oy/src/ble_kiss.cpp`).
La compilacion de verificacion lo gasto. **Yo no he cambiado ni una linea del firmware.**

---

## 8. LO QUE NO HE PODIDO AVERIGUAR (no lo cierro por conveniencia)

1. **Las dos discrepancias pequenas de la ficha del SoftDevice.** (a) El hex oficial de
   Nordic y el bootloader de LilyGO traen `2C FF FF FF` (tamano de ficha) **ocho bytes
   antes** del uuid, y **en las imagenes del nodo ese byte no aparece**. (b) Las
   **cabeceras** dicen que el campo `size` esta en `+0x08`
   (`nrf_sdm.h:111`) y las **release notes** dicen *"the size field is located at offset
   4"*. **Ninguna de las dos afecta a lo que se lee** (el `fwid` esta en `base+0x0C` en las
   tres fuentes y su valor es el correcto en las tres: `0x0100` / `0x0123` / `0x00B6`),
   pero **NO RESUELTAS**. El ancla es la placa en vivo mas el PDF oficial, no mi lectura de
   los offsets.
2. **Por que el firmware de la foto del 14-sep tiene la tabla de vectores en `0x26000` y
   un SoftDevice que declara `0x27000`.** `[MEDIDO]`: en `0x27000` de esa imagen estan los
   bytes `BD 72 02 00 E5 72 02 00 ...`, que **no** son una tabla de vectores (no hay
   puntero de pila de RAM). La tabla de verdad esta en `0x26000` (`SP=0x20004770`,
   reset `0x00027040`). **No cuadra y NO RESUELTO.** Es una foto vieja (no el estado
   actual), asi que no cambia el plan, pero queda dicho.
3. **El suelo de RAM exacto de la S140 7.2.0 con la configuracion CONCRETA de este nodo.**
   El **minimo oficial** si se sabe (`0x1678`), pero el numero real depende de MTU, tabla
   de atributos y colas, y **no hay ninguna medicion publicada de esta combinacion**. **Hay
   que medirlo**: para eso esta el banco.
4. **Que prioridades de interrupcion deja RadioLib y el driver de la tinta.** No lo he
   auditado linea a linea. Es **el siguiente trabajo** si el banco da `0x1001`.
5. **Nada del nodo en vivo.** No he tocado ningun puerto COM (orden permanente) ni he
   grabado nada. **No he podido confirmar por mi mismo** la medida de los 36 segundos
   estables; me la ha dado el operador y la uso como `[DEL OPERADOR]`.
6. **Las release notes de la 7.3.0 no las he podido leer** (no son recuperables, esta
   detras de Cloudflare). Su `fwid 0x0123` esta confirmado por tres vias indirectas (bytes
   del hex oficial, `boards.txt` de Adafruit y README del bootloader).
7. **No he verificado el banco en hardware.** Compila, enlaza y los simbolos estan donde
   tienen que estar (`.noinit` fuera de `.bss`, el vector del fallo duro apuntando al
   manejador propio), pero **no se ha ejecutado nunca**. **No digo que "funciona": digo
   que esta escrito, compilado y verificado en el PC.** Lo que pase al grabarlo lo dira la
   placa.
8. **AVISO QUE NO ES MIO PERO QUE HAY QUE TENER EN CUENTA**: `[FUERA]` el
   `lilygo_techo_bootloader-0.6.1.zip` que publica Meshtastic **lleva dentro la S140
   6.1.1** (`sd_bl.bin`: `size 0x26000`, `fwid 0x00B6` — lo he medido yo tambien en
   `C:\Users\Jesus\Desktop\Escritorio\guillermo\firmware-develop\bin\lilygo_techo_bootloader-0.6.1.zip`).
   **Grabarlo bajaria el SoftDevice de 7.2.0 a 6.1.1** y obligaria a reenlazar la
   aplicacion en `0x26000`. **No grabarlo.**

---

## 9. FUENTES EXTERNAS (todas las que sostienen algo de este informe)

**Oficiales de Nordic / Adafruit**

| que sostiene | fuente |
|---|---|
| `SD_FLASH_SIZE = 0x26000`, offsets de la ficha, `MBR_SIZE = 0` (la trampa) | `cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\include\nrf_sdm.h` (local), lineas 65, 107-123, 144; `MBR_SIZE` bueno en `...\include\nrf52\nrf_mbr.h:68` |
| `sd_ble_enable()` es entrada Y salida | `ble.h:387-433` (local) — https://raw.githubusercontent.com/adafruit/Adafruit_nRF52_Arduino/master/cores/nRF5/nordic/softdevice/s140_nrf52_7.3.0_API/include/ble.h |
| **"The Firmware ID of this SoftDevice is 0x0100"**, Flash `0x27000`, RAM minima `0x1678` | PDF oficial de release notes de la **S140 7.2.0**: https://raw.githubusercontent.com/greenlsi/nrf5-sdk/main/components/softdevice/s140/doc/s140_nrf52_7.2.0_release-notes.pdf |
| Los niveles de prioridad que el SoftDevice se reserva (0, 1 y 4) | `nrf_nvic.h:80-83` (local) |
| El fallo duro del core reinicia | `cores\nRF5\utility\debug.cpp:60-64` (local) — https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/cores/nRF5/utility/debug.cpp |
| `LOG_LV1` se compila a nada sin `CFG_DEBUG` | `common_func.h:161-167` + `platform.txt:58` (locales) |
| `FWID 0x0100` = S140 7.2.0 | Nordic DevZone 69286 (respuesta verificada de Vidar Berg): https://devzone.nordicsemi.com/f/nordic-q-a/69286/what-is-s140_nrf52_7-2-0-softdevice-id/284038 |
| `4097 = 0x1001` es prioridad ilegal, NO `INVALID_STATE` | Nordic DevZone 52215: https://devzone.nordicsemi.com/f/nordic-q-a/52215/error-4097-unknown-error-code-at-nrf_sdh_enable_request |
| Actualizar el SoftDevice **borra la aplicacion** | Nordic DevZone 20724 (Hung Bui, verificado): https://devzone.nordicsemi.com/f/nordic-q-a/20724/can-i-update-the-softdevice-without-touching-the-application/80865 |
| Dentro de la misma version mayor no hace falta recompilar | DevZone 20724 (misma) + DevZone 70537 (Torbjorn Ovrebekk) |
| Recomienda usar las cabeceras de la SD que corre | DevZone 82849 (Einar Thorsrud): https://devzone.nordicsemi.com/f/nordic-q-a/82849 |
| Un problema de LFCLK da **cuelgue**, no reinicio | DevZone 35917 y 53888: https://devzone.nordicsemi.com/f/nordic-q-a/35917/sd_softdevice_enable-does-not-return |
| Medicion real de la RAM que pide la SD (`0x200038A0`) y el `0x20004000` de Meshtastic | meshtastic/firmware PR #10903 y `src/platform/nrf52/nrf52840_s140_v7.ld` |
| El bootloader solo pone la app en `0x27000` (v7) / `0x26000` (v6) | README de Adafruit_nRF52_Bootloader: https://github.com/adafruit/Adafruit_nRF52_Bootloader/blob/master/README.md |
| Usar una SD mas vieja que el SDK "may work ... but there is no guarantee" | DevZone 63771 (Hung Bui) |

**Codigo local de otros proyectos (solo lectura, no he tocado nada)**

| que sostiene | fuente |
|---|---|
| `0x20004260` sale del firmware aleman y **no lo valido `sd_ble_enable()`** | `_referencias\t-echo-lora-aprs\t-echo.ld` (`FLASH 0x27000 LENGTH 0xd9000`, `RAM 0x20004260`) + `Makefile:362` (`SOFTDEVICE_HEX := .../s140_nrf52_7.2.0_softdevice.hex`) + `.gitmodules` -> `cfr34k/nrf5-sdk` (SDK **sin** SoftDevice) |
| Meshtastic `t-echo-plus` es un build de **v6** | `C:\Users\Jesus\Desktop\firmware\variants\nrf52840\t-echo-plus\platformio.ini`, `...\boards\t-echo.json`, y `readelf -S` sobre el `.elf` precompilado |
| El `.ld` de la v7 de Meshtastic usa `0x27000` + RAM `0x20004000` | `C:\Users\Jesus\Desktop\firmware\src\platform\nrf52\nrf52840_s140_v7.ld` (con su comentario medido sobre `0x200038A0`) |
| La S140 7.2.0 **no** es la que trae el SDK 17.1.0 por defecto... y su PDF da los numeros oficiales | PDF de release notes (URL arriba) + `C:\Users\Jesus\Desktop\Escritorio\guillermo\firmware-develop\bin\s140_nrf52_7.3.0_softdevice.hex` (el nuestro es 7.3.0) |

**Herramienta propia nueva**: `_trabajo_ea2oy\tools\mapa_memoria.py` — solo lectura.
Saca el mapa real de un UF2 o Intel HEX: regiones contiguas, ficha del SoftDevice y tablas
de vectores candidatas. **Es con lo que he medido toda la seccion 2.**

---

## 10. LO PRIMERO QUE YO HARIA **SIN GRABAR NADA** (una idea que no es mia y es buena)

Lo propone otro investigador de esta sesion y **tiene razon**: el firmware del nodo
**apunta en el registro de viaje en que marca del arranque del Bluetooth se queda**
(`ble_kiss.cpp:548` "BLE inicio 1/6: Bluefruit.begin (SoftDevice)" y `:564` "BLE inicio
2/6: SoftDevice arriba"). O sea:

| donde acabe el registro | que significa |
|---|---|
| en **`1/6`** (o no llega al `2/6`) | el cuelgue esta **DENTRO** de `Bluefruit.begin()` -> H1/H2/H3/H5 |
| en **`2/6`** | **el SoftDevice ARRANCO** y el culpable es lo que viene despues -> H4 |

**Es gratis, no gasta grabacion y descarta H4 de un vistazo.** Se lee con el comando
`log dump` del CLI por el cable (o desde la app), **sin tocar puertos COM del PC**.
`[DEDUCIDO]` — no lo he podido hacer yo porque no toco el nodo, pero el dato esta ahi.
