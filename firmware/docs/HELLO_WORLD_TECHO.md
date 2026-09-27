# "Hello world" en la pantalla del LilyGO T-Echo Plus — lo que se hizo, lo que salió y lo que se aprendió

> **Documento de trabajo.** Fecha: 2026-09-16. Autor de la sesión: agente independiente.
> Todo lo que dice aquí está medido en la placa o leído en el código; cuando algo es una
> deducción, lo dice. **Lo que manda es la placa.**

## 0. Resultado, en tres líneas

1. **SÍ: el "hello world" se ve en la pantalla.** Lo ha confirmado el operador: marco,
   cuadradito negro arriba a la izquierda, `hello` y `world` en letras grandes y
   `T-Echo Plus` debajo. La orientación es la correcta.
2. **La pantalla se controla por un periférico SPI propio: SPIM2.** No hay que compartir
   el SPIM3 que usa la radio ni pelearse con el `SPI` del core.
3. **El pin BUSY de esta unidad no se mueve nunca** (0 de 39.565 muestras durante un
   refresco completo, medido de dos formas distintas). Y la pantalla se refresca igual.
   Toda la diagnosis anterior del proyecto partía de la premisa contraria, y esa premisa
   es falsa en esta placa.

## 1. Qué pantalla lleva y cómo se averiguó (sin que nadie lo dijera)

El T-Echo / T-Echo Plus lleva **tinta electrónica**, no una OLED:

| Dato | Valor |
|---|---|
| Panel | **GDEH0154D67** (Good Display), 1,54", **200 × 200** |
| Controlador | **SSD1681** |
| Bus | SPI, **8 MHz** como máximo en la práctica (el firmware de referencia lo usa a 8 MHz) |
| Refresco completo | ~2 s |

Fuentes que se contrastaron **entre sí** (y coinciden):
`firm_ref_techo/t-echo-lora-aprs-main/config/pinout.h` (el firmware que traía la placa de
fábrica), la tabla oficial de LilyGO y las variantes de Meshtastic
(`C:\Users\Jesus\Desktop\firmware\variants\nrf52840\t-echo-plus\variant.h`).

**Pines** (los tres coinciden; los números de Arduino son directos: N = P0.N, 32+N = P1.N):

| Señal | Pin |
|---|---|
| SCK | P0.31 |
| MOSI (SDI) | P0.29 |
| CS | P0.30 |
| DC | P0.28 |
| RST | P0.02 |
| BUSY | P0.03 |
| MISO | P1.06 (la pantalla no lo usa para escribir) |
| Interruptor del panel / luz | P1.11 |
| MOSFET de periferia | P0.12 |
| Regulador 3,3 V | P0.13 |

### 1.1 El orden de los bits del framebuffer: la duda resuelta

Había dos candidatos incompatibles en apariencia:

- **cfr34k** (el firmware de fábrica, que funciona) usa `bitidx = (W-1-x)*H + y`.
- **GxEPD2** (Meshtastic, que también funciona en esta placa) escribe por filas
  (`byte = x/8 + y*(W/8)`, MSB = píxel de más a la izquierda).

**No son incompatibles: son lo mismo.** Meshtastic usa GxEPD2 con `setRotation(3)`, y la
rotación 3 de GxEPD2 (`GxEPD2_BW.h`, `drawPixel`) mapea `x_ram = y`, `y_ram = H-1-x` y el
bit `7 - x_ram%8`. Sustituyendo, el índice de byte sale
`y/8 + (H-1-x)*(W/8)` y el bit `7 - y%8`: **exactamente** la fórmula de cfr34k.
Comprobado índice a índice. Por eso el dibujo del proyecto (que usa la fórmula de cfr34k)
ya era correcto: **el fallo nunca estuvo en el dibujo, estuvo en el bus.**

## 2. El camino que se siguió (y por qué)

El firmware del proyecto hace de iGate (radio, GPS, sensores, registro) y en él la pantalla
no dibujaba nada. En vez de tocar ese firmware (que funciona y que el operador usa), se hizo
un **banco de pruebas aislado**:

- Fichero nuevo `src/hello_techo.cpp` y entorno nuevo **`techo_plus_hello`** en
  `platformio.ini`, con `build_src_filter = +<hello_techo.cpp>` y la lista de librerías
  vacía: **ni radio, ni GPS, ni sensores, ni escritura en flash**. Si algo falla aquí, el
  sospechoso es uno: la pantalla.
- **Dos transportes**, elegibles por USB en caliente:
  - `tr 0` = **bit-bang** (GPIO puro, ningún periférico).
  - `tr 1` = **SPIM2** con un `nrfx_spim_t` propio (`NRFX_SPIM_INSTANCE(2)`), sin manejador
    de interrupciones (transferencias bloqueantes).
- Diagnóstico por USB (`epd`, `probe`, `who`) y **`dfu confirm`** para poder volver al
  cargador UF2 por software (la salida de emergencia).
- Todos los bucles de espera con **timeout**: el firmware no puede quedarse colgado.

La secuencia de arranque del panel está copiada de **GxEPD2_154_D67** (`_InitDisplay`):
`0x12` (soft reset, +10 ms), `0x01 C7 00 00`, `0x3C 05`, `0x18 80`, `0x11 03`,
`0x44 00 18`, `0x45 00 00 C7 00`, `0x4E 00`, `0x4F 00 00`; luego `0x22 E0` + `0x20`
(encender tensiones), el framebuffer por `0x24` y `0x26`, `0x22 F7` + `0x20` (refresco
completo) y `0x10 01` (sueño profundo).

## 3. LA CAUSA RAÍZ: el bus, y por qué el bit-bang anterior no era un bit-bang

### 3.1 El `SPI` del core vive en SPIM3

Leído en el core (`_pio_core/.../libraries/SPI/SPI.cpp`):

```cpp
#define _SPI_DEV NRF_SPIM3      // con SPI_32MHZ_INTERFACE == 0
SPIClass SPI(_SPI_DEV, PIN_SPI_MISO, PIN_SPI_SCK, PIN_SPI_MOSI);
```

Y **la radio usa ese `SPI` global** (`radio.cpp` llama a `SPI.begin()`). Por tanto el
periférico SPIM3 es de la radio, y todo lo que se hizo antes "sobre SPIM3 para la pantalla"
era una pelea por el mismo bloque de hardware: dos objetos `SPIClass` con el mismo
`drv_inst_idx`, PSEL pisándose, y el `begin()` del core (`if (initialized) return;`) que se
sale sin reconfigurar nada.

### 3.2 Los pines no eran del GPIO

Un pin con `PSEL` apuntándole **lo gobierna el periférico, no el GPIO**. Consecuencia: en
cuanto se llama a `nrfx_spim_init()` sobre SPIM3 con los pines de la pantalla,
`digitalWrite(P0.31, ...)` / `digitalWrite(P0.29, ...)` **no hacen nada**. El "bit-bang" de
la sesión anterior se ejecutaba después de esa inicialización y sin `nrfx_spim_uninit()`:
no movía un solo flanco de reloj. De ahí la conclusión equivocada "el bit-bang no funciona
en esta placa". **No era el bit-bang: los pines no eran suyos.**

### 3.3 Qué hace exactamente `nrfx_spim_uninit()` (leído en `nrfx_spim.c`)

```c
if (p_cb->miso_pin != NRFX_SPIM_PIN_NOT_USED) nrf_gpio_cfg_default(p_cb->miso_pin);
nrf_spim_disable(p_spim);
```

Es decir: **solo devuelve a estado por defecto el pin MISO**. No limpia `PSEL` de
SCK/MOSI/SS ni toca su `PIN_CNF`. Para que el GPIO vuelva a mandar hay que reconfigurar esos
pines a mano **después** de desinicializar. Lo limpio, y lo que funciona, es **no compartir
periférico**: SPIM2 para la pantalla, SPIM3 para la radio.

### 3.4 SPIM2 está libre y funciona

```
SPIM2 PSEL.SCK=P0.31 MOSI=P0.29 FREQ=0x40000000 ENABLE=1
```

El periférico acaba configurado en los pines de la pantalla y habilitado, y **dibuja**.
Nota: `nrfx_spim_init()` devuelve `0x0BAD0000` (`NRFX_ERROR_INTERNAL`), y **no he
encontrado en `nrfx_spim.c` ningún camino que devuelva ese código** (sus errores son
INVALID_STATE, NOT_SUPPORTED y BUSY). Es inocuo: lo que manda son los registros, y los
registros dicen que quedó bien. **No fiarse del valor de retorno; mirar PSEL/ENABLE.**
Un `NRFX_ERROR_BUSY` (0x0BAD0009) sí sería grave.

## 4. BUSY: el dato que cambia el diagnóstico de todo el proyecto

**En esta unidad el pin BUSY no se mueve nunca.** Medido durante refrescos completos, con
39.565 muestras por ventana y de dos formas independientes a la vez:

```
EPD BUSY: digitalRead altos=0/39565 | registro IN altos=0/39565 | ahora digitalRead=0
```

Y la pantalla se refresca igual (el hello world está en ella). Consecuencias:

1. La premisa de la que partían **todos** los intentos anteriores —"BUSY no se mueve = al
   panel no le llega nada"— **es falsa en esta placa**. Se descartaron variantes que
   probablemente ya funcionaban.
2. En el driver bueno hay que **dejar de usar BUSY como señal** y refrescar a ciegas con su
   tiempo (o mantenerlo con timeout, como red de seguridad, para no colgarse). GxEPD2 y
   cfr34k esperan a BUSY 1,5-2 s por refresco: aquí eso son 2 s perdidos por refresco.

### 4.1 Trampa de instrumentación (importante, y me pasó a mí)

Mi primera medida decía "BUSY=0 en 39.689 muestras", pero estaba hecha con una función mía
que leía el registro `IN` del puerto a mano (`(NRF_P0->IN >> pin) & 1`). Esa lectura
**resultó no ser fiable en este core**: comparada con `digitalRead()` daba 0 donde
`digitalRead()` daba 1, **incluso en el pin del LED azul**, que no tiene nada colgado.
Hubo que rehacer la medida con `digitalRead()`. Moraleja: **un instrumento sin contrastar
no es un instrumento**; y el contraste que lo delató fue usar un pin de control conocido.

### 4.2 La hipótesis del drenador abierto: probada y DESCARTADA

Todo lo anterior está medido con el pin como entrada **sin resistencia de subida** (igual
que lo configuran cfr34k y GxEPD2). Cabía sospechar que la salida BUSY fuese de **drenador
abierto**: sin subida se leería siempre 0 y nunca se vería el pulso, y el panel funcionaría
igual (GxEPD2 sale de su espera al primer muestreo y sigue). **Se probó**: se puso P0.03
como entrada **con pull-up** y se midió durante 2,5 s justo después de mandar el refresco:

```
EPD BUSY pull-up: tramoMasLargoEnBajo=2500 ms (altos=0)
```

**Ni con subida aparece un solo nivel alto: la línea se queda baja los 2500 ms.** Es decir,
algo la sujeta a 0 con más fuerza que la resistencia interna (~13 kΩ): o el panel mantiene
su salida BUSY baja siempre, o ese pin no es la señal que creemos, o esa salida está
averiada en esta unidad. En cualquier caso: **BUSY no sirve como señal en esta placa, con
subida ni sin ella.** (Ojo: el mensaje que imprime el firmware, "BUSY SI RESPONDE...", está
mal: su criterio era "tramo largo en bajo = responde", y una línea siempre baja también da
tramo largo. Lo que vale es el dato crudo: 2500 ms en bajo y **0 altos**.)

## 5. Lo que se probó y qué salió (incluidos los fracasos)

| Prueba | Resultado |
|---|---|
| Bit-bang tras inicializar SPIM3 sin desinicializar | **Inválida**: los pines eran del periférico. Explica el "el bit-bang no funciona" de la sesión anterior |
| Bit-bang con los pines libres (en el banco de pruebas aislado) | **Sigue sin estar demostrado.** Ver §5.1 |
| Bit-bang con etiqueta en pantalla (`tag 0`, después de haber usado SPIM2) | El operador seguía leyendo `SPIM2`: el bit-bang no pintó. **Pero la prueba está contaminada** (§5.1) |
| SPI por hardware sobre **SPIM3** | El nodo vive, pero es el periférico de la radio: pelea segura |
| SPI por hardware sobre **SPIM2** | **FUNCIONA: es el camino bueno** |
| `nrfx_spim_init` sobre SPIM2 devolviendo `0x0BAD0000` | Inocuo: PSEL y ENABLE quedan bien y dibuja |
| Esperar a **BUSY** | No sirve en esta unidad: nunca se mueve (0 altos de 39.565 muestras por los dos métodos) |
| BUSY **con pull-up** (hipótesis del drenador abierto) | **Descartada**: con subida tampoco hay ni un alto; la línea se queda baja los 2500 ms |
| Lectura del registro `IN` a mano para BUSY | **Instrumento roto**: daba 0 siempre; re-medido con `digitalRead()` |
| Prueba de pines con mi lectura directa | Dio "todos fallan", pero era el instrumento. Con `digitalRead()` los pines obedecen |
| Sonda de pines como "detector de alimentación" (diodos ESD) | **Descartada**: la lectura que la sostenía era la rota |
| Cambiar P1.11 (ALTO / BAJO / sin tocar) | Sin diferencia apreciable en las medidas; el panel dibuja con P1.11 en ALTO |
| Ciclar la corriente del panel, reset largo, orden exacto de la máquina de estados, salidas de alta corriente | Ya probado en sesiones anteriores; no cambiaba nada, y ya se sabe por qué (el problema era el bus) |
| Orientación del dibujo (rotación) | Rotación 3 = cfr34k = GxEPD2 en el T-Echo: **confirmada a ojo por el operador** |
| Comprobar que no se rompe nada más | Los 5 entornos existentes siguen compilando (hubo que añadir `-<hello_techo.cpp>` a sus `src_filter`, si no se colaba y rompía el enlazado por `setup()`/`loop()` duplicados) |

## 5.1 El bit-bang: lo que se sabe y lo que NO (honestamente)

No está demostrado que el bit-bang pinte en esta placa. Intentos y por qué no valen:

1. **El de la sesión anterior** (llamaba a `digitalWrite(P0.31/P0.29)` después de
   `nrfx_spim_init` sobre SPIM3, sin desinicializar): **inválido**, los pines eran del
   periférico. Ver §3.2.
2. **El mío con etiqueta** (`tag 0`, hello world + `BITBANG`): el operador seguía leyendo
   `SPIM2`, así que el bit-bang no pintó. **Pero también está contaminado por lo mismo**:
   cuando se ejecutó ese comando, SPIM2 ya estaba inicializado (lo hace la secuencia de
   arranque), así que `PSEL.SCK/MOSI` apuntaban a P0.31/P0.29 y el GPIO no mandaba ahí.
3. **El único intento limpio** es el primer pintado del arranque: en el banco de pruebas el
   bit-bang va **antes** de que nadie inicialice SPIM2, así que ahí los pines sí eran del
   GPIO. Ese pintado se hizo (todo negro), pero **no se puede saber si fue él o el de SPIM2
   que va justo después**: el resultado es el mismo negro. La única forma de distinguirlo es
   mirar la pantalla en el momento, y no se hizo.

**Conclusión:** SPIM2 es el camino demostrado y el que hay que usar. El bit-bang queda como
**no demostrado**, y ojo con la trampa: después de que SPIM2 haya reclamado los pines,
`nrfx_spim_uninit()` **no** los devuelve al GPIO (solo el MISO, ver §3.3), así que un
bit-bang "de después" no puede funcionar. Si alguien quiere zanjarlo, la prueba limpia es un
firmware que **nunca** inicialice SPIM2 y que pinte el hello world **solo** con bit-bang.

## 6. Estado de la placa al cerrar

- **Firmware grabado:** `techo_plus_hello` (el banco de pruebas de la pantalla), construido
  desde `src/hello_techo.cpp`. Se identifica por USB con `who` → `HELLO-TECHO-1`.
- **Responde por el puerto serie** (COM40) y acepta comandos: `help`, `epd`, `who`, `hello`,
  `black`, `white`, `tr 0|1`, `rot 0..3`, `probe`, `spin off`, `bl 0|1|2`, `tag 0|1`,
  **`dfu confirm`** y `reboot confirm`.
- **La pantalla muestra el "hello world"** (marco, cuadradito arriba a la izquierda, `hello`,
  `world`, `T-Echo Plus`). El dibujo final lo hace **siempre por SPIM2**, que es el camino
  demostrado. Con la tinta electrónica, esa imagen se queda aunque se apague o se regrabe.
- **Cómo volver al firmware bueno** (el iGate, EA2KR-3):
  `.\tools\hello_send.ps1 -Port COM40 -Line "dfu confirm"`, copiar
  `.pio\build\techo_plus_s140v7\firmware.uf2` a la unidad `TECHOBOOT` y esperar al reinicio.
  Si el USB no respondiera: **doble toque al botón de reset** (lo tiene que hacer una persona).
- **SoftDevice de la placa:** S140 **7.2.0** (leído en `INFO_UF2.TXT`), por eso el binario
  correcto es el del entorno `*_s140v7` (aplicación en `0x27000`).
- **Trampa de logística que costó un rato:** `dfu confirm` **no se procesa mientras el
  firmware está ocupado** en su secuencia de arranque. Con las esperas de BUSY largas
  (8 s por pintado) el arranque tardaba ~35 s y el comando se ejecutaba tarde: parecía que
  el nodo no obedecía. Con las esperas cortas, el arranque es de ~20 s y va fino. (Y al
  revés: si se manda `dfu confirm` y se espera solo 30 s a la unidad, se llega tarde.)

## 7. Ficheros

**Creados en esta sesión:**
- `src/hello_techo.cpp` — el banco de pruebas de la pantalla (dos transportes + diagnóstico).
- `tools/hello_send.ps1` — hablar con el nodo esperando a que arranque (`send.ps1` escribe
  demasiado pronto: abre el puerto, el nRF52 se reinicia, y la línea se pierde).
- `docs/HELLO_WORLD_TECHO.md` — este documento.

**Modificados:**
- `platformio.ini` — entorno nuevo `techo_plus_hello` y `-<hello_techo.cpp>` en el
  `src_filter` de los 5 entornos existentes (para que el banco de pruebas no se cuele en ellos).

**No se tocó:** `src/epaper_techo.cpp` (el driver del firmware principal), ni ningún fichero
fuera de este repositorio, ni la configuración del nodo.

## 8. Lo que hay que hacer en el driver bueno (resumen para el port)

1. **SPIM2** con un `nrfx_spim_t` propio (`NRFX_SPIM_INSTANCE(2)`), 4-8 MHz, modo 0,
   MSB primero, `ss_pin = NRFX_SPIM_PIN_NOT_USED`, sin manejador (transferencias bloqueantes).
   No usar el `SPI` del core (es SPIM3, de la radio) y **no tocar SPIM3**.
2. **No esperar a BUSY** (en esta unidad no se mueve): refrescar con su tiempo, con timeout.
3. Secuencia de comandos: la de GxEPD2_154_D67.
4. Framebuffer: `bitidx = (W-1-x)*H + y`, bit `0x80>>(bitidx&7)`, 1 = blanco. Es lo mismo que
   GxEPD2 con rotación 3 (que es la que se ve bien en esta placa).
5. `nrfx_spim_uninit()` NO devuelve los pines al GPIO (solo MISO): si algún día se quiere
   bit-bang, hay que reconfigurar `PIN_CNF` a mano después.
