# POR QUE SE CUELGA EL NODO AL ARRANCAR EL BLUETOOTH — INVESTIGACION COMPLETA

### Kacho System / APRS LoRa EA2OY — 2026-09-17

> **Estado: INVESTIGACION, no arreglo.** No se ha grabado nada. Este documento explica como
> arranca de verdad el SoftDevice en este proyecto (con fichero y linea), ordena las hipotesis
> con su comprobacion y su riesgo, y deja escrito **el build que hay que grabar UNA vez para
> que el nodo CUENTE por que falla en vez de colgarse**.
>
> Convencion de este informe, que se respeta en cada afirmacion:
> **[MEDIDO]** = lo he leido yo de un fichero, un binario o una cabecera, o lo midio el
> operador y esta en el encargo. **[DEDUCIDO]** = se sigue de lo medido con las reglas de la
> documentacion. **[SUPUESTO]** = no tengo con que respaldarlo todavia.

---

## 0. LO QUE HAY QUE SABER EN TREINTA SEGUNDOS

1. **El nodo tiene grabado un S140 7.2.0 (`fwid 0x0100`), no un 7.3.0.** [MEDIDO] El
   `fwid = 0x0100` es de la **S140 7.2.0**, confirmado por Nordic (respuesta verificada,
   [DevZone 69286](https://devzone.nordicsemi.com/f/nordic-q-a/69286/what-is-s140_nrf52_7-2-0-softdevice-id/284038):
   *"For s140 v7.2.0, the FWID is 0x0100"*). El `0x0123` es de la 7.3.0
   ([boards.txt de Adafruit](https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/boards.txt):
   `pca10100.menu.softdevice.s140v7.build.sd_fwid=0x0123`). **El proyecto enlaza contra las
   cabeceras 7.3.0** (`boards\techo-nrf52840-s140v7.json:42: "sd_version": "7.3.0"`) **y la
   placa lleva 7.2.0**: eso es un hecho, no una hipotesis.
2. **`sd_softdevice_enable()` no comprueba la version del SoftDevice**, y no hay ninguna nota
   de Nordic que diga que `sd_ble_enable()` falle por mezclar revisiones. [MEDIDO] Las
   cabeceras 7.2.0, 7.3.0 y 6.1.1 dicen lo mismo y solo listan `INVALID_STATE` para "ya
   inicializado". Es decir: **la hipotesis "el SoftDevice viejo rechaza una aplicacion nueva"
   no tiene respaldo documental**. Lo que si cambia entre revisiones es la **RAM** y el
   **`fwid`** (que afecta al DFU, no al arranque de una aplicacion ya grabada).
3. **Hay un error de 4 KB en la documentacion del proyecto, y explica por que no se entiende
   nada.** [MEDIDO] `variants\techo\nrf52840_s140_v7.ld:27-43` afirma que con la aplicacion en
   `0x20006000` "sus variables caian DENTRO de la RAM del SoftDevice" y que por eso el b33
   bajo a `0x20004260`. **Es exactamente al reves.** La documentacion de Nordic dice, literal:
   *"The SoftDevice RAM region is located between 0x20000000 and APP_RAM_BASE-1 and the
   application's RAM region is located between APP_RAM_BASE and the start of the call stack"*
   (`ble.h`, S140 7.3.0). O sea: **subir `APP_RAM_BASE` le da MAS RAM al SoftDevice; bajarlo
   se la quita**. El b33 **empeoro** la situacion respecto al valor que tenia.
4. **El 4097 (que el proyecto llama `NRF_ERROR_INVALID_STATE`) es en realidad
   `NRF_ERROR_SDM_INCORRECT_INTERRUPT_CONFIGURATION`.** [MEDIDO] `NRF_ERROR_INVALID_STATE` es
   **8**; **4097 = 0x1001** significa *"SoftDevice interrupt is already enabled, or an enabled
   interrupt has an illegal priority level"* (`nrf_sdm.h:322`). Ese numero apunta a
   **interrupciones/prioridades**, no a un SoftDevice averiado, y no a la version.
   ([nrf_error.h](https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/cores/nRF5/nordic/softdevice/s140_nrf52_7.3.0_API/include/nrf_error.h),
   [nrf_error_sdm.h](https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/cores/nRF5/nordic/softdevice/s140_nrf52_7.3.0_API/include/nrf_error_sdm.h))
5. **El `INFO_UF2.TXT` no es texto fijo: es una lectura del chip.** [MEDIDO] El cargador lo
   escribe con `SD_ID_GET(MBR_SIZE)` y `SD_VERSION_GET(MBR_SIZE)`
   ([ghostfat.c](https://github.com/adafruit/Adafruit_nRF52_Bootloader/blob/master/src/usb/uf2/ghostfat.c)).
   Por eso dice la verdad: **la placa lleva 7.2.0**. El encargo lo daba por texto fijo; no lo es.
6. **`sd_ble_enable()` devuelve, ADEMAS del error, la RAM que hace falta de verdad.** [MEDIDO]
   `p_app_ram_base` es `[in, out]` (`ble.h:390-392`, S140 7.3.0): entra con la base de la
   aplicacion y **sale con la RAM MINIMA que pide el SoftDevice**. El nucleo de Adafruit la lee
   y la imprime con `LOG_LV1("CFG", "SoftDevice's RAM requires: 0x%08lX", ram_start)`
   (`bluefruit.cpp:446`) **pero ese log esta compilado fuera** (`platform.txt:58:
   build.debug_flags=-DCFG_DEBUG=0`, y `common_func.h:161-167` convierte `LOG_LV1` en nada).
   **Hoy ese numero se tira a la basura.** Ese es, probablemente, el dato que falta.

---

## 1. COMO ARRANCA DE VERDAD EL SOFTDEVICE EN ESTE PROYECTO (fichero y linea)

### 1.1 La cadena de llamadas, desde el arranque

```
main()                                  cores\nRF5\main.cpp:78
 └─ loop_task()                         cores\nRF5\main.cpp:47
     ├─ TinyUSB_Device_Init(0)          cores\nRF5\main.cpp:52     <-- EL USB YA ESTA VIVO AQUI
     └─ setup()                         src\main.cpp:276
         └─ bleLinkInit()               src\main.cpp:378
             └─ bleAplica(true)         src\ble_kiss.cpp:902
                 ├─ softdevicePareceValido()   src\ble_kiss.cpp:654  (la sonda estricta)
                 └─ bleArrancaStack()          src\ble_kiss.cpp:681
                     ├─ Bluefruit.configPrphConn(247, ...)  src\ble_kiss.cpp:552
                     └─ Bluefruit.begin(1, 0)               src\ble_kiss.cpp:554
```

### 1.2 `AdafruitBluefruit::begin()`, paso a paso

Fichero: `_pio_core\packages\framework-arduinoadafruitnrf52\libraries\Bluefruit52Lib\src\bluefruit.cpp`
(version **1.10700.0**, la que usa `tools\compila_tanda.ps1:40` al apuntar
`PLATFORMIO_CORE_DIR` a `_trabajo_ea2oy\_pio_core`; [MEDIDO] leido de su `package.json`).

| # | linea | que hace | que pasa si falla |
|---|---|---|---|
| 1 | `289` | `usb_softdevice_pre_enable()` | **AQUI EMPIEZA EL PELIGRO.** Desmonta el vigilante de VBUS del USB: `nrfx_power_usbevt_disable()` + `uninit()` + `nrfx_power_uninit()` (`bluefruit.cpp:67-72`). El USB sigue enumerado por hardware, pero **TinyUSB ya no se entera de nada**. |
| 2 | `293-313` | elige el reloj de 32 kHz: `USE_LFXO` -> `XTAL / 0 / 0 / 20 ppm`; `USE_LFRC` -> `RC / 16 / 2 / 250 ppm`; si no hay ninguno, `#error` | En esta placa esta `USE_LFXO` [MEDIDO] `variants\techo\variant.h:18` |
| 3 | `319` | `VERIFY_STATUS( sd_softdevice_enable(&clock_cfg, nrf_error_cb), false )` | **NO se registra nada y el USB NO se vuelve a montar.** Ver 1.4 |
| 4 | `323` | `usb_softdevice_post_enable()` | Solo corre **si el paso 3 fue bien**: `sd_power_usbdetected_enable(true)` etc. (`bluefruit.cpp:76-92`) |
| 5 | `348` | `uint32_t ram_start = (uint32_t) __data_start__` | El simbolo del guion de enlazado. [MEDIDO] En el b33 vale `0x20004260` (`nm` del `.elf`) |
| 6 | `355-432` | **7 x `sd_ble_cfg_set()`**: VS_UUID(10) · ROLE_COUNT(1 perif/0 central) · GATTS_SERVICE_CHANGED · ATTR_TAB_SIZE(0x1000) · CONN_CFG_GATT(MTU 247) · CONN_CFG_GAP(event_len, 1 enlace) · CONN_CFG_GATTS(hvn 4) · CONN_CFG_GATTC(wr 4) | cada una con `VERIFY_STATUS(..., false)`: **devuelve `false` y NADIE sabe cual fallo** |
| 7 | `438` | `uint32_t err = sd_ble_enable(&ram_start)` | `ram_start` **sale** con la RAM minima que pide. Si `err != 0` -> `LOG_LV1("CFG", ...)` (compilado fuera) y `VERIFY_STATUS(err,false)` (linea `447`) |
| 8 | `454` | `sd_ble_opt_set(BLE_COMMON_OPT_CONN_EVT_EXT)` | Data Length Extension |
| 9 | `457-463` | `Periph.begin()`, `Security.begin()`, `sd_ble_gap_device_name_set()` | primer uso real de la API |
| 10 | `469-480` | crea las tareas FreeRTOS "BLE" y "SOC" y sus semaforos | |
| 11 | `482` | `NVIC_EnableIRQ(SD_EVT_IRQn)` | **CMSIS directo**, no `sd_nvic_EnableIRQ()` |
| 12 | `488` | `bond_init()` | lee los enlaces guardados |

### 1.3 Los relojes, en una linea

- **LFCLK**: lo enciende el SoftDevice dentro de `sd_softdevice_enable()` con lo que le pasa el
  nucleo. En este proyecto: **cristal de 32 kHz (XTAL, 20 ppm)** [MEDIDO]
  `variants\techo\variant.h:18` (`#define USE_LFXO`) y `bluefruit.cpp:293-301`. Si el T-Echo
  **no** llevara cristal, el error seria `NRF_ERROR_SDM_LFCLK_SOURCE_UNKNOWN` (**4096**), que es
  un numero distinto del 4097 medido: **el 4097 ya descarta el problema de cristal** [DEDUCIDO].
- **HFCLK**: lo pide TinyUSB al arrancar el periferico USB (`hfclk_enable()` en `dcd_nrf5x.c`).
  El SoftDevice tambien lo usa. No hay conflicto conocido.
- **[SUPUESTO]** no hay nada medido sobre el estado del cristal de 32 kHz en esta placa.

### 1.4 **Que pasa si algo devuelve error: NADA. Y ese es el problema de fondo.** [MEDIDO]

Tres piezas encajadas hacen que el fallo sea **invisible y luego mortal**:

1. `platform.txt:58` -> `build.debug_flags=-DCFG_DEBUG=0`. Con `CFG_DEBUG` a 0,
   `LOG_LV1(...)` se expande a **nada** (`cores\nRF5\common_func.h:161-167`) y `VERIFY_MESS`
   tambien (`cores\nRF5\verify.h:72-74`). O sea: **el mensaje que dice cuanta RAM hace falta no
   se compila**.
2. `nrf_error_cb()` (`bluefruit.cpp:118-140`) **tiene todo el cuerpo dentro de `#if CFG_DEBUG`**.
   Compilado sin debug es una funcion **vacia** [MEDIDO] (`nm` del b33: `nrf_error_cb` existe y
   no hace nada). El SoftDevice la llama cuando **el** falla; al volver, el SoftDevice hace
   `NVIC_SystemReset()` (`nrf_sdm.h:274-275`: *"If the application returns from the fault
   handler the SoftDevice will call NVIC_SystemReset()"*).
3. `HardFault_Handler` del nucleo (`cores\nRF5\utility\debug.cpp:60-64`) hace
   `NVIC_SystemReset()` **a secas, sin dejar rastro**. [MEDIDO] Esta declarado **debil** en el
   arranque (`linker\gcc_startup_nrf52840.S:310: .weak HardFault_Handler`), asi que se puede
   sustituir; hoy no se sustituye.

**Consecuencia [DEDUCIDO]**: cualquier fallo del SoftDevice o falta dura se convierte en
**reinicio silencioso**. El nodo se reinicia, el cargador vuelve a montar su USB, la aplicacion
vuelve a arrancar, vuelve a llamar a `Bluefruit.begin()`... y de ahi el **ir y venir del puerto
USB** que el operador ha visto tres veces. **El "bucle" no es una causa: es el sintoma de un
reinicio que no se puede leer.**

### 1.5 Lo que el firmware bueno SI protege (y por que se queda)

`softdevicePareceValido()` (`src\ble_kiss.cpp:194-212`) lee la ficha del SoftDevice en
`0x3000`/`0x300C` (`SD_MBR_DIR 0x1000 + 0x2000 (+0x0C)`, offsets oficiales de `nrf_sdm.h:99-115`
con `MBR_SIZE 0x1000` de `nrf52\nrf_mbr.h:68`) **antes** de llamar a `Bluefruit.begin()`.
Si no coincide **exactamente** con `SD_ESPERADO_FWID`, no se arranca el Bluetooth y el USB no se
toca. **Esto no se toca en ninguna propuesta de este informe.** Su limite honesto esta escrito
en el propio fichero (lineas 95-97): dice que *hay* un SoftDevice de la revision esperada, no
que vaya a arrancar.

---

## 2. EN QUE SE DIFERENCIA DE LOS DOS FIRMWARES QUE SI FUNCIONAN

**[MEDIDO]** en los tres arboles. Lo importante es que **el aleman (`cfr34k`) usa exactamente
`0x27000` de flash y `0x20004260` de RAM, igual que este proyecto**, y sin embargo arranca.

| | **este proyecto** | **cfr34k/t-echo-lora-aprs** (SI funciona) | **Meshtastic** (SI funciona) |
|---|---|---|---|
| SDK | Arduino de Adafruit 1.10700.0 | nRF5 SDK 17.x, C puro | nRF5 SDK + core de Adafruit |
| SoftDevice del binario | **no lleva** (`firmware.uf2` solo cubre `0x27000-0xEA000`) [MEDIDO] | **si**: `make uf2_sd` fusiona `s140_nrf52_7.2.0_softdevice.hex` | **si**: `s140_nrf52_7.3.0_softdevice.hex` (via paquete) |
| `fwid` esperado | `SD_ESPERADO_FWID=0x0100` (`platformio.ini:209`) | (no aplica: graba el SD el mismo) | `0x0123` |
| FLASH de la app | `0x27000` | `0x27000` (`t-echo.ld:8`) | `0x27000` (`nrf52840_s140_v7.ld`) |
| **RAM de la app** | **`0x20004260`** (b33) | **`0x20004260`** (`t-echo.ld:9`) | **`0x20004000`** |
| Como enciende el SD | `Bluefruit.begin()` (nucleo) | `nrf_sdh_enable_request()` + `nrf_sdh_ble_default_cfg_set()` + `nrf_sdh_ble_enable()` (`main.c:1372-1391`) | `Bluefruit.begin()` (el mismo nucleo) |
| Quien lee la RAM que pide | **nadie** (log compilado fuera) | `nrf_sdh_ble_enable()` **avisa e imprime la base minima** (`nrf_sdh_ble.c`) | `NRF52Bluetooth::setup()` comprueba el retorno de `Bluefruit.begin()` y lo registra como fallo critico |
| USB | TinyUSB 0.18 dentro del core | no usa USB CDC | TinyUSB 0.16/0.18 |

**Lo que esto dice [DEDUCIDO]**: la memoria (flash y RAM) **no** distingue a los que funcionan
de los que no: `cfr34k` usa los mismos numeros. La diferencia esta en **el camino del arranque
del SoftDevice y en quien mira el resultado**. Y hay una diferencia de RAM que si es interesante:
**Meshtastic usa `0x20004000`, que da MAS RAM al SoftDevice que nuestro `0x20004260` (0xA60 =
2656 bytes mas)**. Su comentario en el `.ld` lo dice con todas las letras: *"sd_ble_enable()
reports a requirement well below the old 0x20006000 ORIGIN; every byte of gap is unusable RAM.
0x20004000 keeps a ~2+ KB margin over the reported base"*.

---

## 3. EL REPARTO DE FLASH Y RAM, CON LOS NUMEROS LEIDOS

### 3.1 Flash

| region | desde | hasta | de donde sale |
|---|---|---|---|
| MBR | `0x00000` | `0x00FFF` | `MBR_SIZE 0x1000` (`nrf52\nrf_mbr.h:68`) [MEDIDO] |
| SoftDevice S140 7.2.0 (el de la placa) | `0x01000` | `0x27FFF` | `SD size = 0x27000` en la ficha [MEDIDO en el encargo] |
| **aplicacion** | **`0x27000`** | `0xEA000` | `variants\techo\nrf52840_s140_v7.ld:25` |
| (hueco de la ficha del SD que la app no toca en la practica) | `0x27000` | `0x27FFF` | ver aviso abajo |
| registro de viaje (del nodo) | `0xC8000` | `0xE7FFF` | `src\flog.cpp:37-39` |
| config | `0xE8000` | `0xE9FFF` | `src\config.cpp` |
| **cargador TECBOOT** | `0xEA000` | `0xFDFFF` | `extra_scripts\nrf52_uf2.py:15` |
| settings del cargador | `0xFF000` | | `boards\techo-nrf52840-s140v7.json:46` |

**Aviso honesto sobre los 4 KB de enmedio [MEDIDO + DEDUCIDO]**: la ficha del SoftDevice dice
`SD size = 0x27000`, es decir el SoftDevice **terminaria en `0x28000`**, y la aplicacion empieza
en `0x27000`: se solapan 4096 bytes. Pero **el hex oficial de Nordic de la 7.3.0 solo trae datos
hasta `0x26497`** [MEDIDO con `tools\mapa_memoria.py`] y el guion del aleman (que funciona) usa
el mismo `0x27000`. Conclusion [DEDUCIDO]: **ese solape no es la causa del cuelgue**; el
`SD_FLASH_SIZE` es un "hasta aqui puede llegar", no "hasta aqui llega". Queda dicho para que
nadie lo "arregle" moviendo la aplicacion a `0x28000` sin motivo.

### 3.2 RAM — **aqui esta el error de direccion del proyecto** [MEDIDO]

| | valor | quien |
|---|---|---|
| RAM del SoftDevice | `0x20000000` .. `APP_RAM_BASE - 1` | `ble.h` (7.3.0), literal |
| RAM de la aplicacion | `APP_RAM_BASE` .. `__StackTop` | idem |
| `APP_RAM_BASE` de este proyecto (b33) | **`0x20004260`** | `variants\techo\nrf52840_s140_v7.ld:43` |
| `APP_RAM_BASE` del b32 y anteriores | `0x20006000` | historial |
| `APP_RAM_BASE` del aleman (funciona) | `0x20004260` | `_referencias\t-echo-lora-aprs\t-echo.ld:9` |
| `APP_RAM_BASE` de Meshtastic (funciona) | `0x20004000` | `src\platform\nrf52\nrf52840_s140_v7.ld:31` |
| `__data_start__` real del b33 | `0x20004260` | `nm` del `.elf` [MEDIDO] |
| `.data` + `.bss` del b33 | `2032 + 241072` bytes | `arm-none-eabi-size` [MEDIDO] |
| pila (solo ISR y SoftDevice) | 2048 B en `0x2003F800` | `nrf52_common.ld:53` |

**El comentario del `.ld` del proyecto (lineas 27-43) esta al reves** [MEDIDO frente a la
documentacion]: bajar de `0x20006000` a `0x20004260` **no** aparta las variables de la
aplicacion de la RAM del SoftDevice: **le quita 7520 bytes al SoftDevice y se los da a la
aplicacion**. Si el SoftDevice necesitaba mas de `0x4260`, el b33 lo dejo **peor**, no mejor.
Y si necesitaba menos, el b33 no demuestra nada (el b32 con `0x6000` tambien daba mas RAM al
SoftDevice que `0x4260`, y tambien se colgaba). **En los dos casos el b33 no descarta la RAM:
solo prueba un valor en la direccion equivocada.**

---

## 4. TABLA DE HIPOTESIS (ordenadas por probabilidad estimada)

> Las probabilidades son **estimaciones mias**, no medidas. Estan ordenadas por "cuanto explicaria
> y que barato es comprobarlo", que es lo que sirve para decidir el proximo paso.

### H1 — El SoftDevice no arranca por CONFIGURACION DE INTERRUPCIONES (el 4097), y el cable USB de TinyUSB es el sospechoso numero uno

- **Que es**: `sd_softdevice_enable()` devuelve `0x1001`
  (`NRF_ERROR_SDM_INCORRECT_INTERRUPT_CONFIGURATION`): *"the SoftDevice interrupt is already
  enabled, or an enabled interrupt has an illegal priority level"* (`nrf_sdm.h:322`). El
  SoftDevice solo admite las prioridades **0, 1 y 4** en las interrupciones que se reserva
  (`__NRF_NVIC_SD_IRQ_PRIOS`, `nrf_nvic.h:79-83`). **La prioridad 2 y 3 son ilegales.**
- **Por que encaja con lo medido**: (a) el operador midio **4097** con el b25 [MEDIDO en el
  encargo]; (b) el banco viejo `_BLE_DIAG` midio el mismo 4097 con la S140 6.1.1
  (`_BLE_DIAG\CAUSA_RAIZ.md`); (c) **el core pone la interrupcion del USB en prioridad 2**:
  `NVIC_SetPriority(USBD_IRQn, 2)` (`Adafruit_TinyUSB_nrf.cpp:69`), con el comentario
  *"Priorities 0, 1, 4 (nRF52) are reserved for SoftDevice. 2 is highest for application"*;
  (d) en esta placa el USB **arranca antes** que el SoftDevice (`main.cpp:52`), asi que cuando
  se llama a `Bluefruit.begin()` ya hay un periferico con su interrupcion configurada.
  **[SUPUESTO IMPORTANTE]**: `USBD` **no** esta en la lista de interrupciones reservadas del
  SoftDevice (`nrf_nvic.h:89-100`: POWER_CLOCK, RADIO, RTC0, TIMER0, RNG, ECB, CCM_AAR, TEMP,
  NVMC, SWI5). Si no esta reservada, su prioridad **no** deberia importar... pero el error 4097
  esta medido y hay que explicarlo. La comprobacion del banco de pruebas lo resuelve.
- **Como se comprueba**: el banco `diag_sd` apunta (datos 29, 30, 31) **cuantas** interrupciones
  reservadas estan encendidas y con que prioridad estan `POWER_CLOCK` y `USBD`, **y** el codigo
  de error exacto de `sd_softdevice_enable()`. Si sale 4097, el dato 29/30/31 dice cual.
- **Riesgo para el nodo**: **ninguno**. El banco no arranca el Bluetooth del firmware bueno, no
  se reinicia solo y no lee el puerto. Si falla, se queda parado y late.
- **Probabilidad estimada**: **~30 %**. Es el unico error **concreto y medido** que hay sobre la
  mesa, y el 4097 tiene una causa documentada y comprobable. Baja del 50 % porque el 4097 se
  midio con un SoftDevice que resulto **no ser valido** (`fwid 0xE002` leido en la direccion
  equivocada, `_BLE_DIAG`), y con un SoftDevice invalido el error puede ser otro.

### H2 — El SoftDevice necesita mas RAM de la que le da `0x20004260` (`NRF_ERROR_NO_MEM` = 4)

- **Que es**: la aplicacion reserva para si misma desde `0x20004260`; al SoftDevice le queda
  `0x4260` = **16992 bytes**. Si su configuracion (MTU 247, tabla de atributos `0x1000`, 10
  UUID de 128 bits, colas de 4) necesita mas, `sd_ble_enable()` devuelve **4** y deja en
  `p_app_ram_base` la base minima. El nucleo **tira ese numero** (log compilado fuera).
- **Por que encaja con lo medido**: (a) Meshtastic, que funciona en esta misma placa, reserva
  **`0x20004000` (16384 B)**: **2656 bytes MENOS** para la aplicacion y por tanto **mas** para el
  SoftDevice; (b) el b33 bajo el ORIGIN **en la direccion equivocada**, quitandole RAM al
  SoftDevice, y por eso "no era la causa" no se puede concluir de ese experimento; (c) hay un
  caso casi identico en Nordic DevZone
  ([94744](https://devzone.nordicsemi.com/f/nordic-q-a/94744/upgrading-to-softdevice-7-3-0-from-6-1-1-issue/400068):
  6.1.1 -> 7.3.0, "la aplicacion ni se ejecuta", respuesta verificada: *"This RAM allocation
  solved the issue"*).
- **Como se comprueba**: el banco `diag_sd` llama a `sd_ble_enable()` con la **misma**
  configuracion que el nucleo, desde la **misma** base (`__data_start__`) y apunta **el error y
  la RAM que sale**: datos 6 (`entra`), 7 (`sale`) y T_ERR 9. Si sale `7 > 6`, esta es la causa
  y el numero exacto queda escrito.
- **Riesgo**: **ninguno** (mismo banco).
- **Probabilidad estimada**: **~25 %**. Es barato de comprobar y el numero se mide, no se
  supone. Lo que la baja es que el aleman funciona con `0x20004260` exactamente igual que
  nosotros... aunque **su configuracion de BLE es la de fabrica** (`nrf_sdh_ble_default_cfg_set`
  con los valores por defecto del SDK), **no MTU 247 + tabla 0x1000 + 4 avisos en cola** como
  pide este proyecto. Esa diferencia es real y va en la direccion de esta hipotesis.

### H3 — El cuelgue NO es del SoftDevice: es de la aplicacion justo despues (pantalla, radio, flash, reloj)

- **Que es**: el SoftDevice arranca bien y lo que se rompe es el firmware grande en los
  milisegundos siguientes. El LED/sensor de temperatura se lee por registro directo si
  `bleSoftDeviceIsp()` no esta puesto a tiempo; `flogLine()` **escribe la flash con el NVMC**,
  que esta en la lista de perifericos reservados del SoftDevice (`nrf_nvic.h:98`); la pantalla
  de tinta y el bucle de pintado tardan decenas de segundos.
- **Por que encaja con lo medido**: (a) `bleArrancaStack()` llama a `flogLine("BLE inicio 1/6")`
  **justo antes** de `Bluefruit.begin()` (`ble_kiss.cpp:548`) y a `flogLine("BLE inicio 2/6")`
  justo despues (`:564`): si el nodo se cuelga dentro de `Bluefruit.begin()`, el rastro de la
  flash **tiene que** acabar en `1/6`; si acabara en `2/6`, el culpable no es el arranque del
  SoftDevice; (b) el banco viejo `ble_min` (solo `Bluefruit.begin()` + LED, **sin** aplicacion)
  **arranco y llego al bucle** con la S140 6.1.1 (`_BLE_DIAG\README.md`, "RESULTADO MEDIDO"),
  aunque con el USB mudo; (c) la temperatura por `NRF_TEMP` ya se corrigio a `sd_temp_get()`
  (`sensors.cpp:88-103`), lo que demuestra que este tipo de fallo ya ha pasado una vez en este
  proyecto.
- **Como se comprueba**: **hoy mismo, sin grabar el banco**: leer el registro de viaje del nodo
  y mirar en que marca `BLE inicio n/6` se queda (`LOG DUMP` por el mando del taller, o el
  propio registro tras recuperar el nodo). El banco `diag_sd` lo contesta de forma
  independiente: si sus 7 pasos corren enteros, el SoftDevice esta bien y el culpable es la
  aplicacion.
- **Riesgo**: ninguno (leer el registro no toca nada).
- **Probabilidad estimada**: **~20 %**. Es la hipotesis mas facil de descartar y por eso hay
  que descartarla **primero**: ya existe la instrumentacion.

### H4 — Hay que GRABAR un SoftDevice (el de la placa esta a medias o no es el que el binario cree)

- **Que es**: el SoftDevice grabado no es valido o no esta completo, y por eso no arranca.
- **Por que encaja con lo medido**: (a) es la conclusion del banco viejo
  (`_BLE_DIAG\CAUSA_RAIZ.md`), con `sd_softdevice_enable() = 4097` y una ficha ilegible; (b) el
  `firmware.uf2` de este proyecto **NO lleva SoftDevice** [MEDIDO: solo cubre
  `0x27000-0xEA000`], asi que la zona del SoftDevice **no se ha tocado nunca** desde el
  firmware de fabrica; (c) el operador **nunca ha grabado un SoftDevice** (declarado en el
  encargo).
- **Por que NO encaja bien ahora**: **[MEDIDO por mi, y es el dato nuevo mas importante de este
  informe]** en la copia del firmware de fabrica del nodo
  (`data\rescue\respaldo_nodo_antes_de_flashear_20260913.uf2`, 3728 bloques UF2, 256 B por
  bloque, `0x1000..0xE9FFF`) hay **un SoftDevice S140 6.1.1 COMPLETO y coherente** en
  `0x1004`-`0x27000`: tamano `0x26000`, `id 0x8C`, `fwid 0x00B6`, **version 6.1.1**. Es decir:
  **el nodo SI traia SoftDevice de fabrica, y era el 6.1.1**. Y la ficha que el operador leyo
  despues (`fwid 0x0100`, version 7.2.0) prueba que **alguien grabo un SoftDevice 7.2.0**
  (el firmware del aleman, o Meshtastic, o un `uf2_sd`) entre el respaldo y ahora. O sea: el
  SoftDevice de la placa **ha cambiado** y **no** es el de fabrica.
- **Como se comprueba**: el banco `diag_sd` lee **toda** la ficha (datos 1-5 y 11-15, en las dos
  direcciones posibles) y **la version completa** en `0x3014`, que es el campo que casi nadie
  mira y que dice `7.2.0` o `7.3.0` sin ambiguedad. Un chip sin SoftDevice da tamano 0 o ficha
  imposible.
- **Riesgo**: **ALTO si se graba a ciegas.** Grabar un SoftDevice por encima de otro **sin
  asegurar el cargador** es la unica operacion de todo este informe que puede dejar la placa sin
  poder grabarse por USB. **NO se propone grabar nada en este paso.** Antes de plantearlo hay
  que tener decidido como se recupera (SWD) y que el cargador 0.6.1 del T-Echo esta a salvo.
- **Probabilidad estimada**: **~10 %**. El SoftDevice esta ahi y es coherente; y ademas el
  operador descarto dos veces "chip averiado" por dos errores propios de direccion (`0x200C` en
  vez de `0x300C`). Se queda en la lista porque **es lo unico que explicaria un 4097 con las
  interrupciones bien**, y porque la duda no se puede cerrar sin leer `0x3014`.

### H5 — El SoftDevice de la placa (7.2.0) es incompatible con las cabeceras 7.3.0 del enlazado

- **Que es**: la aplicacion se compila y enlaza contra
  `cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\` (lo elige
  `boards\techo-nrf52840-s140v7.json:42: "sd_version": "7.3.0"`) pero la placa lleva **7.2.0**.
  El encargo pregunta en concreto si `sd_ble_enable()` de una revision **mas antigua** puede
  fallar con una aplicacion enlazada contra cabeceras **mas nuevas**.
- **Respuesta, con la busqueda hecha**: **NO hay ninguna nota oficial que diga eso.** [MEDIDO] En
  las cabeceras 6.1.1, 7.2.0 y 7.3.0, los unicos valores documentados de `sd_ble_enable` son
  `NRF_SUCCESS`, `NRF_ERROR_INVALID_STATE` ("already been initialized"), `NRF_ERROR_INVALID_ADDR`,
  `NRF_ERROR_NO_MEM` y `NRF_ERROR_RESOURCES`. **No existe un "chequeo de version de API"**. Y las
  release notes de la 7.3.0 dicen lo contrario: *"This SoftDevice is binary compatible with the
  s140_nrf52_7.2.1, and memory requirements have not changed. Applications are therefore not
  required to be recompiled"* (transcrito en
  [DevZone 97850](https://devzone.nordicsemi.com/f/nordic-q-a/97850/upgrading-softdevice-s140-7-2-0-to-softdevice-s140-7-3-0-in-existing-applications/416042)).
- **Pero hay dos efectos laterales REALES de la mezcla, y son de este tipo**:
  1. **La RAM**: el requisito de RAM **no aumenta dentro de la misma version mayor**, pero la
     configuracion que pide este proyecto (MTU 247, tabla `0x1000`) es **mucho mayor** que la de
     fabrica. Eso empuja a **H2**, no a una incompatibilidad.
  2. **El `fwid`**: `0x0100` != `0x0123` **si** rompe cosas, pero **fuera del arranque**: el
     cargador valida el `fwid` contra la lista `--sd-req` del paquete DFU
     ([dfu_init.c](https://github.com/adafruit/Adafruit_nRF52_Bootloader/blob/master/src/dfu_init.c))
     y `platform.txt:109` genera `--sd-req {build.sd_fwid}`. Es decir: **grabar por DFU un
     paquete pidiendo `0x0123` en una placa con `0x0100` seria rechazado**. Con UF2 no aplica.
- **Como se comprueba**: el banco mide el `fwid` y la **version** reales y los errores de cada
  llamada. Si `sd_softdevice_enable()` da 0 y `sd_ble_enable()` da 0, **esta hipotesis queda
  muerta** y la mezcla de revisiones no era el problema.
- **Riesgo**: ninguno.
- **Probabilidad estimada**: **~8 %**. Es la hipotesis que el encargo ponia casi como principal,
  y la evidencia documental la deja **casi descartada**; se queda en la lista porque **si**
  produce dos efectos reales (RAM y DFU) que hay que separar.

### H6 — La sonda estricta (`SD_ESPERADO_FWID`) no es el problema, pero su numero merece una correccion

- **Que es**: [`tools\prueba_sonda_softdevice.py`](tools/prueba_sonda_softdevice.py) da **3
  FALLOS** hoy [MEDIDO: lo he ejecutado] porque su tabla espera `fwid 0x0123` para los entornos
  `s140v7`, mientras el proyecto declara `0x0100`. **El que esta bien es el proyecto**: la placa
  lleva 7.2.0 = `0x0100`. El que esta mal es **el nombre del entorno y su comentario**, que
  dicen "s140v7" y "0x0123" como si fueran lo mismo.
- **Por que importa**: no es la causa del cuelgue, pero **es la causa de que el proyecto se haya
  creido dos veces que el chip estaba averiado**: un numero mal puesto hace que la sonda
  rechace un SoftDevice bueno, o que se acepte uno que no lo es.
- **Como se comprueba**: ya esta comprobado (los 3 fallos son de la tabla de la prueba, no del
  firmware). **No se toca la prueba** (regla del encargo): si se corrige, tiene que ser la tabla
  la que aprenda que en este proyecto `s140v7` significa **7.2.0/`0x0100`**, con una nota.
- **Riesgo**: ninguno.
- **Probabilidad de ser la causa del cuelgue**: **~0 %** (es un fichero de escritorio, no entra
  en el binario). Se lista porque **hay que arreglarlo para no volver a perder tiempo**.

### H7 — Alimentacion / pico de corriente al encender la radio del SoftDevice

- **Que es**: el SoftDevice enciende el RADIO y pide HFCLK; si la bateria esta baja o el
  regulador no aguanta, el chip se reinicia.
- **Por que NO encaja**: [MEDIDO] `_BLE_DIAG\CAUSA_RAIZ.md` mide `vbat_raw=2366` (~3465 mV) y
  `MAINREGSTATUS=0x1`, estable antes y despues (3465 -> 3464 mV). Y el nodo se cuelga **tambien
  con el cable puesto**.
- **Como se comprueba**: el banco apunta `MAINREGSTATUS` y `USBREGSTATUS` (datos 32 y 33) antes
  de arrancar el SoftDevice: es un dato gratis que se queda escrito.
- **Riesgo**: ninguno.
- **Probabilidad estimada**: **~5 %**. Va en la lista solo para que quede descartada con un
  numero.

---

## 5. EL PLAN DE DIAGNOSTICO: **UN SOLO BUILD QUE CUENTA POR QUE FALLA**

### 5.1 La idea, en una frase

**Un firmware que no puede colgarse y que no puede callarse**: no lee el USB en ningun momento
(asi no se queda mudo esperando al PC), arranca el SoftDevice **a mano** para apuntar el codigo
de error de **cada** llamada, y escribe todo en **la flash**, que se lee despues copiando un
fichero, **sin abrir ningun puerto COM**.

### 5.2 Que se ha escrito (y que NO se ha grabado)

| fichero | que es |
|---|---|
| `diag_sd\diag_sd_main.h` | **el banco de pruebas entero** (el guion de los 7 pasos) |
| `diag_sd\ble_rastro.h` | la caja negra en la flash (registros con tipo/dato/valor + textos, a prueba de escritura cortada) |
| `src\diag_sd_main.cpp` | relevo de dos lineas: PlatformIO solo compila lo que esta bajo `src\`, asi que el banco entra por aqui. Los CUATRO entornos de release lo excluyen |
| `tools\lee_rastro.py` | **el lector**: saca el rastro del `CURRENT.UF2` del cargador y lo traduce a español |
| `platformio.ini` -> `[env:techo_diag_sd]` | el entorno que compila SOLO el banco |

**Compilado y verificado** [MEDIDO]:
```
RAM:   1.3% (used 3212 bytes from 248832 bytes)
Flash: 3.9% (used 31428 bytes from 815104 bytes)
Converting to uf2, output size: 62976, start address: 0x27000
```
`nm` del `.elf`: `HardFault_Handler` en `0x278e0` (el nuestro, que gana al debil del nucleo),
`hardfaultC` en `0x27888`, `setup` en `0x278e8`, `__data_start__ = 0x20004260`, y **ni un solo
simbolo del firmware del nodo** (ni `bleLinkInit`, ni `epaper`, ni `radioSetup`).

### 5.3 Las cuatro reglas del encargo, cumplidas una a una

| requisito | como se cumple |
|---|---|
| **Bluetooth APAGADO de fabrica; arranque estable** | El banco **no incluye `ble_kiss.cpp` ni llama a `Bluefruit.begin()`**. Arranca el SoftDevice a mano y, si sale bien, **lo apaga** en el paso 7. El firmware del nodo **no cambia ni un byte** por tener este entorno en `platformio.ini`. |
| **Capturar el codigo de error de `sd_softdevice_enable()` y de `sd_ble_enable()`** | T_ERR 1 y T_ERR 9, con el catalogo de codigos traducido en el lector. Ademas los 7 `sd_ble_cfg_set()` uno a uno (T_ERR 2..8), que el nucleo **no** distingue. |
| **Si hay fallo duro, dejar QUE fallo y EN QUE DIRECCION, y que sobreviva** | `HardFault_Handler` propio (gana al debil del nucleo) que guarda **CFSR, HFSR, MMFAR, BFAR y el PC y el LR del marco de pila** en la flash, y **NO reinicia**: se queda parado. En el arranque siguiente el banco **lee lo que dejo escrito** y lo copia a los datos 40-48. |
| **NO tocar el USB en ningun momento** | No hay **ni una** lectura de `Serial` en todo el banco: nadie puede quedarse bloqueado esperando al PC. Lo unico que pasa es lo que hace el core en **todos** los arranques (`TinyUSB_Device_Init`, `cores\nRF5\main.cpp:52`), que es justo lo que hay que medir y que es imposible de evitar en este core. **No se desmonta ni se rearma a mano.** |
| **Respetar la sonda estricta** | `src\ble_kiss.cpp` y `softdevicePareceValido()` **no se tocan**. El banco no arranca el Bluetooth del nodo: hace su propio guion, que ademas **ensena** la ficha del SoftDevice en las dos direcciones posibles. |

### 5.4 Que contesta el banco, y como se traduce a una decision

| Si el rastro dice... | Entonces... | Que hacer despues |
|---|---|---|
| `sd_softdevice_enable()` = **0** y `sd_ble_enable()` = 0 | **El SoftDevice arranca bien con la configuracion del nucleo.** | El culpable es **la aplicacion grande** (H3). Se mira en que marca `BLE inicio n/6` se queda el firmware bueno. |
| `sd_ble_enable()` = **4** (`NO_MEM`) | **La RAM no llega.** El dato 7 trae la base minima. | **Subir** el `ORIGIN` de RAM del `.ld` por encima del dato 7 (con margen). Es un cambio de una linea y **se puede calcular sin grabar a ciegas**. |
| `sd_softdevice_enable()` = **4097** (0x1001) | **Interrupciones/prioridades.** | Los datos 29/30/31 dicen cuantas reservadas estan encendidas y con que prioridad. Se ataca la que este mal. |
| `sd_softdevice_enable()` = **8** | El SoftDevice **ya estaba arrancado** (lo dejo el cargador o un reinicio suave). | Es un problema de estado, no de configuracion: mirar el camino de reinicio. |
| `sd_softdevice_enable()` = **4096** (0x1000) | **No hay cristal de 32 kHz** o el reloj esta mal declarado. | Cambiar a `USE_LFRC` en la variante. |
| La **version** (dato 4) sale 7.2.0 y el `fwid` `0x0100` | Es lo que ya sabemos. | La mezcla 7.2.0/7.3.0 **no es la causa** si los errores salen 0. |
| Sale un **fallo duro** con su CFSR/BFAR/PC | Hay un acceso a una direccion que no existe, y **sabemos cual**. | Se busca esa direccion en el `.map`/`.elf`. |
| **No hay rastro en la pagina** | El nodo ni ha llegado a escribir, o el fichero no cubre la pagina. | El lector lo dice **explicitamente** (con las direcciones que trae el fichero) para no confundirlo con "no hay nada". |

### 5.5 Como se graba y como se lee (paso a paso, para el operador)

> **NADA DE ESTO LO HAGO YO.** Lo decide y lo ejecuta el operador. Aqui no se graba, y no se
> toca ningun puerto COM.

**Preparar el fichero (en el PC, sin tocar el nodo):**
```powershell
cd C:\Users\Jesus\Desktop\LoRa_APRS_iGate-main\_trabajo_ea2oy
powershell -NoProfile -ExecutionPolicy Bypass -File tools\compila_tanda.ps1 -Entornos techo_diag_sd
# queda en:  .pio\build\techo_diag_sd\firmware.uf2      (62 976 bytes, verificado)
```

**Grabar (desde el movil, como siempre):**
1. `adb push .pio\build\techo_diag_sd\firmware.uf2 /data/local/tmp/diag_sd.uf2`
2. Doble toque al reset del nodo -> aparece la unidad **TECHOBOOT** (`0042-0042`).
3. Copiar `/data/local/tmp/diag_sd.uf2` a la unidad. El nodo se reinicia solo.

**Que pasa entonces (y por que no puede quedarse en bucle):**
- El nodo arranca, escribe en la flash el hito de cada paso, arranca el SoftDevice a mano, mide,
  apaga el SoftDevice y **se queda quieto latiendo el LED azul cada segundo**.
- Si un paso falla, **se para ahi y palpita** (3 palpitaciones = fallo en el paso 3,
  5 = fallo en el paso 5).
- **No lee el puerto, asi que no hay nada que pueda quedarse mudo.**

**Leer el resultado (SIN abrir ningun puerto COM):**
1. Doble toque al reset **otra vez** -> vuelve a aparecer **TECHOBOOT**.
2. Copiar `CURRENT.UF2` de la unidad al PC (o pasarsela a `lee_rastro.py` directamente).
3. ```powershell
   python tools\lee_rastro.py E:\CURRENT.UF2
   ```
   (con la letra que tenga la unidad) — o, desde el movil:
   `adb pull /storage/0042-0042/CURRENT.UF2` y luego el mismo comando.

El lector saca, en español y en orden: si hubo **fallo duro** (con CFSR traducido, BFAR y PC), el
**ultimo paso** del arranque anterior **en palabras**, el codigo de error de
`sd_softdevice_enable()` y de `sd_ble_enable()` **con su significado**, y **la RAM que pide el
SoftDevice**.

### 5.6 Por que NO se ha usado el registro de viaje del firmware bueno (que ya existe)

Porque `flog.cpp` **no escribe nada durante el arranque a proposito**
(`src\flog.cpp:18-23`: *"every write goes through flogLine() ... That stall during START-UP
killed the USB CDC port of a node that was otherwise alive"*), y justo lo que hace falta aqui es
escribir **antes** de cada paso peligroso. Ademas el registro es del operador y un banco de
pruebas no debe revolverse con el. El banco usa **su propia pagina** (`0xE7000`), la misma que
uso el banco viejo `_BLE_DIAG`.

**[SUPUESTO, y hay que decirlo]**: el firmware bueno reclama `0xC8000..0xE7FFF` como registro de
viaje, asi que **`0xE7000` cae dentro de su ultima pagina**. Usar el banco **cuesta, como mucho,
las lineas mas viejas del registro**. Se acepta a cambio de poder leer el diagnostico **sin
depender del USB**, que es la leccion central de todo esto. Es la misma eleccion que ya tomo
`_BLE_DIAG`.

---

## 6. EL EJEMPLO MINIMO ("hola mundo") — el mismo banco, en su version minima

El banco **es** el ejemplo minimo: 31 428 bytes de flash (3,9 %), 3 212 bytes de RAM (1,3 %),
**sin** pantalla, GPS, radio, registro ni protocolo. Su unico trabajo es **decidir UNA cosa**:

> **¿el fallo esta en el SoftDevice o en la aplicacion grande?**

Y la decide asi: hace **exactamente** el mismo camino que el nucleo
(`usb` -> `sd_softdevice_enable` -> los 7 `sd_ble_cfg_set` -> `sd_ble_enable`) desde la **misma**
base de RAM (`__data_start__`) y con las **mismas** opciones
(`MTU 247`, `event_len` por defecto, `hvn 4`, `wr 4`, `attr_tab 0x1000`, `vs_uuid 10`).
Si eso sale bien y el firmware bueno se cuelga, **el SoftDevice queda absuelto**.

### 6.1 El codigo (los trozos que deciden)

El fichero completo esta en `diag_sd\diag_sd_main.h`. Estos son los tres trozos que importan:

**(a) El fallo duro deja de ser silencioso** (`diag_sd_main.h`):
```cpp
extern "C" void hardfaultC(uint32_t *marco) {
  rastro::guarda(rastro::T_FALTA, 1, SCB->CFSR);
  rastro::guarda(rastro::T_FALTA, 2, SCB->HFSR);
  rastro::guarda(rastro::T_FALTA, 3, SCB->MMFAR);
  rastro::guarda(rastro::T_FALTA, 4, SCB->BFAR);
  if (marco != nullptr) {
    rastro::guarda(rastro::T_FALTA, 5, marco[6]);  // PC: la instruccion que fallo
    rastro::guarda(rastro::T_FALTA, 6, marco[5]);  // LR
  }
  rastro::texto("FALLO DURO (HardFault): mirar los registros T_FALTA", 0);
  for (;;) { __asm volatile("nop"); }   // NADA de reiniciar
}
extern "C" __attribute__((naked)) void HardFault_Handler(void) {
  __asm volatile("  mov r0, sp        \n"
                 "  b   hardfaultC    \n");
}
```
*(El `HardFault_Handler` del nucleo esta declarado debil en
`linker\gcc_startup_nrf52840.S:310`, asi que este gana; comprobado con `nm` en el `.elf`.)*

**(b) El error de cada llamada, apuntado** (`diag_sd_main.h`):
```cpp
rastro::paso(P_SD_ENABLE, "paso 3: sd_softdevice_enable (XTAL 20 ppm)");
uint32_t e = sd_softdevice_enable(&reloj, nullptr);
rastro::error(E_SD_ENABLE, e);
if (e != 0) { /* se PARA aqui: nada de reintentar con el SD a medias */ }
...
rastro::dato(D_APP_RAM_ANTES, ramBase);   // con lo que ENTRA
e = sd_ble_enable(&ramBase);
rastro::error(E_BLE_ENABLE, e);
rastro::dato(D_APP_RAM_DESPUES, ramBase); // con lo que SALE (la RAM minima que pide)
```

**(c) La caja negra, a prueba de escritura cortada** (`diag_sd\ble_rastro.h`): cada registro son
8 bytes (`u16 tipo | u16 dato | u32 valor`) y la marca de la pagina se escribe **despues** del
primer registro, asi que un corte deja la pagina **ilegible antes que falsa**.

### 6.2 Como se compila y como se graba

```powershell
# compilar (ya hecho y verificado: SUCCESS, 31 428 B de flash)
cd C:\Users\Jesus\Desktop\LoRa_APRS_iGate-main\_trabajo_ea2oy
powershell -NoProfile -ExecutionPolicy Bypass -File tools\compila_tanda.ps1 -Entornos techo_diag_sd

# grabar: lo decide el operador, desde el movil (ver 5.5)
```

### 6.3 Que NO hace este banco (limites honestos)

- **No dice si el enlace BLE funciona**: no crea servicios ni anuncia. Solo dice si el stack
  **arranca**.
- **No prueba la aplicacion grande**: esa es justo la conclusion que se saca **por contraste**.
- **[SUPUESTO] No prueba que el firmware bueno se cuelgue por lo mismo que mide el banco.** Si
  el banco sale limpio, hay que mirar el registro del nodo para saber donde se queda la
  aplicacion (H3).
- **No sustituye a un grabador SWD**: si la placa quedara sin cargador, no hay herramienta fisica
  en este PC (solo `tool-jlink` instalado, **sin adaptador**). Por eso **no se propone grabar
  ningun SoftDevice** en este paso.

---

## 7. COMPROBACIONES QUE HE HECHO YO (con sus numeros)

| # | que he comprobado | como | resultado |
|---|---|---|---|
| 1 | Version del framework que usa la tanda | `package.json` de `_trabajo_ea2oy\_pio_core\...\framework-arduinoadafruitnrf52` | **1.10700.0** (el fork `_pio_core_fork` es 1.10601.0; **no** es el que usa `compila_tanda.ps1`) |
| 2 | De donde sale la version del SoftDevice del enlazado | `boards\techo-nrf52840-s140v7.json` | `sd_version: "7.3.0"`, `sd_fwid: "0x0100"` |
| 3 | Ficha del SD oficial 7.3.0 | `tools\mapa_memoria.py` sobre `s140_nrf52_7.3.0_softdevice.hex` | ficha 44 B, `SD size 0x27000`, `fwid 0x0123`, `id 0x8C`, **version 7.3.0** |
| 4 | Ficha del SD de fabrica del nodo | `data\rescue\respaldo_nodo_antes_de_flashear_20260913.uf2` (3728 bloques, 256 B/bloque, region `0x1000..0xE9FFF`) | hay un **S140 6.1.1 completo**: `SD size 0x26000`, `id 0x8C`, `fwid 0x00B6`, **version 6.1.1** |
| 5 | Diferencias entre fabrica y el hex oficial 6.1.1 de Adafruit | comparacion byte a byte | **no coinciden** (9122 bytes iguales de 151016): el de fabrica es un 6.1.1 **distinto** (otra build). [DEDUCIDO] no es un problema, pero invalida suponer que son el mismo fichero |
| 6 | `__data_start__` real del firmware bueno | `arm-none-eabi-nm` sobre `.pio\build\techo_plus_s140v7\firmware.elf` | **`0x20004260`**; `.data` = 2032 B, `.bss` = 241072 B; pila en `0x2003F800` |
| 7 | El `firmware.uf2` del nodo NO lleva SoftDevice | `mapa_memoria.py` | una sola region: **`0x27000-0x95DFF`**, familia `0xADA52840`. Y el `.hex` empieza en `0x27000` (`:020000022000DC` + `:10700000...`) |
| 8 | `HardFault_Handler` es sustituible | `nm` del `.elf` + `gcc_startup_nrf52840.S:310` | `W HardFault_Handler` (**debil**) y su cuerpo es `NVIC_SystemReset()` (`utility\debug.cpp:60`) |
| 9 | Los avisos de error estan compilados fuera | `platform.txt:58` + `common_func.h:161` + `verify.h:56` | `-DCFG_DEBUG=0` => `LOG_LV1` y `VERIFY_MESS` son **nada**; `nrf_error_cb` (`bluefruit.cpp:118`) queda **vacio** |
| 10 | `prueba_sonda_softdevice.py` | ejecutado | **3 FALLOS**, todos por su tabla `0x0123` frente al `0x0100` del proyecto. **No lo he tocado** |
| 11 | El banco `diag_sd` compila | `pio run -e techo_diag_sd` | **SUCCESS**: 31 428 B flash (3,9 %), 3 212 B RAM (1,3 %), UF2 de 62 976 B en `0x27000` |
| 12 | El banco no lleva nada del firmware del nodo | `nm` del `.elf` | `HardFault_Handler 0x278e0`, `hardfaultC 0x27888`, `__data_start__ 0x20004260`, y **cero** simbolos de `bleLinkInit`/`epaper`/`radioSetup` |
| 13 | El lector no se cree basura | `python tools\lee_rastro.py` sobre 2 ficheros | sobre el firmware de fabrica detecta que las letras "GAIM" son **casualidad** (tipo de registro 36, invalido) y **no** inventa un diagnostico. Ese fallo lo encontro la prueba, no la teoria |
| 14 | Los cuatro entornos de release siguen compilando | `tools\compila_tanda.ps1` | **los cuatro SUCCESS** y **los cuatro con b34 dentro** (ver §9) |
| 15 | El banco **no** entra en el firmware del nodo | `nm` del `.elf` de `techo_plus_s140v7` b34 | **cero** simbolos `rastro::*` y `hardfaultC`; y si estan los suyos: `__data_start__ 0x20004260`, `bleLinkInit 0x37e04` |
| 16 | El verificador del proyecto | `tools\verifica_memoria.ps1` | **FALLOS: 0** (venia con 1 fallo **preexistente**: 7 referencias por numero de linea en `Cerebro_Faketec_APRS_Igate_EA2OY\ESTADO.md`, que prohibe el propio proyecto; corregidas por nombre de funcion) |

---

## 8. LO QUE NO HE PODIDO AVERIGUAR, Y POR QUE

1. **El codigo de error REAL de `sd_softdevice_enable()` en esta placa con el SoftDevice 7.2.0
   que lleva ahora.** Es, literalmente, lo que hay que medir y no se puede medir sin grabar. El
   **4097** que hay apuntado es del episodio con el SoftDevice que resulto no ser valido.
2. **Cuanta RAM pide de verdad el SoftDevice con la configuracion del nucleo.** El numero existe
   (`sd_ble_enable` lo devuelve) pero **el nucleo lo tira** porque el log esta compilado fuera.
   Se sabra con el banco. Meshtastic deja una pista (**"well below 0x6000"**, usa `0x4000`),
   pero es de **su** configuracion, no de la nuestra.
3. **Por que el 4097 con la S140 6.1.1 en `_BLE_DIAG`**, si `USBD` no figura entre las
   interrupciones reservadas. **No encontrado** en la documentacion. El banco lo resuelve
   apuntando que interrupcion reservada esta encendida y con que prioridad.
4. **La tabla completa de `fwid` por revision del S140.** Solo tengo filas **verificadas**:
   `6.1.1 -> 0x00B6`, `7.2.0 -> 0x0100` (Nordic), `7.3.0 -> 0x0123` (Adafruit). Las de 7.0.x y
   7.1.0 **no las he encontrado**: segun Nordic estan en las release notes de
   `<sdk_root>\components\softdevice\s140\doc\`, que **no son accesibles** (los PDF de
   `docs.nordicsemi.com` e `infocenter.nordicsemi.com` responden 403 desde aqui).
5. **Si el cargador TECBOOT 0.6.1 (LilyGO) deja el SoftDevice arrancado al saltar a la
   aplicacion.** El codigo de Adafruit **si** llama a `disable_softdevice()` antes de
   `bootloader_app_start()`, pero **el de LilyGO es otro binario** y no tengo su fuente. Si lo
   dejara arrancado, `sd_softdevice_enable()` devolveria **8** (`INVALID_STATE`). El banco lo
   distingue con un numero.
6. **Que hay exactamente en `0xE7000` del firmware de fabrica** (tiene las letras "GAIM" por
   casualidad). Es una anecdota, pero dejo dicho que **el lector ya no se la cree**.
7. **Si el cuelgue deja el SoftDevice arrancado o no.** No se puede saber sin volver a colgar el
   nodo. El banco lo dice **en el arranque siguiente** con el dato 8
   (`sd_softdevice_is_enabled()` no esta: lo que si queda es el resultado del enable anterior).

---

## 9. FICHEROS TOCADOS Y ESTADO DE LA COMPILACION

**Nuevos (el banco de pruebas, y nada del firmware del nodo):**
- `diag_sd\diag_sd_main.h` — el banco entero
- `diag_sd\ble_rastro.h` — la caja negra en la flash
- `src\diag_sd_main.cpp` — el relevo de dos lineas (excluido en TODOS los entornos de release)
- `tools\lee_rastro.py` — el lector
- `docs\INFORME_BLUETOOTH_SOFTDEVICE.md` — este informe

*(Se uso una herramienta temporal para sacar la ficha del SoftDevice del firmware de fabrica y
**se ha borrado** al terminar: no queda codigo suelto en `tools\`.)*

**Modificados:**
- `platformio.ini` — un entorno nuevo (`[env:techo_diag_sd]`) y la exclusion
  `-<diag_sd_main.cpp>` en los **siete** `src_filter`/`build_src_filter` que ya existian, para
  que el banco **no pueda** entrar en el firmware del nodo.
- `Cerebro_Faketec_APRS_Igate_EA2OY\ESTADO.md` — **no** por este encargo, sino porque el
  verificador del proyecto daba **1 FALLO preexistente**: 7 referencias por numero de linea
  (prohibidas por la regla del propio proyecto). Sustituidas por el **nombre de la funcion**
  (`menuShort` / `menuLong`), que no se queda obsoleto cuando el fichero crece.
  **`tools\verifica_memoria.ps1` pasa de `FALLOS: 1` a `FALLOS: 0`.**

**NO tocados (a proposito):** `src\ble_kiss.cpp` (la sonda estricta se queda tal cual),
`tools\prueba_sonda_softdevice.py`, los guiones de enlazado, `tools\compila_tanda.ps1`,
`tools\verifica_memoria.ps1`. **No se ha grabado nada. No se ha tocado ningun puerto COM.**

**Compilacion final [MEDIDO]**: `tools\compila_tanda.ps1` -> **TANDA CORRECTA**, los cuatro
entornos con **b34** dentro:

| entorno | numero | tamano del .uf2 | SHA256 (16) |
|---|---|---|---|
| `faketec_sx1262_433` | b34 | 917 504 B | `69CCAC39DDA15977` |
| `faketec_e22p_433` | b34 | 917 504 B | `85F1F6E6E960622A` |
| `techo_s140v7` | b34 | 911 360 B | `19ED1C706AB75265` |
| `techo_plus_s140v7` | b34 | 908 288 B | `B8195942974E5172` |
| `techo_diag_sd` (**el banco**) | — (no gasta numero) | **62 976 B** | — |

*(Nota de taller: la primera pasada de la tanda fallo en `faketec_sx1262_433` por una carrera al
subir el contador — el mismo sintoma que ya esta documentado en el proyecto con el PID de la
tanda. Repetida, los cuatro salieron SUCCESS y con el mismo numero. El contador quedo en b34.)*

**Sobre el contador de compilacion**: sube solo al cambiar el codigo, y **es correcto** que
suba: ha cambiado `src\`. El entorno del banco **no** lleva `sube_buildnum.py`, asi que **no
gasta numero**.

---

## 10. LO QUE YO HARIA, EN ESTE ORDEN (y lo que NO haria)

1. **Grabar `techo_diag_sd` UNA vez** y leer el rastro. Es un solo ciclo de doble toque, no
   puede dejar el nodo en bucle y contesta la pregunta que lleva tres builds sin respuesta.
2. **Mirar el registro de viaje del firmware bueno** en la marca `BLE inicio n/6` donde se queda
   (**sin grabar nada**: es leer el registro que el nodo ya tiene). Si acaba en `1/6`, el cuelgue
   esta dentro de `Bluefruit.begin()`; si acabara en `2/6`, el SoftDevice arranco y el culpable
   es la aplicacion.
3. **Segun lo que diga el rastro**, aplicar **una** cosa:
   - `NO_MEM` -> subir el `ORIGIN` de RAM del `.ld` por encima de la base que devuelva
     `sd_ble_enable()` (y **corregir el comentario del `.ld`**, que hoy explica la direccion al
     reves).
   - `4097` -> atacar la interrupcion senalada (mirar si es `USBD` en prioridad 2).
   - **todo 0** -> el SoftDevice esta absuelto: el trabajo es en la aplicacion.
4. **Corregir la documentacion del `fwid`**: `s140v7` en este proyecto significa **7.2.0 =
   `0x0100`**, y `0x0123` es **7.3.0**. Y decidir si el entorno se llama "s140v7" o
   "s140v720", porque el nombre es lo que ha hecho creer dos veces que el chip estaba averiado.

**Lo que NO haria:**
- **No grabar un SoftDevice** (ni el de fabrica ni el 7.3.0) hasta tener resuelta la vuelta
  atras: es la unica operacion que puede dejar la placa sin cargador, y no hay adaptador SWD.
- **No bajar mas el `ORIGIN` de RAM** (ni volver a `0x20006000` sin medir): la documentacion dice
  que eso le quita RAM al SoftDevice.
- **No tocar la sonda estricta** para "ver que pasa": es lo unico que ha evitado el bucle.
- **No mover la aplicacion a `0x28000`** por el solape de 4 KB con la ficha del SoftDevice: el
  aleman funciona en `0x27000` y el hex oficial no llega ahi.
