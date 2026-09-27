# INFORME — Origen de RAM ORIGIN 0x20004260 y reglas de SoftDevice S140 (compatibilidad + FWID)

Fecha: 2026-09-17. Todas las citas entre comillas son **verbatim en ingles** (fuente original).
Todo dato lleva URL. Lo no comprobado se marca **NO CONFIRMADO**.

---

## A) ORIGEN DE `RAM ORIGIN = 0x20004260`

### A.0 Resumen en una linea

`0x20004260` **no sale de Meshtastic, ni de MeshCore, ni del core de Adafruit, ni de PlatformIO**.
Sale de **un unico proyecto**: `cfr34k/t-echo-lora-aprs`, fichero **`t-echo.ld`** (firmware del T-Echo
en C con el SDK nRF5, no Arduino). Todos los demas usan **`0x20006000`**.

### A.1 El commit de Meshtastic `deb7c274` NO introduce `0x20004260` (es solo un movimiento)

Commit: "Cleanup NRF s140 Softdevice variants (#4252)", Tom Fifield, 2024-07-08.

- URL JSON: https://api.github.com/repos/meshtastic/firmware/commits/deb7c274c4864812f4ebc936fefb8f2f5ccffaea
- URL patch: https://github.com/meshtastic/firmware/commit/deb7c274c4864812f4ebc936fefb8f2f5ccffaea.patch

Mensaje (verbatim): *"The nrf52 boards that depend on the v7 softdevice all use the same code and
linker files. Rather than duplicate the code, keep it all together with the platform."*

Que hace exactamente (25 ficheros, +10/-86):

| accion | fichero |
|---|---|
| rename 100% | `variants/wio-sdk-wm1110/nrf52840_s140_v7.ld` → **`src/platform/nrf52/nrf52840_s140_v7.ld`** |
| rename 100% | `variants/xiao_ble/softdevice/*.h` → `src/platform/nrf52/softdevice/*.h` |
| delete | `variants/wio-tracker-wm1110/nrf52840_s140_v7.ld` (identico a los otros dos) |
| delete | `variants/xiao_ble/nrf52840_s140_v7.ld` (identico) |
| **cambio real** | `src/platform/nrf52/softdevice/nrf_sdm.h`: `#define SD_FLASH_SIZE 0x26000` → **`0x27000`** |
| ini | `variants/{wio-sdk-wm1110,wio-tracker-wm1110,xiao_ble}/platformio.ini`: `board_build.ldscript = src/platform/nrf52/nrf52840_s140_v7.ld` y `-Isrc/platform/nrf52/softdevice` |

Los dos `.ld` borrados y el renombrado tienen el MISMO contenido (blob `6aaeb4034fe9e750cbe7621567c23c5e9bf76681`):

```
FLASH (rx)     : ORIGIN = 0x27000, LENGTH = 0xED000 - 0x27000
RAM (rwx) :  ORIGIN = 0x20006000, LENGTH = 0x20040000 - 0x20006000
```

O sea: en este commit **la RAM es 0x20006000** (no 0x20004260), y el commit explica de donde sale
`0x27000` en flash: **`SD_FLASH_SIZE`** de la cabecera del SoftDevice v7 (el mismo commit lo
actualiza de 0x26000 a 0x27000).

El commit **padre** (`e1bf4c32f3bade256667e24f706010505a115a9c`, "Update to SoftDevice 7.3.0 for
wio-sdk-wm1110 and wio-tracker-wm1110") es el que **crea** esos `.ld` y cambia los board JSON:
`"sd_version": "6.1.1" / "sd_fwid": "0x00B6"` → `"sd_version": "7.3.0" / "sd_fwid": "0x0123"`.
URL: https://github.com/meshtastic/firmware/commit/e1bf4c32f3bade256667e24f706010505a115a9c.patch

### A.2 Scripts de enlazado localizados (ruta exacta + MEMORY)

| # | Repo | Fichero | URL | FLASH | RAM | tiene 0x20004260? |
|---|---|---|---|---|---|---|
| 1 | meshtastic/firmware (master) | `src/platform/nrf52/nrf52840_s140_v7.ld` | https://raw.githubusercontent.com/meshtastic/firmware/master/src/platform/nrf52/nrf52840_s140_v7.ld | `0x27000` `LENGTH = 0xED000 - 0x27000` | **0x20006000** | NO |
| 2 | meshtastic/firmware (borrado en deb7c274) | `variants/xiao_ble/nrf52840_s140_v7.ld` (y `variants/wio-tracker-wm1110/...`, `variants/wio-sdk-wm1110/...`) | https://github.com/meshtastic/firmware/commit/deb7c274c4864812f4ebc936fefb8f2f5ccffaea.patch | `0x27000` `LENGTH = 0xED000 - 0x27000` | **0x20006000** | NO |
| 3 | meshcore-dev/MeshCore | `boards/nrf52840_s140_v7.ld` | https://raw.githubusercontent.com/meshcore-dev/MeshCore/main/boards/nrf52840_s140_v7.ld | `0x27000` `LENGTH = 0xED000 - 0x27000` | **0x20006000** | NO |
| 4 | meshcore-dev/MeshCore | `boards/nrf52840_s140_v6.ld` | https://raw.githubusercontent.com/meshcore-dev/MeshCore/main/boards/nrf52840_s140_v6.ld | `0x26000` `LENGTH = 0xED000 - 0x26000` | **0x20006000** | NO |
| 5 | meshcore-dev/MeshCore | `boards/nrf52840_s140_v7_extrafs.ld` | https://raw.githubusercontent.com/meshcore-dev/MeshCore/main/boards/nrf52840_s140_v7_extrafs.ld | `0x27000` `LENGTH = 0xD4000 - 0x27000` | **0x20006000** | NO |
| 6 | adafruit/Adafruit_nRF52_Arduino | `cores/nRF5/linker/nrf52840_s140_v6.ld` | https://raw.githubusercontent.com/adafruit/Adafruit_nRF52_Arduino/master/cores/nRF5/linker/nrf52840_s140_v6.ld | `0x26000` `LENGTH = 0xED000 - 0x26000` | **0x20006000** | NO |
| 7 | adafruit/Adafruit_nRF52_Arduino | `cores/nRF5/linker/nrf52833_s140_v7.ld` (el unico "v7" del core) | https://raw.githubusercontent.com/adafruit/Adafruit_nRF52_Arduino/master/cores/nRF5/linker/nrf52833_s140_v7.ld | `0x27000` `LENGTH = 0x6D000 - 0x27000` | **0x20006000** | NO |
| 8 | **cfr34k/t-echo-lora-aprs** | **`t-echo.ld`** | https://raw.githubusercontent.com/cfr34k/t-echo-lora-aprs/master/t-echo.ld | `0x27000` `LENGTH = 0xd9000` | **0x20004260** `LENGTH = 0x3bda0` | **SI — UNICO** |

Copias locales verificadas (byte a byte, mismo contenido que las URLs de arriba):

- `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\cores\nRF5\linker\nrf52840_s140_v6.ld`
  → `FLASH 0x26000` / `RAM 0x20006000` (filas 8 y 17 del fichero).
  En ese paquete **no existe `nrf52840_s140_v7.ld`**: solo `nrf52832_s132_v6.ld`,
  `nrf52833_s140_v7.ld`, `nrf52840_s140_v6.ld`, `nrf52_common.ld`.
- `_referencias\t-echo-lora-aprs\t-echo.ld` (lineas 8-9):
  `FLASH (rx) : ORIGIN = 0x27000, LENGTH = 0xd9000` / `RAM (rwx) : ORIGIN = 0x20004260, LENGTH = 0x3bda0`
- `_trabajo_ea2oy\_pio_core\packages\...` y `_pio_core_fork\packages\...` y
  `C:\Users\Jesus\Desktop\firmware\.pio_core_meshtastic\packages\...`: copias identicas del core de Adafruit.

Ese `t-echo.ld` es un guion del **SDK nRF5** (secciones `.sdh_*_observers`, `.log_const_data`,
`.cli_command`, `INCLUDE "nrf_common.ld"`), no del core de Arduino.

### A.3 MeshCore: que usa cada placa (para contraste)

- `boards/t-echo.json` — https://raw.githubusercontent.com/meshcore-dev/MeshCore/main/boards/t-echo.json
  ```json
  "ldscript": "nrf52840_s140_v6.ld"
  "softdevice": { "sd_name": "s140", "sd_version": "6.1.1", "sd_fwid": "0x00B6" }
  "maximum_ram_size": 235520, "maximum_size": 815104
  ```
- `variants/lilygo_techo/platformio.ini` — https://raw.githubusercontent.com/meshcore-dev/MeshCore/main/variants/lilygo_techo/platformio.ini
  `board_build.ldscript = boards/nrf52840_s140_v6.ld`, `-I lib/nrf52/s140_nrf52_6.1.1_API/include`,
  y los entornos `*_companion_radio_ble/usb` cambian a `boards/nrf52840_s140_v6_extrafs.ld`
  (`board_upload.maximum_size = 712704`).
- Conclusion: MeshCore en T-Echo va con **S140 6.1.1 / flash 0x26000 / RAM 0x20006000**.

### A.4 Por que `0x20004260` (dato duro + inferencia marcada)

- Dato duro: el valor aparece **solo** en `cfr34k/t-echo-lora-aprs/t-echo.ld` (verificado en la copia
  local y en el raw de GitHub, HTTP 200).
- Regla oficial de Nordic sobre ese numero (cabeceras S140 7.3.0, locales):
  `sd_ble_enable(uint32_t * p_app_ram_base)` — *"Pointer to a variable containing the start address of
  the application RAM region (APP_RAM_BASE). **On return, this will contain the minimum start address
  of the application RAM region required by the SoftDevice for this configuration.**"* y
  *"At runtime the IC's RAM is split into 2 regions: The SoftDevice RAM region is located between
  0x20000000 and APP_RAM_BASE-1 and the application's RAM region is located between APP_RAM_BASE and
  the start of the call stack."*, con `@retval NRF_ERROR_NO_MEM` *"The amount of memory assigned to
  the SoftDevice by *p_app_ram_base is not large enough ... Check *p_app_ram_base and set the start
  address of the application RAM region accordingly."*
  Fichero: `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\cores\nRF5\nordic\softdevice\s140_nrf52_7.3.0_API\include\ble.h` (doc de `sd_ble_enable`).
- **Inferencia (NO CONFIRMADO)**: `0x20004260` es el `APP_RAM_BASE` que resultaba de la configuracion
  de SoftDevice de cfr34k (probablemente el valor que `sd_ble_enable()` devolvio en su placa).
  No he encontrado ninguna fuente Nordic que publique ese numero.
- Dato util para nuestra placa: la nota oficial de la misma cabecera (`ble.h`, linea 409 y 444):
  *"@note The memory requirement for a specific configuration **will not increase between SoftDevices
  with the same major version number**."*
  ⇒ ligar a `RAM ORIGIN = 0x20006000` (valor de Adafruit/Meshtastic/MeshCore para S140 v6 **y** v7)
  no puede quedarse corto al cambiar entre revisiones 7.x. `0x20006000 > 0x20004260` (8 KB de margen).

---

## B) POSICION DE NORDIC: app compilada con cabeceras NUEVAS sobre SoftDevice VIEJO (misma major 7.x)

### B.1 DevZone 70537 — respuesta **verificada** de un empleado de Nordic

URL: https://devzone.nordicsemi.com/f/nordic-q-a/70537/sdk-backwards-compatibility-support-for-older-sd/289652
Responde **ovrebekk (Torbjorn Ovrebekk, Nordic Semiconductor)**, +2, "Verified Answer":

> "Minor SoftDevice updates (S132 v5.0.0 to v5.1.0 for example) **will never change existing API's**,
> they will only modify the implementation of existing ones or add new ones. This means that you can
> **safely update to a new minor version without changing the SDK**.
> Major SoftDevice updates on the other hand (S132 v5.x.x to v6.x.x for example) **can both change and
> remove existing API functions**, in addition to adding new ones, and they will normally not work on
> other SDK versions than the ones they were designed for."

### B.2 DevZone 20724 — respuesta **verificada** de un empleado de Nordic

URL: https://devzone.nordicsemi.com/f/nordic-q-a/20724/can-i-update-the-softdevice-without-touching-the-application/80867
Pregunta original: *"I can update a SoftDevice without updating the app as long as both SDs are of the
same Major Version, e.g. s132 v3.0 to v3.1, presumably because Nordic preserves APIs ... Correct?"*
Responde **Hung Bui (Nordic Semiconductor)**, "Verified Answer":

> "When you update your softdevice, the application will be erase to give space for the new image of
> the softdevice. You will have to update the application back after you done with the softdevice.
> **Between minor versions of the softdevice, the application doesn't need to be modified to work with
> the new softdevice (with same major version).** We will try to keep the MBR unchanged alongside same
> softdevice in different major versions."

(Comunidad, `endnode`, en el mismo hilo, aviso practico: *"you should test all combinations of your
application code/binary versions and SD versions which you want to support before releasing it to the
field. You will then find out if any API or SD size changed (which would break compatibility with your
app)."*)

### B.3 DevZone 82849 — el caso **inverso** (SDK/cabeceras nuevas con SoftDevice viejo)

URL: https://devzone.nordicsemi.com/f/nordic-q-a/82849/is-it-possible-to-update-sdk-without-updating-softdevice-version/412835
Empleado de Nordic: **Einar Thorsrud (eith)**. Respuesta literal (lo importante en negrita):

> "Generally, SoftDevice with different major version numbers have API changes. And the SDK are only
> adapted to use the SoftDevices they ship with. That means that you will need to make some adjustments
> in the SDK code, and you will need to properly test this."
> "please ensure that the basics is in place: **You are using the header files for the SoftDevice you
> are actually using.** Also, as there are API changes you will need to handle those. To see if you
> forgot some I recommend you read the SoftDevice migration document ... (in this case you would red it
> the other way, **as you are migrating SDK 17.1.0 to an older SoftDevice**, so start with the migration
> document for S140 7.2.0 and see all changes from 6.1.1, and ensure that you handle those. There are
> not many changes, though.)"

⇒ Es exactamente nuestro caso (codigo enlazado con 7.3.0 sobre un SoftDevice 7.2.0) y Nordic **no lo
bendice**: dice que se usen **las cabeceras del SoftDevice que realmente corre** y revisar el
*migration document*.

### B.4 DevZone 63771 — SoftDevice nuevo con bootloader/entorno viejo (y al reves)

URL: https://devzone.nordicsemi.com/f/nordic-q-a/63771/using-dfu-bootloader-with-an-application-on-a-newer-sdk-version
**Hung Bui (Nordic)**:

> "It's not suggested to use Softdevice v7.0.1 with the bootloader made for softdevice v6.1.1 ...
> Softdevice v6.1.1 and Softdevice v7.0.1 are quite similar **and it may work when you replace them
> but there is no guarantee that there would be no problem.**"

En el mismo hilo, el usuario `matkusch` (comunidad) cuenta que le funciono cambiando **solo** la ruta
al `.ld` nuevo y **el SD ID de los ZIP de DFU**: *"i didn't modify the ble_*.h at all, i just changed
the loader file path to the newer SD version, and the SD ID for the DFU zip files, and everything was
working fine after that."*

### B.5 SD_FWID / FWID: quien lo comprueba

- **El init packet de DFU no lleva sd-id**; el chequeo se hace con el campo `sd_req` del init packet de
  la **aplicacion**. **bjorn-spockeli (Nordic Semiconductor)**, DevZone 61117:
  https://devzone.nordicsemi.com/f/nordic-q-a/61117/how-can-i-determine-the-softdevice-id-from-data-in-the-dfu-package
  > "the init packet does not have any sd-id field"
  > "The value passed as --sd-id is he new SoftDevice ID to be used as --sd-req for the Application update."
  > "Yes, the ID of the SoftDevice is stored at a fixed location, **0x300C**"
- **`--sd-req` = el FWID del SoftDevice que hay AHORA en el aparato.** **Vidar Berg (Nordic
  Semiconductor)**, DevZone 95105:
  https://devzone.nordicsemi.com/f/nordic-q-a/95105/how-do-i-calculate-the-softdevice-requriment-parameter---sd-req-parameter-when-genenrating-dfu-mesh/402248
  > "--sd-req must specify the FWID of the Softdevice currently present on the device."
  > "You can run 'nrfutil pkg generate --help' to view the FWIDs for our Softdevice releases."
  > "Note that you can find the FWID from the Softdevice release notes too."
- **NO CONFIRMADO**: no he encontrado ninguna afirmacion de Nordic de que **el SoftDevice** (en
  arranque) compruebe el FWID de la aplicacion. Todo lo documentado apunta a que la comprobacion vive
  en el **bootloader/DFU** (campo `sd_req` del init packet generado con `nrfutil`/`adafruit-nrfutil`).
  En el DFU de Adafruit se ve el mecanismo: `platform.txt` del core →
  `nrfutil dfu genpkg --dev-type 0x0052 --sd-req {build.sd_fwid} --application ...`
  (`C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\platform.txt`, linea 109).

### B.6 Que pasa si la app llama a una API que NO existe en el SoftDevice grabado

- Valor exacto del error (cabeceras S140 7.3.0 locales, `nrf_error.h` lineas 56 y 63):
  ```
  #define NRF_ERROR_BASE_NUM      (0x0)       ///< Global error base
  #define NRF_ERROR_SVC_HANDLER_MISSING  (NRF_ERROR_BASE_NUM + 1)  ///< SVC handler is missing
  ```
  ⇒ **NRF_ERROR_SVC_HANDLER_MISSING = 0x0001 (1)**, descrito como "SVC handler is missing".
  El core de Adafruit lo traduce en `cores/nRF5/utility/debug.cpp:360`.
- Indicador real de que eso ocurre en produccion: DevZone 115330 — el usuario ve
  *"app_error_fault_handler() gets called with id = 1 (NRF_ERROR_SVC_HANDLER_MISSING)"* y SVC #255
  antes de un HardFault en `0xA60`. **Sigmund (Nordic)** responde que `0x00000A60` es el *hardfault
  exception handler dentro del MBR*, que normalmente se reenvia a la aplicacion.
  https://devzone.nordicsemi.com/f/nordic-q-a/115330/nrf5-sdk-svc-255-0xff
- **NO CONFIRMADO**: no he encontrado un documento Nordic que diga literalmente "llamar desde una app
  a una API anadida en una revision posterior del SoftDevice devuelve NRF_ERROR_SVC_HANDLER_MISSING".
  Lo que si esta documentado es el significado del codigo (SVC handler missing) y que el HardFault
  puede acabar en el MBR.
- Nota adicional: en las cabeceras del SoftDevice S140 7.3.0 **no existe macro `*_API_VERSION`**
  (grep sin resultados en `s140_nrf52_7.3.0_API\include\*.h`); lo unico de version es
  `nrf_sdm.h`: `SD_MAJOR_VERSION (7)`, `SD_MINOR_VERSION (3)`, `SD_BUGFIX_VERSION`,
  `SD_VERSION (SD_MAJOR_VERSION * 1000000 + SD_MINOR_VERSION * 1000 + SD_BUGFIX_VERSION)`.
  Es decir: **no hay comprobacion en tiempo de compilacion del API version**; el unico control real es
  el FWID en el DFU.

---

## C) FWID (SD_FWID / `sd-req`) POR VERSION — QUE ESTA PROBADO

### C.0 Metodo (reproducible, sin depender de terceros)

La ficha del SoftDevice esta en `0x3000` = `MBR_SIZE (0x1000) + SOFTDEVICE_INFO_STRUCT_OFFSET (0x2000)`.
Offsets, de `nrf_sdm.h` (cabeceras S140 7.3.0 locales):

```
SD_INFO_STRUCT_SIZE_OFFSET = 0x2000        (uint8)   -> absoluto 0x3000
SD_SIZE_OFFSET             = 0x2000 + 0x08 (uint32)  -> absoluto 0x3008
SD_FWID_OFFSET             = 0x2000 + 0x0C (uint16)  -> absoluto 0x300C   <-- el FWID
SD_ID_OFFSET               = 0x2000 + 0x10 (uint32)  -> absoluto 0x3010   (S140 = 0x0000008C)
SD_VERSION_OFFSET          = 0x2000 + 0x14 (uint32)  -> absoluto 0x3014
```
He parseado los `.hex`/`.bin` con un script propio (Intel HEX) y he leido los bytes.

### C.1 Tabla de resultados

| SoftDevice | FWID | Como esta probado (fuente primaria) |
|---|---|---|
| S140 **6.1.1** | **0x00B6** | Bytes en `0x300C` del hex oficial: `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\bootloader\pca10056\pca10056_bootloader-0.9.1_s140_6.1.1.hex` (`size=0x26000, id=0x8C, fwid=0x00B6`). Ademas `boards.txt` del core: `feather52840.menu.softdevice.s140v6.build.sd_fwid=0x00B6`. Y MeshCore `boards/t-echo.json`. |
| S140 **7.0.1** | **NO CONFIRMADO** | Existe (pagina oficial de descargas), pero no he encontrado ni un hex ni un documento con su FWID. (Una tabla antigua de blog, no oficial, lista S140 7.0.0 = 0xC1 y S132/S112/S113 7.0.1 = 0xCB/0xCD/0xCC; de ahi se *podria* inferir 0xCA para S140 7.0.1, pero **eso es especulacion: NO CONFIRMADO**.) |
| S140 **7.1.0** | **NO EXISTE** | La pagina oficial de descargas de Nordic lista solo: 7.3.0, 7.2.0, 7.0.1, 6.1.1, 6.1.0, 6.0.0 (+ alphas). https://www.nordicsemi.com/Products/Development-software/s140/download |
| S140 **7.2.0** | **0x0100** | Bytes en `0x300C` del hex oficial `s140_nrf52_7.2.0_softdevice.hex`: `2C FF FF FF DB E5 B1 51 | 00 70 02 00 | 00 01 | FF FF | 8C 00 00 00 | 90 D7 6A 00 ...` → `size=0x27000, fwid=0x0100, id=0x8C, ver=0x006AD790`. Hex disponible en https://raw.githubusercontent.com/adafruit/Adafruit_nRF52_Bootloader/master/lib/softdevice/s140_nrf52_7.2.0/s140_nrf52_7.2.0_softdevice.hex (y anadido en el commit https://github.com/adafruit/Adafruit_nRF52_Bootloader/commit/8d4e89fdde916f012022bad376467cacf4fdf7a0). **Confirmado ademas por Nordic**: Vidar Berg, https://devzone.nordicsemi.com/f/nordic-q-a/69286/what-is-s140_nrf52_7-2-0-softdevice-id/284038 → *"For s140 v7.2.0, the FWID is 0x0100."* |
| S140 **7.3.0** | **0x0123** | Bytes en `0x300C` del hex oficial `s140_nrf52_7.3.0_softdevice.hex` (copia local `...\guillermo\firmware-develop\bin\` y `C:\Users\Jesus\.platformio\...\bootloader\pca10100\pca10100_bootloader-0.9.1_s140_7.3.0.hex`): `size=0x27000, fwid=0x0123, id=0x8C`. Ademas: `boards.txt` → `pca10100.menu.softdevice.s140v7.build.sd_fwid=0x0123` (con `sd_version=7.3.0`); README del bootloader de Adafruit → `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x0123 ...` (https://raw.githubusercontent.com/adafruit/Adafruit_nRF52_Bootloader/master/README.md); assets de Meshtastic `Meshtastic_7.3.0_bootloader-0.9.2_s140_7.3.0.hex`. |

### C.2 Consecuencia directa para nuestra placa (T-Echo Plus / "el nodo del operador")

- La ficha leida en la flash del nodo (`fwid = 0x0100`, `size = 0x27000`, `id = 0x8C`) es **S140 7.2.0**
  — coincide exactamente con lo que dicen los bytes del hex oficial 7.2.0 y con el `INFO_UF2.TXT`
  ("SoftDevice: S140 version 7.2.0").
- El core de Adafruit con el que compilamos (`framework-arduinoadafruitnrf52`) trae cabeceras
  **7.3.0** y declara `sd_fwid=0x0123` para `s140v7` ⇒ **el FWID que espera el build NO es el de la
  placa**. Eso no impide arrancar (el FWID lo comprueba el DFU, no el SoftDevice), pero si explica que
  `0x0101` (el valor que llego a usarse en `platformio.ini`) no sea el FWID de ninguna S140:
  los reales de la rama 7.x son **0x0100 (7.2.0)** y **0x0123 (7.3.0)** (7.0.1 sin confirmar).
- El paquete `lilygo_techo_bootloader-0.6.1.zip` que trae Meshtastic
  (`C:\Users\Jesus\Desktop\Escritorio\guillermo\firmware-develop\bin\`) contiene `sd_bl.bin` con la
  ficha en el offset `0x2000` (el bin empieza en la base del SoftDevice, 0x1000):
  `size=0x26000, fwid=0x00B6, id=0x8C` ⇒ **S140 6.1.1**, y su `manifest.json`
  (`"softdevice_req": [65534]` = 0xFFFE) no exige ningun FWID concreto. Es decir: ese paquete
  **bajaria** el SoftDevice del nodo de 7.2.0 a 6.1.1 (y entonces si habria que enlazar a 0x26000).

---

## ANEXO — Herramientas y ficheros usados (para repetir la verificacion)

- Parser de Intel HEX + lectura de la ficha: `C:\Users\Jesus\AppData\Local\Temp\sdinvest\lee_sd.py`
  (uso: `python lee_sd.py <fichero.hex> [otro.hex|paquete.zip]`; imprime los 48 bytes de 0x3000 con
  `size`, `sd_id`, `version` y **`FWID`**).
- Hex de 7.2.0 extraido del patch de Adafruit: `C:\Users\Jesus\AppData\Local\Temp\sdinvest\s140_7.2.0.hex`.
- Descargas/greps de DevZone (via `r.jina.ai`): `C:\Users\Jesus\AppData\Local\Temp\sdinvest\dz*.txt`.
- Copias locales de referencia:
  `_referencias\t-echo-lora-aprs\t-echo.ld`,
  `C:\Users\Jesus\.platformio\packages\framework-arduinoadafruitnrf52\` (linker, boards.txt, softdevice 6.1.1 y 7.3.0, bootloaders).
