# SESION 2026-09-17 (noche) — EL BLUETOOTH: LO MEDIDO, LO DESCARTADO Y LO QUE SIGUE ABIERTO

> Escrito para que **cualquiera lo pueda auditar**: cada afirmacion dice de donde sale.
> Marcas: **[MEDIDO]** con la placa o leyendo un fichero, **[DOC]** documentacion oficial,
> **[DEDUCIDO]** conclusion a partir de medidas, **[SUPUESTO]** sin comprobar, **[ABIERTO]** sin resolver.
> **Nada de esta sesion se ha grabado en el nodo salvo lo que se dice expresamente.**

## 0. En una frase

El nodo **tiene un SoftDevice S140 valido**; lo que estaba mal era **nuestro codigo** (leia la
ficha 0x1000 bytes mas abajo y esperaba un `fwid` que no existe), y **al arrancar el Bluetooth
el nodo se cuelga y entra en bucle de reinicios** — eso sigue **sin resolver**.

## 1. LO MEDIDO (y con que)

### 1.1 La direccion de la ficha del SoftDevice **[DOC] + [MEDIDO]**

La ficha (`SoftDevice Information Structure`) esta en **`0x3000`** y el `fwid` en **`0x300C`**:
`MBR_SIZE (0x1000) + SOFTDEVICE_INFO_STRUCT_OFFSET (0x2000) + 0x0C`.

★ **De donde viene la confusion historica [DOC]**: `.../s140_nrf52_7.3.0_API/include/nrf_sdm.h`
linea 65 define **`MBR_SIZE 0`**; el valor bueno (`0x1000`) esta en
`.../include/nrf52/nrf_mbr.h` linea 68. Quien incluya solo `nrf_sdm.h` calcula `0x2000` y se
equivoca por `0x1000`. Eso nos paso: la sonda leia **`0x200C`**, donde hay **codigo del
SoftDevice**, no una ficha.

Los dos numeros que este proyecto tomo por chips averiados son **los mismos bytes de codigo
leidos en la direccion vieja [MEDIDO]**, comprobado en los hex oficiales:

| offset | S140 7.3.0 (hex oficial) | S140 6.1.1 (hex oficial) |
|---|---|---|
| `0x200C` (la vieja) | `fwid=0xD902` | `fwid=0xE002` |
| `0x300C` (la buena) | `fwid=0x0123` | `fwid=0x00B6` |

Ficheros: `_trabajo_ea2oy\tools\lee_hex.py` (lector de Intel HEX) y
`_trabajo_ea2oy\tools\uf2.py` (lector/escritor de UF2). Herramienta nueva de solo lectura del
subagente de investigacion: `_trabajo_ea2oy\tools\mapa_memoria.py`.

### 1.2 Que lleva grabado hoy la placa del operador **[MEDIDO]**

Leido de su firmware de fabrica (`CURRENT.UF2`, 1.908.736 B, SHA-256
`1A8A65E65EF040F1E064DC7FD166783A80A1D7034ED7B1898F75D928B7A21AAC`, sacado del cargador
`TECHOBOOT`):

```
ficha: tamano 44 B | SD_size = 0x27000 | fwid = 0x0100 | id = 0x0000008C (S140) | version 0x006AD790
```

Y la **placa viva contesta lo mismo**: el comando `ble` devolvio `fwid 0x0100` en todos los
arranques y builds. **El `id 0x8C` y un tamano coherente son la huella de un SoftDevice de
verdad**: la placa **no** tiene el chip averiado.

- **`0x0100` no es el fwid de la 7.3.0** (esa es `0x0123`, medido en el hex oficial de Nordic):
  es una **S140 v7 de otra revision**. **[DEDUCIDO]**
- El `INFO_UF2.TXT` del cargador dice «SoftDevice: S140 version 7.2.0», pero **es texto fijo del
  cargador, no una lectura del chip**. **[MEDIDO]** (el fichero es de 2021 y no cambia).

### 1.3 Por que el nodo entra en «bucle» y no dice nada **[MEDIDO]**

El core de Adafruit **reinicia en cualquier fallo duro**
(`framework-arduinoadafruitnrf52\cores\nRF5\utility\debug.cpp`, `HardFault_Handler`:
`NVIC_SystemReset()`). Y el manejador de errores del SoftDevice del core **no imprime nada** si
no esta definido `CFG_DEBUG` (no lo esta: `build.debug_flags=-DCFG_DEBUG=0`).
=> Un fallo duro = **reinicio inmediato = montar/desmontar el USB, sin consola**. El sintoma
observado **no necesita ninguna suposicion**: es un fallo duro reiniciando el nodo. **[DEDUCIDO]**

### 1.4 El dato que permite quitarse la duda de la RAM **[DOC]**

`.../s140_nrf52_7.3.0_API/include/ble.h` (aprox. lineas 387-433): el parametro
`p_app_ram_base` de **`sd_ble_enable()` es de entrada Y de salida** — al volver trae **la
direccion minima donde el SoftDevice quiere que empiece la RAM de la aplicacion** para esa
configuracion. Si la RAM dada es poca, devuelve `NRF_ERROR_NO_MEM` (4) y **deja el puntero
actualizado**. => **Se puede averiguar el numero exacto en UNA sola grabacion.** **[DEDUCIDO]**

### 1.5 La RAM: los tres numeros que hay sobre la mesa **[MEDIDO]**

| quien | origen de RAM de la aplicacion | nota |
|---|---|---|
| este proyecto, desde el b33 | `0x20004260` | es el borde: **cero margen** |
| Meshtastic (T-Echo, S140 v7) | `0x20004000` | su guion dice: «deja ~2+ KB de margen sobre la base que reporta `sd_ble_enable()`» |
| el firmware del aleman (S140 7.2.0) | `0x20004260` | funciona con Bluetooth en este hardware |

Y el proyecto enlazaba en `0x20006000` hasta el b33 (copiado del guion de la **S140 v6**): eso
se corrigio y **no era la causa del cuelgue** (el b33 se grabo y se colgo igual).

## 2. LO QUE SE PROBO EN LA PLACA Y QUE PASO

| build | que llevaba | resultado |
|---|---|---|
| **b25** | Bluetooth activado, sin proteccion | **bucle** al encender el Bluetooth |
| b26/b27 (sonda) | sonda que lee `0x200C` esperando `0x0101` | USB intacto, Bluetooth no arranca, **y el fwid que decia no era real** |
| b29/b30 | sonda leyendo `0x300C`, esperando `0x0123` | USB intacto; dice `fwid 0x0100` (el real) |
| b31 | sonda «basta con que HAYA SoftDevice» | USB intacto |
| **b32** | sonda relajada + arranque intentado | **bucle** |
| **b33** | `fwid 0x0100` exacto + RAM en `0x20004260` | **estable con el Bluetooth APAGADO (36 s medidos)**; **bucle al encenderlo** |

Recuperacion en todos los casos: **doble toque al reset + grabar por el cargador UF2** (el b13
de `_publicar_web\uf2\` es el que se uso). Desde el PC **no se puede** sacar de un bucle: no hay
`TECHOBOOT` montado y el nodo no responde. **[MEDIDO]**

## 3. LO QUE **NO** ES LA CAUSA (descartado con medida)

1. **El chip no esta averiado.** Tiene un S140 con `id 0x8C` y `SD_size 0x27000`. **[MEDIDO]**
2. **La direccion de la sonda** (era `0x200C`): corregida a `0x300C` y verificado en el binario
   (9 sitios cargan `0x300C`). **[MEDIDO]**
3. **El `fwid` esperado** (`0x0101`): no es el de ninguna S140. Ahora los entornos `s140v7`
   declaran **`0x0100`**, el de la placa. **[MEDIDO]**
4. **La RAM en `0x20006000`**: corregida a `0x20004260` en el b33 **y el nodo se colgo igual**.
   => la RAM no era la causa (o no era la unica). **[MEDIDO]**
5. **No hay watchdog** en este firmware: el bucle no era el vigilante. **[MEDIDO]**
6. **La temperatura por `NRF_TEMP`** (periferico que el SoftDevice reserva): corregido con
   `sd_temp_get()` en `sensors.cpp`. **[MEDIDO]**

## 4. LO QUE SIGUE ABIERTO

1. **Por que se cuelga al arrancar el SoftDevice.** Es **el** problema. **[ABIERTO]**
2. **El numero exacto de RAM** que pide este SoftDevice (se saca con la tecnica de §1.4). **[ABIERTO]**
3. ★ **Discrepancia de 4 bytes entre las fotos y la placa viva**: en las fotos
   (`data\rescue\*.uf2`) leer en `0x300C` da el `id` (`0x008C`), mientras **la placa viva
   contesta `0x0100`**. Hoy el ancla es **la placa** (la medida en vivo), y las fotos se usan
   solo como historia. **Sin resolver.** **[ABIERTO]**
4. **Una de las fotos** (`techo_EA2KR_original_..._20260914-1118.uf2`) tiene su **tabla de
   vectores en `0x26000`** mientras su SoftDevice declara `SD_size 0x27000`. O el fichero es una
   mezcla, o la foto no es de un arranque limpio. **Sin resolver.** **[ABIERTO]**
5. **Si el SoftDevice de la placa es compatible** con una aplicacion enlazada contra las
   cabeceras 7.3.0, o hay que grabarle otro (se tiene el de fabrica del nodo y el hex oficial
   de Nordic **`s140_nrf52_7.3.0_softdevice.hex`**). **[ABIERTO]**

## 5. LO QUE HAY QUE HACER (y en que orden)

1. **Esperar el informe del subagente de investigacion** (`ead1d4fe`), que esta escribiendo:
   - un **banco de diagnostico aislado** (`_trabajo_ea2oy\diag_ble\`, proyecto aparte que **no**
     entra en el firmware del nodo) con zona de RAM `.noinit` que **sobrevive al reinicio por
     fallo duro**, para que el nodo **cuente por que falla en vez de colgarse**;
   - el **«hola mundo» del SoftDevice**: ejemplo minimo que solo arranca el SoftDevice, dice
     hasta donde llega y **no toca el USB** (la leccion del b25/b32);
   - **5+ hipotesis** con como se comprueba cada una y su riesgo.
2. **Auditar ese informe** (el operador lo ha pedido expresamente) antes de grabar nada.
3. **Grabar UNA vez el banco de diagnostico** y leer el resultado por el cable.
4. Solo despues, decidir el arreglo (margen de RAM, SoftDevice distinto, u otra cosa).

**Regla que no se toca**: la sonda del SoftDevice **se queda y sigue siendo estricta**. Un nodo
sin Bluetooth es aceptable; un nodo en bucle, no.

## 5-bis. ★ LA INVESTIGACION, CERRADA (2026-09-17, madrugada) — LO ENTREGADO

Los dos subagentes han terminado. **Nada de esto se ha grabado.**

### Lo que explica el «bucle», ya sin suposiciones **[MEDIDO]**

1. **El cuelgue no es un cuelgue: es un REINICIO.** `cores\nRF5\utility\debug.cpp:60-64`:
   `HardFault_Handler() { NVIC_SystemReset(); }`, y **no es `weak`** (lo dice
   `gcc_startup_nrf52840.S:310`). Cualquier fallo duro = reinicio = USB montando/desmontando.
2. **Al fallar, nadie cuenta nada.** El manejador de errores que el core le pasa al SoftDevice
   solo imprime `#if CFG_DEBUG` (`bluefruit.cpp:118-140`) y `platform.txt:58` trae
   `-DCFG_DEBUG=0`: **la funcion esta VACIA**. El SoftDevice llama, la funcion retorna, y el
   SoftDevice reinicia. De ahi el silencio.
3. **El orden dentro de `Bluefruit.begin()` lo explica todo**: el USB se **devuelve** en la
   linea **323** (`usb_softdevice_post_enable()`) y `sd_ble_enable` es la **438**. Si falla
   `sd_softdevice_enable()` (linea 319) **el USB no vuelve nunca** (el bucle); si falla mas
   adelante, el USB ya estaba devuelto.

### El dato oficial que cierra una duda **[DOC]**

Las notas de la **S140 7.2.0** dicen: RAM minima `0x1678`, Flash `0x27000` y
**«The Firmware ID of this SoftDevice is 0x0100»**. O sea: **la placa lleva la 7.2.0** y el
`fwid 0x0100` que lee la sonda **es el correcto**. Y con `0x20004260` le damos ~9,6 KB mas que
el minimo -> **la RAM es hipotesis SECUNDARIA (~8 %)**.

### Hipotesis, con su probabilidad estimada (estimaciones, no medidas)

| | hipotesis | est. | comprobacion |
|---|---|---|---|
| H1 | prioridad de IRQ ilegal (`sd_softdevice_enable` = `0x1001`) | ~35 % | el banco dice el codigo y **cual IRQ es** |
| H3 | cabeceras 7.3.0 sobre SD 7.2.0 | ~20 % | el banco captura `id`/`pc`/`info` |
| H4 | el fallo es DESPUES de arrancar la SD | ~18 % | el banco para tras `sd_ble_enable` |
| H5 | cristal de 32 kHz (LFCLK) | ~12 % | entorno `_rc` (reloj RC) |
| H2 | la RAM (`0x20004260`) | ~8 % | el banco mide el `app_ram_base` exacto |
| H6 | SD danado | ~3 % | `sd_ble_version_get()` |

Los niveles de prioridad que el SoftDevice se reserva son **0, 1 y 4** (`nrf_nvic.h:80-83`), y
`0x1001` es `NRF_ERROR_SDM_INCORRECT_INTERRUPT_CONFIGURATION` (**no** `INVALID_STATE`, que es
8). ⚠ **El proyecto lo tiene mal escrito** en `ble_sdk.h:40-41` y en `platformio.ini`
(dicen `INVALID_STATE` para el 4097): **corregir**.

### ★ LO PRIMERO QUE HAY QUE HACER, Y ES GRATIS (sin grabar nada)

El firmware del nodo **ya escribe marcas** del arranque del Bluetooth en el registro de viaje
(`ble_kiss.cpp:548` «BLE inicio 1/6», `:564` «2/6»). Entonces:
- si el registro se queda en **`1/6`** -> el fallo esta **dentro de `Bluefruit.begin()`**
  (H1/H2/H3/H5);
- si llega al **`2/6`** -> **la SD arranco** y el culpable es lo de despues (H4).

Se lee con `log dump` por el cable, con el b34 ya grabado. **Descarta H4 de un vistazo.**

### El banco de diagnostico (`diag_ble\`) — escrito, compilado, **NUNCA EJECUTADO**

Proyecto aparte (PlatformIO solo compila lo de su carpeta -> no puede entrar en el firmware del
nodo; ademas **no gasta numero de compilacion**). Mide: los dos juegos de valores de la ficha,
el `fwid` que contesta la SD (`sd_ble_version_get()`), el error de `sd_softdevice_enable`, el
error de **cada uno** de los 7 `sd_ble_cfg_set`, **el `app_ram_base` exacto** (base baja ->
lee lo que contesta -> vuelve a llamar con ese valor), el estado del NVIC
(`NVIC->ISER` + las 32 prioridades) **y cual es la IRQ culpable**, y el fallo duro
(`CFSR/HFSR/MMFAR/BFAR/PC/LR/R0-R3`). Guarda todo en `.noinit` (`0x200045C8`, verificado por
debajo de `__bss_start__`) mas copia en el fichero interno (verificado sin solape con el
registro del operador: el FS interno vive en `0xED000..0xF3FFF` y el nodo usa `0xC8000..0xE9000`).

Propiedades de seguridad **comprobadas en el binario**: `AdafruitBluefruit::begin()` **NO esta
enlazado** (`arm-none-eabi-nm` -> 0 resultados) -> **no puede arrancar el Bluetooth como el
firmware del nodo**; usa `usb_softdevice_pre_enable/post_enable` (para **volver a montar** el
USB); apaga la SD siempre; respeta la sonda estricta; y si hay fallo duro **deja el rastro y
reinicia**, para que el acta se pueda leer. Si un paso se queda mudo **sin** reiniciar, el LED
**late antes de cada paso** (3/4/5) para ver donde se paro.

`firmware.uf2` = **217.600 B**, llega a `0x41880` -> **673,9 KB libres** antes del cargador.
Instrucciones: `diag_ble\COMO_SE_GRABA_Y_SE_LEE.md`.

### Lo que queda SIN RESOLVER (escrito, no cerrado)

1. Las dos discrepancias pequeñas de la ficha del SoftDevice (el byte de tamano de ficha no
   esta en las imagenes del nodo; cabeceras y notas dicen offsets distintos). No afectan al
   `fwid`, que sale bien en las tres fuentes.
2. La tabla de vectores en `0x26000` de una foto vieja.
3. **El suelo de RAM real** con la configuracion concreta del nodo: hay que medirlo.
4. **Que prioridades de IRQ dejan RadioLib y el driver de la tinta**: no auditado. Es el
   siguiente trabajo **si sale H1**.
5. **El banco no se ha ejecutado NUNCA**: compila, enlaza y los simbolos estan donde deben; no
   esta probado en hardware.

### Correcciones a datos que circularon (para que no vuelvan)

- **Meshtastic T-Echo es un build de SD v6** (`boards\t-echo.json`: `s140_v6.ld`, `fwid 0x00B6`,
  `.text 0x26000`, `.data 0x20006000`). **No es una comparacion valida** con nuestra placa v7, y
  **no se puede grabar encima** (empezaria en `0x26000`, dentro de la zona de la SD v7).
- **`0x20004000` no existe en el arbol de Meshtastic para el T-Echo**: es del guion de la **v7**,
  que **ningun** entorno del T-Echo usa.
- El SoftDevice ocupa **`0x1000`-`0x27000`** (el campo `0x27000` es el **limite**, no un
  tamano), y **por eso** la aplicacion empieza **en** `0x27000`.
- El firmware de Meshtastic **no arranca la SD a mano**: llama al **mismo `Bluefruit.begin()`**
  del mismo core de Adafruit (los dos `bluefruit.cpp` solo difieren en 4 lineas de
  `#ifdef LED_BLUE`). **No hay nada que copiar de Meshtastic.**
- La RAM va **al reves** de como sugiere el comentario del guion: el SoftDevice ocupa
  `0x20000000..APP_RAM_BASE-1` (`ble.h:412-414`), asi que bajar de `0x20006000` a `0x20004260`
  **le recorto 7.488 B a la SD**, no se los dio a la app (aunque le siguen sobrando ~9,6 KB).

### Informes completos (auditables, cada afirmacion con su comando)

- `docs\INFORME_BLUETOOTH_ARRANQUE.md` (investigacion principal, 10 secciones).
- `docs\INFORME_MESHTASTIC_VS_NUESTRO_ARRANQUE_SD.md` (comparacion con Meshtastic).
- `docs\INFORME_BLUETOOTH_SOFTDEVICE.md` (analisis previo; **ojo: contiene el dato falso del
  `0x20004000`** y su §3.1/§3.2 estan corregidos por los otros dos).



## 6. ESTADO DEL NODO AL CERRAR ESTA SESION

- **En bucle de reinicios**, por decision del operador: **no se recupera con el b13**; se espera
  al trabajo del subagente para probarlo y auditarlo. Sale con **doble toque al reset**.
- La **configuracion del nodo esta intacta** y con **`bleEnabled = false`** (se apago por
  consola antes de grabar el b33), asi que **el b33 arranca estable si no se enciende el
  Bluetooth**: medido, 36 s con USB y consola funcionando. **[MEDIDO]**

## 7. FICHEROS TOCADOS EN ESTA SESION

- `_trabajo_ea2oy\src\ble_kiss.cpp` / `.h`: direccion de la sonda (`0x300C`), comprobacion de
  huella, `bleResumen()` con la ficha entera, `status.ble` ampliado.
- `_trabajo_ea2oy\platformio.ini` y `boards\techo-nrf52840-s140v7.json`: `fwid` esperado
  (`0x0100`), avisos de la direccion buena.
- `_trabajo_ea2oy\variants\techo\nrf52840_s140_v7.ld`: **RAM de `0x20006000` a `0x20004260`**.
- `_trabajo_ea2oy\tools\lee_hex.py`, `tools\uf2.py`, `tools\prueba_sonda_softdevice.py`
  (prueba de escritorio de la sonda, **0 fallos**), `tools\arregla_fwid_s140v7.py`.
- `_trabajo_ea2oy\docs\ENCARGO_INVESTIGACION_BLUETOOTH.md`: el encargo del subagente.
- Este fichero y el aviso de la seccion 4 de `Cerebro_Faketec_APRS_Igate_EA2OY\ESTADO.md`.
