# POR QUE A MESHTASTIC LE ARRANCA EL BLUETOOTH Y A NOSOTROS NO

### Kacho System / APRS LoRa EA2OY — comparacion de los dos arranques del SoftDevice

**Encargo**: averiguar por que el firmware de Meshtastic SI arranca el Bluetooth en este
hardware y el nuestro NO, comparando los dos arranques del SoftDevice linea a linea, y decir
que hay que cambiar (o copiar/adaptar, y bajo que licencia).

**Reglas respetadas**: **NO se ha grabado nada, NO se ha tocado ningun puerto COM, NO se ha
hecho `git stash`, NO se ha modificado nada dentro de `C:\Users\Jesus\Desktop\firmware`
(solo lectura), NO se ha tocado la sonda del SoftDevice.**
**Este trabajo es SOLO INFORME: no se ha modificado ni una linea de codigo del firmware.**

---

## COMO LEER ESTO

| etiqueta | significa |
|---|---|
| `[MEDIDO]` | lo he comprobado yo. **Debajo va el comando o el fichero+linea para repetirlo.** |
| `[DOC]` | documentacion o cabecera oficial (Nordic / Adafruit), con la ruta. |
| `[DEDUCIDO]` | consecuencia logica de lo medido. No es una medida. |
| `[SUPUESTO]` | encaja, pero no lo he podido comprobar. |
| `[DEL OPERADOR]` | medida hecha por el operador que yo no puedo verificar (no toco el nodo). |

### Los cinco comandos con los que se reproduce todo

```powershell
# 0) El entorno del proyecto (importante: PLATFORMIO_CORE_DIR, si no se compila con OTRO core)
$w = 'C:\Users\Jesus\Desktop\LoRa_APRS_iGate-main\_trabajo_ea2oy'
$env:PLATFORMIO_CORE_DIR = "$w\_pio_core"
$pio = 'C:\Users\Jesus\.platformio\penv\Scripts\platformio.exe'

# 1) La definicion de placa de Meshtastic (aqui esta la clave de todo)
Get-Content 'C:\Users\Jesus\Desktop\firmware\boards\t-echo.json'

# 2) El mapa REAL del binario de Meshtastic ya compilado
python "$w\tools\mapa_memoria.py" `
  'C:\Users\Jesus\Desktop\firmware\.pio\build\t-echo-plus\firmware-t-echo-plus-2.7.26.54e0d8d.hex'

# 3) El mapa REAL de nuestro binario
python "$w\tools\mapa_memoria.py" "$w\.pio\build\techo_plus_s140v7\firmware.hex"

# 4) La prueba de escritorio de la sonda (tiene que dar 0 fallos)
python "$w\tools\prueba_sonda_softdevice.py"

# 5) El diff de los dos nucleos (NO hay diferencias de fondo)
$a = 'C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\libraries\Bluefruit52Lib\src\bluefruit.cpp'
$m = 'C:\Users\Jesus\Desktop\firmware\.pio_core_meshtastic\packages\framework-arduinoadafruitnrf52\libraries\Bluefruit52Lib\src\bluefruit.cpp'
Compare-Object (Get-Content $m) (Get-Content $a)
```

---

## 0. LA RESPUESTA, EN CINCO FRASES

1. **El arranque del SoftDevice es EL MISMO CODIGO.** Meshtastic **no** arranca el SoftDevice a
   mano: llama a `Bluefruit.begin()` del **mismo nucleo de Adafruit** que usamos nosotros, y su
   `bluefruit.cpp` es identico al nuestro **salvo cuatro lineas de un LED**. `[MEDIDO]`
   (compare-object de arriba: la unica diferencia son dos `#ifdef LED_BLUE`).
2. **Por tanto el cuelgue NO puede estar en "como se llama a `sd_softdevice_enable()`"**: los
   dos lo llaman igual, con el mismo reloj, la misma tabla de vectores y la misma ausencia de
   `sd_nvic_*`. La comparacion linea a linea sale **identica** y eso es un resultado, no un
   fracaso. `[MEDIDO]`
3. **La diferencia real es de ENTORNO DE COMPILACION, y es gorda**: el entorno
   **`t-echo-plus` de Meshtastic compila para la SoftDevice S140 v6.1.1** (`fwid 0x00B6`), con
   la **aplicacion en la flash `0x26000`**, mientras que **nuestro firmware compila para la
   S140 v7** (`fwid 0x0100`) con la aplicacion en `0x27000`. `[MEDIDO]` con el comando (1) y (2).
4. **De ahi sale la explicacion del enigma**: la placa del operador **venia de fabrica con
   S140 6.1.1** (`fwid 0x00B6` en `data\rescue\respaldo_nodo_antes_de_flashear_20260913.uf2`),
   o sea **exactamente el SoftDevice para el que Meshtastic compila su `t-echo-plus`**: grabar
   Meshtastic no pide tocar el SoftDevice, y su Bluetooth arranca "solo". Nuestro binario, en
   cambio, **exige la v7** (`0x0100`), que **alguien tuvo que grabar** (el firmware del aleman
   la fusiona en un solo UF2, y la placa hoy la lleva). `[MEDIDO]` para los fwid; `[DEDUCIDO]`
   para la cronologia.
5. **Y la RAM (`0x20004260` la nuestra, `0x20006000` la de Meshtastic) es hipotesis secundaria,
   no la causa principal**: la v7.2.0 pide **`0x1678` bytes de RAM minimos** segun las notas
   oficiales de Nordic, y el firmware del aleman, que **si** arranca Bluetooth en este
   hardware, usa **el mismo `0x20004260`** que nosotros con una configuracion de la misma
   familia. **No la descarto del todo** (nuestra tabla de atributos y nuestros UUIDs piden mas
   que los suyos) y **el numero exacto solo lo dice la placa**. `[DOC]` + `[MEDIDO]`

**Lo que NO he podido cerrar**: *cual* de los dos caminos de fallo (el SoftDevice no arranca /
la aplicacion se rompe justo despues) es el que se lleva el nodo, porque **eso solo lo dice la
placa**. Para eso esta el experimento de la seccion 6, que es **una sola grabacion**.

---

## 1. LA SECUENCIA DE ARRANQUE EN MESHTASTIC, PASO A PASO

### 1.0 Primero: con que se compila el entorno `t-echo-plus` (el dato que lo decide todo)

**`[MEDIDO]`** — `C:\Users\Jesus\Desktop\firmware\boards\t-echo.json`:

```json
"arduino": { "ldscript": "nrf52840_s140_v6.ld" },     <-- LINEA 4
"softdevice": {
  "sd_flags":   "-DS140",
  "sd_name":    "s140",
  "sd_version": "6.1.1",                              <-- LINEA 30
  "sd_fwid":    "0x00B6"                              <-- LINEA 31
}
```

**`[MEDIDO]`** — el entorno `t-echo-plus` usa esa placa, sin cambiarla:
`variants\nrf52840\t-echo-plus\platformio.ini:14` -> `board = t-echo`.

**`[MEDIDO]`** — el binario ya compilado **coincide**: la aplicacion empieza en `0x26000`
(comando (2) de arriba):

```
REGIONES CONTIGUAS (1):
   0x00026000 - 0x000DAAAF     740016 B
TABLA DE VECTORES:
   0x00026000: SP=0x20040000  Reset=0x0009C2A0
```

**`[MEDIDO]`** — y el `.map` que queda en `.pio\build\output.map` lo confirma, con el numero
exacto de la RAM:

```
.pio\build\output.map:16455:  FLASH  0x0000000000026000  0x00000000000c7000  xr
.pio\build\output.map:16456:  RAM    0x00000000002006000  0x000000000003a000  xrw
.pio\build\output.map:36845:  .data  0x00000000002006000  0xc20
```

**Conclusion `[MEDIDO]`**: entorno `t-echo-plus` = **S140 6.1.1** (`fwid 0x00B6`),
**aplicacion en `0x26000`**, **RAM de la aplicacion en `0x20006000`**.

### 1.1 ★ TRES CORRECCIONES A DATOS QUE CIRCULAN POR EL PROYECTO

Estas tres cosas estan escritas en informes anteriores y **son incorrectas**. Van primero
porque condicionan todo lo demas.

| lo que se dijo | lo que hay | como se comprueba |
|---|---|---|
| «la RAM de Meshtastic es `0x20004000`» | **es `0x20006000`** (el `0x20004000` **no existe en ningun fichero del arbol**) | comando (2) y (5); `select-string -pattern '0x20004000' -path <arbol>` no devuelve nada |
| «Meshtastic usa `0x27000` de flash» | **usa `0x26000`** (el linker de `0x27000` que hay en `src\platform\nrf52\nrf52840_s140_v7.ld` **no lo usa nadie**) | `.map` linea 16455 y la tabla de vectores del hex |
| «el binario de Meshtastic va con la S140 7.3.0 / `SD_FLASH_SIZE 0x27000`» | **va con la 6.1.1**: su `SD_FLASH_SIZE` vale `0x26000` (`src\platform\nrf52\softdevice\nrf_sdm.h:144`) | leer esa linea del arbol de Meshtastic |

**Por que importa**: si alguien "copia los numeros de Meshtastic" creyendo que son `0x27000` y
`0x20004000`, **mueve la aplicacion a la zona del SoftDevice v6** y el nodo deja de arrancar
del todo. Los numeros buenos de Meshtastic son **`0x26000` / `0x20006000`**.

### 1.2 La cadena completa (fichero + linea)

Todo el trabajo sucio esta en
`C:\Users\Jesus\Desktop\firmware\.pio_core_meshtastic\packages\framework-arduinoadafruitnrf52\`
(version `1.10601.0`, fork de Meshtastic, HEAD `9ddcbc9`, 2026-04-26) — **el mismo core que el
nuestro**, que esta en `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52`
(version `1.10700.0`).

| # | que pasa | donde (fichero:linea) |
|---|---|---|
| 1 | Meshtastic **no llama al SoftDevice en `setup()`**: deja el Bluetooth apagado y lo enciende el FSM de energia | `src\platform\nrf52\main-nrf52.cpp:190` (`setBluetoothEnable`), llamado desde `src\PowerFSM.cpp:180` (`darkEnter`), `:198`, `:211` (`powerEnter`), `:229`, `:237` |
| 2 | Al encender: crea el objeto y llama a `setup()` | `src\platform\nrf52\main-nrf52.cpp:216-219` |
| 3 | `NRF52Bluetooth::setup()`: primero el ancho de banda, luego `begin()` | `src\platform\nrf52\NRF52Bluetooth.cpp:266-267` |
| 4 | `Bluefruit.begin()` con **UN** periferico y **CERO** centrales | `src\platform\nrf52\NRF52Bluetooth.cpp:267` -> `bluefruit.h:139` (`bool begin(uint8_t prph_count = 1, uint8_t central_count = 0)`) |
| 5 | **`usb_softdevice_pre_enable()`**: suelta NRF_POWER y los eventos VBUS antes de ceder el periferico al SoftDevice. **AQUI se desmonta el USB** | `bluefruit.cpp:288-290` llama a la de `bluefruit.cpp:67-72` |
| 6 | Reloj LFCLK: **cristal de 32 kHz, 20 ppm**, `rc_ctiv=0`, `rc_temp_ctiv=0` | `bluefruit.cpp:293-301` (`#if defined(USE_LFXO)`); `USE_LFXO` lo pone el propio builder si la variante no dice otra cosa |
| 7 | **`sd_softdevice_enable(&clock_cfg, nrf_error_cb)`** | `bluefruit.cpp:319` |
| 8 | `usb_softdevice_post_enable()`: `sd_power_usbdetected_enable/pwrrdy/removed` y un evento a TinyUSB. **AQUI vuelve el USB** | `bluefruit.cpp:322-324` llama a la de `:76-92` |
| 9 | **`uint32_t ram_start = (uint32_t) __data_start__;`** — la base de RAM de la aplicacion **la pone el guion de enlazado**, no una constante | `bluefruit.cpp:347-348` |
| 10 | 7 x **`sd_ble_cfg_set(...)`**, todas con el mismo `ram_start`: UUID de 128 bits, roles, service-changed, tabla de atributos, MTU, event length, cola HVN y cola de write-cmd | `bluefruit.cpp:355, 362, 371, 376, 385, 392, 398, 404` |
| 11 | **`sd_ble_enable(&ram_start)`** — y el valor devuelto **se usa**: si falla, imprime la RAM que hace falta y **`Bluefruit.begin()` devuelve `false`** | `bluefruit.cpp:438-447` |
| 12 | Prioridad de irq del SoftDevice: **`NVIC_EnableIRQ(SD_EVT_IRQn)` directo** (no `sd_nvic_EnableIRQ`) | `bluefruit.cpp:482` |
| 13 | **Tabla de vectores (`SCB->VTOR` / `sd_softdevice_vector_table_base_set`): NO SE TOCA.** Ni Meshtastic ni el nucleo la tocan | comando (5): no hay ni una coincidencia de `VTOR` ni de `sd_softdevice_vector_table` en el codigo de Meshtastic (tampoco en el nucleo) |
| 14 | Si algo falla: `VERIFY_STATUS(...)` **devuelve `false` y sale ahi**, sin deshacer nada. Y el manejador de errores del SoftDevice **solo imprime si `CFG_DEBUG`**, que vale 0 | `bluefruit.cpp:317/319/355/.../447`; `cores\nRF5\verify.h`; `nrf_error_cb` en `bluefruit.cpp:118-140` |
| 15 | Al **apagar** el Bluetooth, Meshtastic **no llama a `sd_softdevice_disable()`**: solo para el advertising y baja la potencia | `src\platform\nrf52\NRF52Bluetooth.cpp:230-246` |

**Resumen `[MEDIDO]`**: Meshtastic hace **exactamente** lo que hace el nucleo de Adafruit, sin
una sola linea propia de SoftDevice. **No hay nada suyo que copiar.**

---

## 2. LA MISMA SECUENCIA EN NUESTRO FIRMWARE, PASO A PASO

| # | que pasa | donde (fichero:linea) |
|---|---|---|
| 1 | `bleLinkInit()` se llama **al final de `setup()`**, despues de radio, pantalla, sensores y registro | `_trabajo_ea2oy\src\main.cpp:378`; `Serial.begin(115200)` en `:269` |
| 2 | La **sonda** lee la ficha del SoftDevice en `0x300C`; si el `fwid` no es exactamente `SD_ESPERADO_FWID`, **`Bluefruit.begin()` NO SE LLAMA** | `src\ble_kiss.cpp:132-134` (lectura), `:194-212` (la sonda), `:654-671` (la puerta) |
| 3 | `Bluefruit.configPrphConn(247, BLE_GAP_EVENT_LENGTH_DEFAULT, 4, 4)` | `src\ble_kiss.cpp:552` |
| 4 | `Bluefruit.begin(1, 0)` | `src\ble_kiss.cpp:554` |
| 5 | `usb_softdevice_pre_enable()` — **igual que Meshtastic** | `bluefruit.cpp:288-290` + `:67-72` |
| 6 | LFCLK **cristal 32 kHz, 20 ppm**, `rc_ctiv=0`, `rc_temp_ctiv=0` — **igual** | `bluefruit.cpp:293-301` + `variants\techo\variant.h` (`USE_LFXO`) |
| 7 | **`sd_softdevice_enable(&clock_cfg, nrf_error_cb)`** — **igual** | `bluefruit.cpp:319` |
| 8 | `usb_softdevice_post_enable()` — **igual** | `bluefruit.cpp:322-324` + `:76-92` |
| 9 | `uint32_t ram_start = (uint32_t) __data_start__;` = **`0x20004260`** (medido en el `.elf`) | `bluefruit.cpp:347-348`; `variants\techo\nrf52840_s140_v7.ld:43` |
| 10 | 7 x `sd_ble_cfg_set(...)` — **iguales** | `bluefruit.cpp:355-404` |
| 11 | `sd_ble_enable(&ram_start)` — **igual** | `bluefruit.cpp:438` |
| 12 | `NVIC_EnableIRQ(SD_EVT_IRQn)` — **igual** | `bluefruit.cpp:482` |
| 13 | Tabla de vectores: **no se toca** — **igual** | idem Meshtastic |
| 14 | Si algo falla, `Bluefruit.begin()` devuelve `false`, y nosotros **si lo miramos** y rearmamos el USB a mano | `src\ble_kiss.cpp:554-561`; `usbRearma()` en `:339-353` |

**Diferencia unica en la llamada**: `configPrphConn(247, EVENT_LENGTH_DEFAULT, 4, 4)`
(nosotros) contra `configPrphBandwidth(BANDWIDTH_MAX)` (Meshtastic, `NRF52Bluetooth.cpp:266`).
Los dos acaban en MTU 247 y event length; nosotros pedimos **4** en las dos colas y Meshtastic
**3** en la de avisos y **1** en la de escritura. `[MEDIDO]` (`bluefruit.cpp:259-281` para
`configPrphBandwidth`; `bluefruit.cpp:161-164` para los valores por defecto).
★ **Eso hace que nuestra peticion de RAM sea mayor, no menor** -> va en contra de la hipotesis
"falta RAM". Ver seccion 3.

---

## 3. LA COMPARACION, PUNTO POR PUNTO

### 3.1 Tabla

| punto | **Meshtastic `t-echo-plus`** | **nosotros `techo_plus_s140v7`** | **¿podria explicar el cuelgue?** |
|---|---|---|---|
| **SoftDevice del build** | **S140 6.1.1** (`sd_version` `boards\t-echo.json:30`, `sd_fwid 0x00B6` `:31`) | **S140 7.3.0** (`boards\techo-nrf52840-s140v7.json:42-43`) | **SI, es la diferencia de verdad** — pero no por "el codigo", sino porque **piden SoftDevices distintos**. Ver 3.2-A. |
| **Flash de la aplicacion** | **`0x26000`** (`.map:16455`) | **`0x27000`** (`variants\techo\nrf52840_s140_v7.ld:25`) | **SI, indirectamente**: `0x26000` solo es correcto si la flash lleva la **v6**; con la **v7.2.0** que lleva la placa, `0x26000` **pisa los ultimos 4 KB de la SoftDevice**. Ver 3.2-A. |
| **RAM de la aplicacion (`APP_RAM_BASE`)** | **`0x20006000`** (`.map:16456`) | **`0x20004260`** (`.ld:43`, `nm` del `.elf`) | **POCO PROBABLE, pero no descartado.** La v7.2.0 pide **`0x1678`** B minimos `[DOC]`, muy por debajo de `0x4260`; y **el aleman funciona con `0x20004260` exactamente igual** (`_referencias\t-echo-lora-aprs\t-echo.ld:9`) con una configuracion parecida. Nuestra tabla de atributos (4096) y nuestros 10 UUIDs si piden mas que los suyos: **el numero exacto lo da `sd_ble_enable()`**. |
| **Reloj LFCLK: fuente** | `NRF_CLOCK_LF_SRC_XTAL` | `NRF_CLOCK_LF_SRC_XTAL` | **NO** (identico) |
| **Reloj LFCLK: `rc_ctiv`/`rc_temp_ctiv`/`accuracy`** | `0` / `0` / `20 PPM` | `0` / `0` / `20 PPM` | **NO** (identico) |
| **Quien elige el reloj** | `USE_LFXO` (builder) | `USE_LFXO` (`variant.h`) | **NO** |
| **`sd_softdevice_enable()`** | `bluefruit.cpp:319`, `VERIFY_STATUS(..., false)` | `bluefruit.cpp:319` (mismo fichero, version 1.10700.0) | **NO** (identico) |
| **Fallo duro / manejador de errores** | `nrf_error_cb` que solo imprime con `CFG_DEBUG` | el mismo, con el mismo `CFG_DEBUG=0` | **NO como causa**, pero **SI como razon de que nadie se entere** |
| **`ble_enable_params_t` / `sd_ble_cfg_set`** | 7 llamadas, `bluefruit.cpp:355-404`, con los valores de `_sd_cfg` | **las mismas 7**, mismos IDs | **NO** en el mecanismo. **SI en el VALOR**: MTU 247 y event length los dos; colas 3/1 vs 4/4. |
| **Opciones de RAM de `sd_ble_enable`** | `p_app_ram_base = __data_start__ = 0x20006000`; el retorno **se mira** (`:439-447`) | igual, con `0x20004260`; el retorno tambien se mira | **NO** — y ojo: **`__data_start__` no es una opcion de RAM, es el borde del guion de enlazado** |
| **`ble_enable_params_t` (API v6, "opciones de RAM")** | **no aplica**: la API v7 usa `sd_ble_cfg_set` + `sd_ble_enable(&ram_base)`. En la API **v6** existia `ble_enable_params_t` con `common_enable_params.vs_uuid_count` / `gatts_enable_params.attr_tab_size` | igual: los dos usamos la API v7 (el `SD_ble_enable` numerado es el mismo) | **NO** |
| **Prioridades de IRQ** | `NVIC_EnableIRQ(SD_EVT_IRQn)` directo (`:482`) | identico | **NO** (identico) |
| **`sd_nvic_*`** | **no se usan** (el nucleo no los llama en `begin()`) | identicos: tampoco | **NO** |
| **Tabla de vectores (`SCB->VTOR`)** | **no se toca** | **no se toca** | **NO** (identico) |
| **`sd_softdevice_vector_table_base_set`** | **no se llama** | **no se llama** | **NO** (identico) |
| **`sd_power_*`** | `sd_power_usbdetected_enable` / `usbpwrrdy` / `usbremoved` (`:78-80`); `sd_power_system_off()` al apagar (`main-nrf52.cpp:500`); `sd_power_mode_set(LOWPWR)` (`:469`) | los **tres primeros**, identicos (`bluefruit.cpp:78-80`); `sd_softdevice_disable()` al apagar (`ble_kiss.cpp:1030`) | **NO** para el arranque. La diferencia esta **al apagar**, no al encender. |
| **RAM no inicializada (`.noinit`)** | **no existe** en el nucleo (`linker\nrf52_common.ld` no tiene `.noinit` ni hay `.noinit` en el arbol de Meshtastic) | **tampoco** | **NO** (ninguno de los dos puede guardar nada en RAM entre reinicios; por eso el banco de diagnostico usa la **flash**) |
| **`sd_ble_cfg_set` que Meshtastic haga y el nucleo no** | **ninguno** | **ninguno** | **NO** |
| **Cuando se enciende el Bluetooth** | **tarde**: lo enciende el FSM de energia al entrar en `powered`/`dark` (`PowerFSM.cpp:180,198,211`), con el nodo ya en marcha | **en `setup()`**: `bleLinkInit()` al final de `setup()` (`main.cpp:378`) | **POSIBLE**, y es la diferencia funcional mas grande. Ver 3.2-C. |
| **USB (TinyUSB)** | `USE_TINYUSB` (el builder lo pone): `TinyUSB_Device_Init(0)` en `cores\nRF5\main.cpp:52` | identico | **NO como mecanismo** (los dos pasan por lo mismo). **SI como detonante**: ver 3.2-B. |
| **Que se hace si `Bluefruit.begin()` falla** | se registra como fallo critico y el nodo sigue | se rearma el USB a mano y se sigue | **NO** |
| **Sonda que impide arrancar** | **no tiene** | **si** (`ble_kiss.cpp:654`), y **se queda** | **NO** es la causa; es la red de seguridad |

### 3.2 Lo que la tabla dice, hipotesis por hipotesis

#### A) **La hipotesis fuerte: el binario pide un SoftDevice y la placa lleva otro.**

`[MEDIDO]`

- Meshtastic `t-echo-plus` -> **S140 6.1.1**, `fwid 0x00B6`, app en **`0x26000`**.
- Nuestro `techo_plus_s140v7` -> **S140 7.3.0**, `fwid 0x0100`, app en **`0x27000`**.
- La placa del operador -> **S140 7.2.0**, `fwid 0x0100`, `size 0x27000` `[DEL OPERADOR]`
  (y `0x0100` = 7.2.0 esta confirmado en el informe `INFORME_S140_fwid_compat.md`, seccion 0,
  con dos respuestas de Nordic).

**Lo que esto explica, y es mucho**:

1. **Por que a Meshtastic le funciona "solo"**: la placa **venia de fabrica con la 6.1.1**
   (`data\rescue\respaldo_nodo_antes_de_flashear_20260913.uf2`, `fwid 0x00B6`) `[MEDIDO en el
   informe anterior]`. Grabar Meshtastic `t-echo-plus` **no requiere tocar el SoftDevice**, y
   su Bluetooth arranca. **No hay ningun truco en su arranque: es que su SoftDevice es el que
   ya estaba.**
2. **Por que a nosotros no**: nuestro binario **exige la v7**, y para tenerla hubo que grabarla.
   La placa la tiene hoy **por el firmware del aleman**, que es el unico de los tres que
   **fusiona el SoftDevice con la aplicacion en un solo UF2** (`Makefile:383-385`, objetivo
   `uf2_sd`, `SOFTDEVICE_HEX := ...s140_nrf52_7.2.0_softdevice.hex` en `:362`). `[MEDIDO]`
3. **Y por que la comparacion "Meshtastic si, nosotros no" es enganosa**: **no es el mismo
   escenario**. Meshtastic corre sobre la 6.1.1 y nosotros sobre la 7.2.0. Si se grabara el
   binario de Meshtastic en la placa **tal como esta hoy** (con la 7.2.0), su aplicacion
   empezaria en `0x26000` y **escribiria encima de la SoftDevice que hoy acaba en `0x27000`**.
   **[SUPUESTO]** que eso es exactamente lo que pasaria: el **bootloader no lo impide** (el
   cargador de Adafruit no compara la direccion de la aplicacion con el `SD size`; el unico
   control que hace el cargador es el `--sd-req` del DFU, y **por UF2 no aplica**), y el
   resultado seria **peor** que un cuelgue. **No lo he probado y NO hay que probarlo.**

**Lo que NO explica**: **por que se cuelga NUESTRO binario con la v7 grabada**. Eso queda
abierto. Pero cambia la pregunta: ya no es "que hace Meshtastic que no hagamos nosotros"
(no hace nada distinto), sino **"que le pasa a este binario v7 en esta placa"**.

#### B) **El detonante concreto: `usb_softdevice_pre_enable()` desmonta el USB y `Bluefruit.begin()` lo vuelve a montar despues del paso 7.**

`[MEDIDO]` — el orden esta en `bluefruit.cpp`:
`288-290` desmonta -> `319` arranca el SoftDevice -> **`322-324` vuelve a montar**.
O sea: si el **paso 7** (`sd_softdevice_enable`) falla o se cuelga, **el USB se queda
desmontado y no vuelve nunca** -> el nodo se queda sin consola. Y si la aplicacion se cuelga
**entre** el 8 y el 11, el USB ya esta montado pero el nodo se reinicia: eso es el
**montar/desmontar** que midio el operador.

Esto **no distingue** a Meshtastic de nosotros (los dos pasan por lo mismo) pero **si explica
el sintoma observado** (bucle y sin consola) sin necesidad de suponer nada. `[DEDUCIDO]`

#### C) **Nuestra diferencia funcional real: encendemos el Bluetooth DENTRO de `setup()`; Meshtastic lo enciende cuando el FSM de energia llega a `powered`/`dark`.**

`[MEDIDO]` — `main.cpp:378` (nosotros) contra `PowerFSM.cpp:180/198/211` (Meshtastic).

Por que podria importar `[DEDUCIDO]`, y encaja con lo medido:

- Nosotros, cuando llamamos a `Bluefruit.begin()`, **ya tenemos encendidos y con IRQ**:
  la radio (SPI), la pantalla de tinta, `Wire`, los sensores, el GPS y el propio USB CDC.
  Meshtastic lo hace **con el nodo ya en marcha pero sin la aplicacion a media inicializacion**.
- El error **`0x1001` (`NRF_ERROR_SDM_INCORRECT_INTERRUPT_CONFIGURATION`) ya esta medido en este
  proyecto** con el b25 `[DEL OPERADOR]`, y su causa documentada es *"una interrupcion
  habilitada con una prioridad ilegal"* `[DOC]` (`nrf_sdm.h:322`). Las prioridades legales
  para el SoftDevice son **0, 1 y 4**; **2 y 3 son ilegales** `[DOC]` (`nrf_nvic.h:79-83`).
- **`USBD_IRQn` NO esta en la lista de interrupciones reservadas del SoftDevice**
  (`nrf_nvic.h:89-100`: POWER_CLOCK, RADIO, RTC0, TIMER0, RNG, ECB, CCM_AAR, TEMP, NVMC, SWI5),
  y el nucleo le pone **prioridad 2** (`Adafruit_TinyUSB_nrf.cpp:69`).
  `[DEDUCIDO]` Si el SoftDevice tambien mira las prioridades de las IRQ **que no son suyas**
  (la comprobacion podria ser sobre el registro completo), ese 2 seria ilegal -> error 4097 ->
  `Bluefruit.begin()` devuelve `false` -> **el USB se queda desmontado** -> lo que se midio.

  ★ **Esto es comprobable en el banco de diagnostico que ya existe**: apunta
  `D_PRIO_POWER` y `D_PRIO_USBD` (`diag_sd\diag_sd_main.h:337-338`) y las IRQ habilitadas
  (`:334`). **Con esos tres numeros se cierra o se descarta.**

- Y hay un dato que **encaja mal** y hay que decirlo: el **firmware del aleman tambien usa
  TinyUSB** (su aplicacion arranca el USB) y **si** le funciona el Bluetooth con `0x20004260`.
  `[SUPUESTO]` su orden de arranque es distinto (SDK de Nordic con `nrf_sdh`, que **si** usa
  `nrf_sdh_ble_default_cfg_set()` y **si** gestiona las prioridades con `sd_nvic_SetPriority`).

#### D) **La RAM: descartada como causa principal, con numeros.**

`[DOC]` Las notas oficiales de la S140 7.2.0 dicen **`RAM: 5.6 kB (0x1678 bytes)`**, *"minimum
required memory; the actual requirements depend on the configuration chosen at
`sd_ble_enable()`"*, mas *"worst-case stack usage for the SoftDevice is 1.5 kB (0x600 bytes)"*
(citado en `_trabajo_ea2oy\..\INFORME_S140_fwid_compat.md`, seccion 0).

`[DEDUCIDO]` `0x1678 + 0x600 = 0x1C78` (7288 B); nuestra configuracion anade la tabla de
atributos (0x1000) y el MTU 247. **Todo cabe de sobra en los `0x4260` = 16992 B que le damos.**

`[MEDIDO]` Y el alemán, **con la misma `0x20004260`**, arranca con una configuracion **muy
parecida a la nuestra pero algo mas barata**: MTU 247 y event length 6 iguales que nosotros
(`config\sdk_config.h:11060/11065`), tabla de atributos de **2048 B** (la mitad que la nuestra,
`:11070`) y **1** UUID de 128 bits (nosotros 10, `:11075`).

★ **Lo que esto significa, dicho con precision**: el caso del alemán demuestra que **`0x20004260`
basta para una configuracion de la misma familia**; **no demuestra** que baste para la nuestra,
que pide mas tabla de atributos y mas UUIDs. La diferencia son unos pocos KB de RAM, y por eso
**la RAM queda como hipotesis secundaria pero NO descartada del todo**: la descarta el numero
oficial (`0x1678` + `0x600` de pila) o la medida del banco, no el caso del alemán.

**Matiz honesto sobre los dos experimentos ya hechos**: `0x20006000` (b32) y `0x20004260` (b33)
Los dos **le dieron a la aplicacion MAS RAM de la que hace falta**, asi que ninguno descarta la
RAM *por si solo*: el b32 le daba al SoftDevice 8 KB mas que el aleman y tambien se colgo, y el
b33 le da exactamente lo del aleman y se cuelga igual. **Lo que falta es el numero exacto, y ese
lo da `sd_ble_enable()`.**

★ **Y el numero exacto se mide, no se supone**: `sd_ble_enable()` **devuelve** la base minima
en su propio parametro `[DOC]` (`ble.h:389-392`), y **hoy nadie lo lee** (el nucleo lo imprime
solo con `CFG_DEBUG`). Ese es el dato 7 del banco de diagnostico.

#### E) **Lo que es identico y por tanto NO puede ser la causa (lista cerrada)**

`[MEDIDO]` con el comando (5) y con la lectura de los dos `bluefruit.cpp`:

- `sd_softdevice_enable()`: misma llamada, mismos parametros, mismo `VERIFY_STATUS`.
- `nrf_clock_lf_cfg_t`: los cuatro campos, identicos.
- `sd_ble_cfg_set()`: las mismas 7 llamadas, los mismos IDs.
- `sd_ble_enable(&ram_start)`: misma llamada.
- Prioridades de IRQ: `NVIC_EnableIRQ(SD_EVT_IRQn)`, identico.
- `sd_nvic_*`: **ninguno de los dos los usa**.
- Tabla de vectores: **ninguno de los dos la toca**.
- `.noinit`: **ninguno de los dos tiene**.

---

## 4. QUE HAY QUE CAMBIAR EN NUESTRO CODIGO

**Conclusion principal: NO hay que copiar nada de Meshtastic.** Su arranque del SoftDevice es
**el mismo nucleo de Adafruit** que ya usamos, y lo unico que se podria "copiar" son dos
constantes de su guion de enlazado que **son las de la S140 v6 y no valen para esta placa**.

Lo que si hay que cambiar, por orden de valor y de riesgo:

### 4.1 ★ LO PRIMERO: dejar de adivinar y MEDIR (no es un cambio de firmware: es una grabacion)

**El cambio mas importante no es de codigo, es de informacion.** Hoy no se sabe si el fallo es
**"el SoftDevice no arranca"** o **"la aplicacion se rompe justo despues"**, y esa pregunta se
contesta con **una sola grabacion** del banco que ya existe. Ver seccion 6.

### 4.2 La pregunta de las dos SoftDevices: **¿se puede usar la v6, como Meshtastic?**

**Se puede, pero NO en este nodo y NO sin grabar la SoftDevice.** Los numeros, para que quede
auditable:

| | SoftDevice v6.1.1 (Meshtastic) | SoftDevice v7.2.0 (la placa) |
|---|---|---|
| `fwid` | `0x00B6` | **`0x0100`** |
| `size` de la ficha | `0x26000` | **`0x27000`** |
| **donde empieza la aplicacion** | **`0x26000`** | **`0x27000`** |
| donde empieza la aplicacion en el firmware del aleman | — | **`0x27000`** (`t-echo.ld:8`) |

**Implicaciones, dichas sin adornos**:

1. Pasar nuestro firmware a la v6 significa: **cambiar el guion de enlazado a `FLASH 0x26000`**
   (el del framework, `cores\nRF5\linker\nrf52840_s140_v6.ld:8`), **cambiar `sd_version` a
   `6.1.1` y `sd_fwid` a `0x00B6`** en `boards\techo-nrf52840-s140v7.json`, y **cambiar
   `SD_ESPERADO_FWID` a `0x00B6`** en `platformio.ini`.
2. **NO se puede mezclar**: el nodo tiene hoy la **v7.2.0**, que ocupa hasta `0x27000`. Un
   binario que empiece en `0x26000` **escribe encima de los ultimos 4 KB de esa SoftDevice**.
   Por tanto, para usar la v6 **hay que grabar tambien el SoftDevice v6.1.1**, y con la
   aplicacion en `0x26000`.
3. **Y eso NO se puede hacer por UF2 sin arriesgar el cargador**: el cargador que lleva la
   placa es el de LilyGO `0.6.1-2-g1224915` `[MEDIDO, INFO_UF2.TXT del nodo]`, que es **de la
   epoca de la v6.1.1**; cambiar de version mayor de SoftDevice con un cargador de la otra
   version es exactamente la operacion que puede dejar la placa **sin poder grabarse por USB**.
   `[DEDUCIDO]` — y el operador ya dijo que **no se graba nada**.
4. **Ventaja real de quedarse en la v7**: no hay que grabar nada, el nodo ya la tiene, el
   aleman demuestra que **la v7.2.0 arranca en este hardware**, y la aplicacion esta donde
   tiene que estar (`0x27000`).

**Recomendacion `[DEDUCIDO]`**: **quedarse en la v7** y buscar la causa con el banco. Cambiar a
la v6 es una operacion **de riesgo alto** (grabar SoftDevice + cargador) para un beneficio que
**no esta demostrado**, porque el fallo puede estar en la aplicacion y no en la SoftDevice.

### 4.3 Los cambios pequeños que SI conviene hacer (cuando se sepa la causa)

Ninguno de estos esta demostrado como causa; son **endurecimientos** que hacen que un fallo se
vea en vez de convertirse en bucle. **Ninguno toca la sonda.**

| # | cambio | fichero:linea | por que |
|---|---|---|---|
| 1 | **Encender el Bluetooth FUERA de `setup()`**, como Meshtastic: marcar la peticion en `setup()` y arrancar el stack en la primera vuelta del bucle | `src\main.cpp:378` -> llamada diferida desde `bleLoop()` (`src\ble_kiss.cpp:906`) | es **la unica diferencia funcional real** que queda. Encender el SoftDevice con la aplicacion ya inicializada (radio, pantalla, `Wire`, USB) es justo el escenario donde encaja el error `0x1001` (prioridades de IRQ). **Riesgo: bajo** (el nodo arrancaria igual; si el Bluetooth falla, se sabe porque la consola ya esta viva). |
| 2 | **Anotar las prioridades de IRQ antes de arrancar** (POWER_CLOCK y USBD) y las IRQ habilitadas, en el registro | `src\ble_kiss.cpp:548` (justo antes del `flogLine("BLE inicio 1/6")`) | si sale `0x1001`, el numero que lo explica queda escrito. **Riesgo: nulo** (son lecturas de registro). |
| 3 | **Publicar la base de RAM que se le da al SoftDevice** (`__data_start__`) en el comando `ble` / `status.ble` | valor ya conocido en compilacion; se publica en `bleResumen()` `src\ble_kiss.cpp:806-834` (el retorno de `begin()` se pierde en `:554`) | hoy **no hay forma de ver por consola con que numero arranco el binario que lleva el nodo**; con esto el operador lo comprueba de un vistazo. El **numero que DEVUELVE** el SoftDevice solo se puede leer llamando a `sd_ble_enable` a mano, y eso es cosa del banco (dato 7). **Riesgo: nulo.** |

★ **Lo que NO hay que hacer**: tocar la sonda, bajar o subir el `ORIGIN` de RAM "a ver si
suena", o grabar un SoftDevice. Las tres cosas ya se han pagado con un nodo en bucle.

---

## 5. LICENCIAS

### 5.1 El estado real

| arbol | licencia | comprobado |
|---|---|---|
| Meshtastic (`C:\Users\Jesus\Desktop\firmware`) | **GPL-3.0** | `LICENSE` empieza por *"GNU GENERAL PUBLIC LICENSE Version 3"* `[MEDIDO]` |
| Este proyecto | **GPL-3.0** | `LICENSE` en la raiz, identico encabezado `[MEDIDO]` |
| Nucleo de Adafruit (`framework-arduinoadafruitnrf52`) | **LGPL-2.1** para el core Arduino y **MIT** para `Bluefruit52Lib` | `libraries\Bluefruit52Lib\src\bluefruit.h:1-25` dice *"The MIT License (MIT) Copyright (c) 2019, hathach (tinyusb.org) for Adafruit"* `[MEDIDO]` |
| Cabeceras del SoftDevice S140 | **licencia Nordic** (permisiva, con aviso de copyright) | las trae el core en `cores\nRF5\nordic\softdevice\` `[MEDIDO]` |

★ **Correccion**: el encargo dice que «aqui `LICENSE` y `NOTICE` ya son GPL-3.0». **`NOTICE` no
existe en este repositorio**: `Get-ChildItem -Recurse -Filter 'NOTICE*'` no devuelve nada ni en
la raiz ni en `_trabajo_ea2oy` `[MEDIDO]`. Solo hay `LICENSE`. Si se acaba copiando codigo de
Meshtastic, **habria que crear el `NOTICE`** (o anadir la atribucion donde el proyecto diga).

### 5.2 ¿Es compatible copiar o adaptar codigo de Meshtastic?

**Si, es compatible**: los dos son **GPL-3.0**, y la GPL-3.0 permite copiar entre obras con la
misma licencia sin conflicto.

### 5.3 Que ficheros serian, y que habria que anotar

**La conclusion del informe es que NO hace falta copiar ningun fichero de Meshtastic**, porque
el arranque del SoftDevice que usa **ya lo tenemos**: es `bluefruit.cpp` del nucleo de Adafruit,
que es **MIT** y que **este proyecto ya distribuye** a traves del framework. Copiar codigo de
Meshtastic aqui **solo anadiria obligaciones sin aportar nada**.

Si aun asi se quisiera adaptar algo (por ejemplo su **guion de enlazado**
`src\platform\nrf52\nrf52840_s140_v7.ld`), seria:

| fichero de Meshtastic | que se llevaria | aviso que habria que anotar |
|---|---|---|
| `src\platform\nrf52\nrf52840_s140_v7.ld` | los dos numeros del `MEMORY` (`0x27000` y **`0x20006000`**) | el fichero **no lleva cabecera de licencia** `[MEDIDO]`: es un `.ld` de 38 lineas sin copyright. Aun asi, al venir de un proyecto GPL-3.0, lo correcto es anotarlo. |
| `src\platform\nrf52\NRF52Bluetooth.cpp` | **nada** (lo unico util es *cuando* llama a `setup()`, y eso esta en `PowerFSM.cpp`, no ahi) | — |

Anotacion que habria que poner (en la cabecera del fichero adaptado y, si se crea, en un
`NOTICE` nuevo):

```
Parte de este fichero esta adaptada de Meshtastic firmware
(https://github.com/meshtastic/firmware), commit 54e0d8d0a,
licencia GPL-3.0. Copyright (c) Meshtastic LLC y colaboradores.
Los cambios respecto al original estan marcados con "KACHO:".
```

★ **Y una advertencia practica**: copiar `src\platform\nrf52\nrf52840_s140_v7.ld` de Meshtastic
**seria un error**, porque su `RAM ORIGIN` es **`0x20006000`** (medido en el `.map` de su
propio binario) y **empeoraria** lo que hoy tenemos. Los numeros de Meshtastic que valen para
esta placa son **cero**.

---

## 6. EL EXPERIMENTO MINIMO (una sola grabacion, sin tocar el USB, con Bluetooth apagado)

### 6.1 Lo que ya esta escrito y sirve (y lo que hay que cambiarle)

En el arbol ya hay un banco de pruebas **`techo_diag_sd`**
(`diag_sd\diag_sd_main.h` + `diag_sd\ble_rastro.h` + `[env:techo_diag_sd]` en
`platformio.ini:312`), con su LEEME (`diag_sd\LEEME.md`). **Cumple las cuatro reglas del
encargo**: no lee el USB, no se reinicia, todo lo cuenta en la flash (`0xE7000`) y respeta la
sonda. **Es la base buena.**

**Pero tiene tres diferencias con el nucleo que hay que corregir**, o el resultado no se podra
comparar con lo que hace el firmware bueno:

| # | que falta | donde | por que importa |
|---|---|---|---|
| 1 | **`usb_softdevice_pre_enable()`** no se llama antes de `sd_softdevice_enable()` | `diag_sd\diag_sd_main.h:449-450` | es **la primera cosa** que hace el nucleo (`bluefruit.cpp:288-290`) y **suelta NRF_POWER**, que es un periferico reservado del SoftDevice. Si el banco no lo hace, **el codigo de error que mida puede ser un falso positivo del propio banco** |
| 2 | **falta `BLE_GATTS_CFG_SERVICE_CHANGED`** | `configuraBle()` en `:348-403` (hace 7 llamadas, ninguna de service-changed) | el nucleo **si** lo pone (`bluefruit.cpp:371`) y `_sd_cfg.service_changed = 1` (`bluefruit.cpp:159`). Es configuracion **de la tabla de atributos**: cambia la RAM que pide el SoftDevice |
| 3 | `central_sec_count = 0` | `:363` | el nucleo pone `(_central_count ? 1 : 0)` = **0** con `begin(1,0)` (`bluefruit.cpp:361`) -> **coincide**, no hay nada que cambiar. Se deja dicho para que no se "arregle" lo que ya esta bien |

★ **Nota honesta sobre el USB del banco**: el banco **no puede** evitar
`TinyUSB_Device_Init(0)` (`cores\nRF5\main.cpp:52`), que el nucleo ejecuta **siempre**, antes
de `setup()`. Eso **no es "tocar el USB" en el sentido peligroso** (lo peligroso es
**desmontarlo** con `usb_softdevice_pre_enable()` y no volver a montarlo), pero **si** deja
`USBD_IRQn` en prioridad 2 y la IRQ encendida — que es justo uno de los sospechosos. El banco
**ya apunta** esos dos numeros (`:334`, `:337-338`). **Es la medida que faltaba.**

### 6.2 El experimento, en una frase

**Grabar UNA vez el banco `techo_diag_sd` (corregido segun 6.1) y leer la flash.**
Con eso se sabe, **sin arriesgar el nodo**:

| si el rastro dice... | entonces | que toca |
|---|---|---|
| `sd_softdevice_enable()` = **0** y `sd_ble_enable()` = **0** | **el SoftDevice arranca bien con nuestra configuracion** | el culpable es **nuestra aplicacion**, no la SoftDevice. Se ataca por la seccion 4.3 (cambio 1: encender el Bluetooth fuera de `setup()`) |
| `sd_softdevice_enable()` = **4097** (`0x1001`) | **prioridades de IRQ ilegales** | mirar `D_IRQ_HABILITADAS` (29), `D_PRIO_POWER` (30) y `D_PRIO_USBD` (31). Si `D_PRIO_USBD == 2`, la seccion 3.2-C queda confirmada |
| `sd_ble_enable()` = **4** (`NO_MEM`) | **falta RAM** | subir el `ORIGIN` de RAM (`variants\techo\nrf52840_s140_v7.ld:43`) **por encima del dato 7** (`D_APP_RAM_DESPUES`), que es el numero que da el propio SoftDevice |
| `sd_softdevice_enable()` = **8** | ya estaba arrancado | problema de estado, no de configuracion |
| `sd_softdevice_enable()` = **4096** | reloj LFCLK mal | probar `USE_LFRC` en la variante |
| un **fallo duro** con su PC y su BFAR | hay un acceso a una direccion que no existe | buscar esa direccion en el `.elf` / `.map` |

### 6.3 ★ La UNICA modificacion que yo anadiria al banco (y por que)

Ademas de las tres correcciones de 6.1, **una prueba que hoy no hace y que contesta la
pregunta de la seccion 3.2-C directamente**:

> **Llamar a `sd_ble_enable()` DOS veces, en dos arranques distintos:**
> **una con la base de RAM que da el guion de enlazado (`0x20004260`) y otra con `0x20006000`**
> (la que da el guion del nucleo para la v6, que es la que usa Meshtastic).
>
> Si con `0x20004260` da `NO_MEM` (4) y con `0x20006000` da 0, la RAM es la causa y ademas
> queda **el numero exacto** en `D_APP_RAM_DESPUES`. Si las dos dan 0, **la RAM queda
> descartada con dos medidas**, no con una teoria.

Esto **no necesita firmware nuevo**: se puede hacer cambiando **una linea** del guion de
enlazado (`variants\techo\nrf52840_s140_v7.ld:43`) y grabando el banco **dos veces**. O mejor,
sin tocar el `.ld`: **pasando la base a mano** en el banco (`ramBase` en
`diag_sd\diag_sd_main.h:475` es una variable local, asi que basta con leerla de un sitio
distinto).

### 6.4 Como se graba y como se lee (para el operador — **lo decide el**)

Nada de esto lo he hecho yo. Esta en `diag_sd\LEEME.md`, secciones 1 a 3, y se resume:

1. Compilar: `powershell -NoProfile -File tools\compila_tanda.ps1 -Entornos techo_diag_sd`
2. Grabar desde el movil: `adb push` del `.uf2` a `/data/local/tmp`, **doble toque al reset**,
   copiarlo a la unidad `TECHOBOOT`.
3. Leer: **doble toque al reset** otra vez, copiar `CURRENT.UF2` y
   `python tools\lee_rastro.py <CURRENT.UF2>`. **Sin abrir ningun puerto COM.**

### 6.5 Por que este banco NO puede dejar el nodo en bucle

`[MEDIDO]` leyendo `diag_sd\diag_sd_main.h`:

- **No lee el USB**: no hay una sola lectura de `Serial` en 546 lineas.
- **No se reinicia**: su `HardFault_Handler` (`:164-180`) escribe los registros en la flash y
  **se queda en un `for(;;)`**, no llama a `NVIC_SystemReset()`.
- **No llama a `Bluefruit.begin()`**: arranca el SoftDevice a mano, paso a paso.
- **No desmonta el USB**: no llama a `usb_softdevice_pre_enable()`.
- Si un paso falla, **se para y late el LED** (`:456-464`, `:492-512`).
- **No toca la sonda**: no incluye `ble_kiss.cpp` ni el codigo del nodo.
- **No entra en la tanda de release**: los cuatro entornos excluyen `src\diag_sd_main.cpp`;
  si alguien se olvidara, **falla el enlazador**, no el nodo (`diag_sd\LEEME.md:95-97`).

---

## 7. COMPROBACIONES QUE HE HECHO YO (con su comando)

| # | comprobacion | resultado |
|---|---|---|
| 1 | `Get-Content firmware\boards\t-echo.json` | `ldscript = nrf52840_s140_v6.ld`, `sd_version 6.1.1`, `sd_fwid 0x00B6` |
| 2 | `mapa_memoria.py` del `.hex` de Meshtastic `t-echo-plus` | region unica `0x26000-0xDAAAF`; tabla de vectores en `0x26000` |
| 3 | `.pio\build\output.map:16455-16456` | `FLASH 0x26000`, `RAM 0x20006000` |
| 4 | `select-string '0x20004000'` en **todo** el arbol de Meshtastic | **0 coincidencias** |
| 5 | `select-string 'SD_FLASH_SIZE'` en el arbol de Meshtastic | `src\platform\nrf52\softdevice\nrf_sdm.h:144: #define SD_FLASH_SIZE 0x27000` (fichero **no incluido** por ningun entorno de esta placa) |
| 6 | `mapa_memoria.py` de nuestro `firmware.hex` (recompilado) | region unica `0x27000-0x95D97`; tabla de vectores en `0x27000`; `Reset_Handler = 0x76D8C` |
| 7 | `arm-none-eabi-nm` de nuestro `.elf` | `__data_start__ = 0x20004260`, `__StackTop = 0x20040000`, `__isr_vector = 0x00027000` |
| 8 | `Compare-Object` de los dos `bluefruit.cpp` | **4 diferencias, todas de `#ifdef LED_BLUE`** |
| 9 | `mapa_memoria.py` de los dos hex de bootloader de Meshtastic | 6.1.1: `SD size 0x26000`, `fwid 0x00B6`. 7.3.0: `SD size 0x27000`, `fwid 0x0123`. Los dos con el cargador en `0xF4000` |
| 10 | `mapa_memoria.py` del hex oficial `s140_nrf52_7.3.0_softdevice.hex` | datos hasta `0x26497`; `SD size` = `0x27000` -> **la app empieza en `0x27000`, no en `0x28000`** (queda confirmado por el propio guion de Adafruit para el nRF52833, `nrf52833_s140_v7.ld:8`) |
| 11 | Alemán `_referencias\t-echo-lora-aprs`: `Makefile:362, 383-385` y `t-echo.ld:8-9` | `s140_nrf52_7.2.0_softdevice.hex`; objetivo `uf2_sd` que **funde SD + aplicacion**; `FLASH 0x27000`, `RAM 0x20004260` |
| 12 | Alemán `config\sdk_config.h:11060/11065/11070/11075` | event length 6, MTU 247, tabla de atributos **2048**, 1 UUID de 128 bits -> **configuracion parecida a la nuestra y funciona** |
| 13 | `python tools\prueba_sonda_softdevice.py` | **FALLOS: 0 — TODO CORRECTO** |
| 14 | Recompilacion de `techo_plus_s140v7` con `PLATFORMIO_CORE_DIR` correcto | `SUCCESS`; `.hex` sigue empezando en `0x27000` |

---

## 8. LO QUE **NO** HE PODIDO AVERIGUAR

1. **Cual de los dos fallos es el que se lleva el nodo**: si `sd_softdevice_enable()` no
   arranca, o si el SoftDevice arranca y se rompe la aplicacion justo despues.
   **Solo lo dice la placa, y no la he tocado.** Es exactamente lo que contesta el banco.
2. **El numero exacto de RAM que pide el SoftDevice para nuestra configuracion.** No se puede
   saber sin arrancarlo (`sd_ble_enable()` lo devuelve, y hoy nadie lo lee). Lo que si se sabe
   es que **`0x1678` es el minimo oficial de la 7.2.0** y que le damos `0x4260`.
3. **Si la placa lleva la SoftDevice del aleman o una grabada aparte.** El `fwid 0x0100` y el
   `size 0x27000` son los de la 7.2.0, y el aleman es el unico de los tres que la fusiona; pero
   **quien la grabo y cuando no lo se**.
4. **Si el binario de Meshtastic `t-echo-plus` se ha llegado a ejecutar en ESTA placa con la
   v7.2.0 grabada.** Si el operador lo probo **antes** de que la placa tuviera la v7, la
   comparacion es entre dos escenarios distintos (y eso es lo que sostiene la seccion 3.2-A).
   **No tengo la fecha, y no puedo obtenerla sin tocar el nodo.**
   ★ Si el operador recuerda **si el Bluetooth de Meshtastic le funciono con la placa tal como
   esta hoy**, ese dato solo cierra o abre la hipotesis A.
5. **Si el `USBD_IRQn` a prioridad 2 es lo que produce el `0x1001`.** Esta documentado que las
   prioridades 2 y 3 son ilegales para las IRQ del SoftDevice, y esta medido que el nucleo le
   pone 2 al USB; lo que **no** esta comprobado es que el SoftDevice mire las IRQ que no son
   suyas. El banco lo contesta con tres numeros.
6. **Si el arranque del aleman (`nrf_sdh` del SDK de Nordic) toca `SCB->VTOR` o las prioridades
   con `sd_nvic_*`.** Su `nrf5-sdk\` esta **vacio** (`[MEDIDO]`: es un submodulo sin clonar), asi
   que **no he podido leer su `nrf_sdh.c`**. Lo hago constar en vez de suponerlo.
7. **Si el cargador que lleva el nodo admite una SoftDevice v7.** El `INFO_UF2.TXT` dice
   "S140 version 7.2.0" pero es **texto fijo del cargador, no una lectura del chip**, y el
   cargador esta fechado en **octubre de 2021**, cuando la 7.2.0 no existia. **Los dos datos
   no pueden ser verdad a la vez y no he podido resolver cual lo es.**

---

## 9. FICHEROS TOCADOS

**Ninguno de codigo.** Este trabajo es **solo informe**, como pedia el encargo.

- **Creado**: `_trabajo_ea2oy\docs\INFORME_MESHTASTIC_VS_NUESTRO_ARRANQUE_SD.md` (este fichero).
- **Recompilado** (sin cambios de fuente): `.pio\build\techo_plus_s140v7\` — el `.hex` sigue
  empezando en `0x27000` y la sonda sigue dando **0 fallos**.
- **Leido, no modificado**: todo `C:\Users\Jesus\Desktop\firmware`,
  `_referencias\t-echo-lora-aprs`, `diag_sd\*`, los dos `framework-arduinoadafruitnrf52`.

---

## 10. LO QUE YO HARIA, EN ESTE ORDEN

1. **Corregir el banco `techo_diag_sd`** con las tres cosas de la seccion 6.1 (`usb_softdevice_pre_enable()`
   y `BLE_GATTS_CFG_SERVICE_CHANGED`; lo del `central_sec_count` no hace falta).
2. **Grabarlo UNA vez** (lo decide el operador) y **leer la flash**. Es la unica forma de saber
   si el problema es el SoftDevice o la aplicacion.
3. **Segun lo que diga**, aplicar **uno** de los cambios de la seccion 4.3 — y el candidato
   numero uno es **encender el Bluetooth fuera de `setup()`**, que es la unica diferencia
   funcional que queda frente a Meshtastic.
4. **No grabar ninguna SoftDevice y no cambiar a la v6.** El riesgo (perder el cargador) es
   alto y el beneficio no esta demostrado: el aleman arranca la v7.2.0 con **nuestros mismos
   numeros**.
5. **No copiar codigo de Meshtastic.** No aporta nada y anade obligaciones de atribucion.
