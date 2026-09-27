# Integración de la pantalla de tinta electrónica en el firmware completo

Bitácora de trabajo. Zona: `_trabajo_ea2oy`. Hardware: LilyGO T-Echo Plus (nRF52840 +
SX1262 + GDEH0154D67/SSD1681 200x200), SoftDevice S140 7.2.0, app en `0x27000`.
Nodo: **EA2KR-3**, modo 2. Puerto serie: **COM40**.

> ## ★ RESULTADO: FUNCIONA ★ (2026-09-14)
>
> **El operador lo ha visto y lo ha confirmado: la pantalla pinta el contenido del firmware
> completo y se lee de pie.** El nodo arranca, responde por USB y `dfu confirm` funciona.
>
> La orientación natural es **`epdRotation = 1`** (el texto se lee de pie), y es el valor por
> defecto y el que está guardado en el nodo. Las otras tres (0/2/3 = 270/180/90 grados)
> existen para las unidades que traigan el panel montado distinto, y se cambian desde el
> configurador web o con `set epdRotation N`.
>
> **CORREGIDO EL 2026-09-15**: este encabezado decía que la posición natural era `0` y que
> los números se habían renumerado. **Era falso**: es justo al revés y el que manda es el
> código (`config.h`, `uint8_t epdRotation = 1`, y `gRotacion = 1` en `epaper_techo.cpp`).
> El mismo error estaba en la ayuda y en las opciones del configurador web
> (`web/index.html`), donde **sí tiene consecuencias**: la web abría con el `0` seleccionado
> y lo llamaba "natural (por defecto)", así que al guardar podía poner la pantalla torcida.
> Corregido en los dos sitios. Ver §2.4-bis.
>
> Las tres causas raíz que impedían que pintara están en §1. Lo que me equivoqué, en §2
> (incluida una tarde persiguiendo un problema eléctrico que no existía).

---

## 0. Punto de partida (comprobado antes de tocar nada)

```
$ .\tools\hello_send.ps1 -Port COM40 -Line "status","epd"
1.0alpha b3 EA2KR-3 D2 M2 GPS:fix RX:1 TX:1 DG:0 Bat:-- LIVE
display: en esta placa no hay tinta electronica
```

El firmware base `b3` arrancaba y respondía, con `display_epaper.cpp` (sustituto vacío) y la
llamada a `displayArrancaPantalla()` **comentada** en `main.cpp`. Ese es el estado seguro al
que se puede volver.

---

## 1. LAS TRES CAUSAS RAÍZ (todas medidas en hardware)

### 1.1 El cuelgue del arranque: `nrfx_spim_xfer()` espera SIN TOPE

`cores/nRF5/nordic/nrfx/drivers/src/nrfx_spim.c`, línea 598 (transferencia bloqueante, la que
se usa cuando el manejador es `NULL`):

```c
if (!p_cb->handler)
{
    while (!nrf_spim_event_check(p_spim, NRF_SPIM_EVENT_END)){}
```

**Ese bucle no tiene salida.** Si el periférico no completa, el firmware se queda ahí para
siempre: USB enumerado pero sin imprimir ni obedecer. Es exactamente el síntoma del encargo.

Y encima el evento estaba mal elegido: el manual del nRF52840 dice que `EVENTS_END` es *"End
of RXD buffer **AND** TXD buffer reached"*, o sea que **solo sube cuando terminan los dos
buffers**. Nosotros solo escribimos (`RXD.MAXCNT = 0`), así que **no sube nunca**. El evento
correcto al escribir es `EVENTS_ENDTX`.

**Arreglo:** las transferencias se programan a mano (registros) y se sondean **con tope de
tiempo**; si vence, se abortan y el firmware sigue vivo. Aunque fallara todo, el refresco
entero cuesta menos de un segundo.

### 1.2 ★★ El fallo que impedía pintar: un periférico con `PSEL` apuntando a un pin SE QUEDA ESE PIN, aunque esté deshabilitado ★★

No basta con `nrf_spim_disable()`. Mientras `PSEL.SCK` sea `0x1F` (P0.31), el GPIO **no**
manda en ese pin y **todas las lecturas dan 0**.

Medido en esta placa, en el mismo instante y con la misma sonda:

```
con PSEL apuntando a P0.31 -> SCK=0/00   ("parece sujeto a masa")
con PSEL desconectado      -> SCK=0/01   ("se gobierna perfectamente")
```

Este firmware llamaba a `epdSpiConfigura()` (que apunta `PSEL` a P0.31/P0.29) al arrancar, y
después el bit-bang intentaba mandar en esos mismos pines: **no mandaba**. El banco de
pruebas nunca inicializa SPIM2 cuando va por bit-bang, y **esa era toda la diferencia**.

La trampa ya estaba documentada en el proyecto ("un pin con `PSEL` apuntándole lo gobierna el
periférico, no el GPIO"), pero se colaba por otra puerta.

**Arreglo:** en modo bit-bang, `epdPinesBitBang()` hace `ENABLE = 0` **y** pone los tres
`PSEL` a `0x80000000` (desconectado) antes de tocar los pines.

### 1.3 SPIM2 no obedece a sus propias tareas

Con el periférico configurado y comprobado en los registros (`ENABLE`, `PSEL`), la sonda
`epdsonda` demuestra que no arranca:

```
tareas SUSPEND=0 STOP=0 | xfer5000: MAXCNT=5000 AMOUNT a1ms=0 a2ms=0 a3ms=0 fin=0 |
dur=54688us STARTED=0 ENDTX=0 END=0 | STALL=0x00000001
```

`TASKS_START` no produce `EVENTS_STARTED`; `TASKS_STOP`/`SUSPEND` no producen
`EVENTS_STOPPED`; el contador de EasyDMA se queda clavado. **Un periférico que no obedece ni
a una tarea que no usa datos no arranca.**

**Arreglo:** se pinta por **bit-bang**, que no depende de ningún periférico. SPIM2 queda en el
código, entero y seleccionable con `epdtrans 1`, por si algún día se averigua.

---

## 2. LO QUE ME EQUIVOQUÉ (lo más útil de esta bitácora)

### 2.1 El instrumento roto, otra vez (y ya iba documentado en el proyecto)

**`nrf_gpio_pin_read()` NO ES FIABLE EN ESTE CORE.** Medido con los pines empujados a 1:

```
EPD mueve? SCK: empujado(reg/dr)=0/1 | MOSI: 0/1 | BUSY: 0/1 | discrepancias=3
```

El registro dice 0 y `digitalRead()` dice 1 **para los mismos pines y en el mismo instante**.
Es el mismo fallo que `docs/HELLO_WORLD_TECHO.md` §4.1 ya tenía documentado para el registro
`IN`, y en el que volví a caer.

Con el instrumento roto me inventé un **problema eléctrico que no existía** ("SCK y MOSI
están sujetos a masa, al panel no le llega corriente") y hasta lo mandé por mensaje. **Era
falso.** Con `digitalRead()` sale `fallos=0x00`.

Segunda trampa, también mía: `nrf_gpio_cfg_output()` **desconecta el buffer de entrada**, así
que leer después da siempre 0 y *todo* parece sujeto a masa… incluido el LED azul, que no lo
está. De ahí la importancia del **pin de control** (LED azul P0.14): si el control no da
`0/01`, el que falla es el método y no la pantalla.

### 2.2 Una tarde perdida por usar texto simétrico como imagen de prueba

Con texto centrado y simétrico, **dos rotaciones distintas parecen la misma** y las
descripciones del operador ("girado", "boca abajo") se vuelven ambiguas. Se discutió durante
horas sobre descripciones subjetivas. **La solución fue dibujar una `F`**: una imagen
asimétrica se distingue sin ninguna duda en cuál de las cuatro orientaciones está, y además
delata si está espejada. La `F` se queda en las herramientas de taller (`epdrot N`).

### 2.3 Un detalle de logística que falseó todas las medidas de orientación

**Abrir el puerto serie reinicia la placa** (normal en los nRF52), y como el driver lee la
rotación de la **configuración**, cada vez que se abría el puerto para mandar un comando o
preguntar el estado, **la placa se reiniciaba y volvía a la rotación guardada**. Por eso
`epdrot N` "no se quedaba": solo duraba hasta la siguiente apertura del puerto. La solución
es **guardarla en la config** (`set epdRotation N`), no solo pintarla.

### 2.4 Conclusiones del encargo que resultaron falsas

| Decía el encargo | Lo medido |
|---|---|
| "El bit-bang no es camino: con `PSEL` apuntando a un pin lo gobierna el periférico" | El **mecanismo** es cierto, pero la conclusión no: cuando se sacó, además estaban mal el byte de más en `0x11/0x44/0x45` y el orden de bits. Con eso corregido, el bit-bang **es** el camino |
| "`0x22 = 0xE0` no existe en la secuencia que funciona" | El banco que **pinta** sí lo manda (`GxEPD2::_PowerOn()`): es lo que pone en marcha las tensiones del panel. Quitarlo fue un error mío |
| "La pantalla se refresca aunque BUSY no se mueva" | Cierto en el banco. Con el driver bueno **BUSY sí se mueve** (`BUSY=1`) |

### 2.4-bis El documento iba en contra del código en la ORIENTACIÓN (corregido 2026-09-15)

Este documento (§3, y el encabezado) decía que la posición natural era `epdRotation = 0`,
que los números "se renumeraron para que el valor por defecto fuera 0" y que `1` era "girada
90°". **Nada de eso es lo que hace el código**, que es quien manda (regla 0 de `REGLAS.md`):

| Dónde | Qué dice el código |
|---|---|
| `src/config.h` | `uint8_t epdRotation = 1;` → **por defecto 1**, comentado "1 = de pie" |
| `src/epaper_techo.cpp` | `int gRotacion = 1;` → "**1 = DE PIE (posicion natural / de fabrica)**" |
| `src/epaper_techo.cpp` (`px()`) | `case 1 → xr = y; yr = H-1-x` = la posición natural confirmada a ojo |

Es decir: **1 = de pie** (por defecto), **2 = 90°**, **3 = 180°**, **0 = 270°**. El número
**no** se renumeró, y el motivo está explicado en `config.h`: renumerar cambiaría el
significado de un valor ya guardado en la flash del nodo y pondría torcidas solas a las
unidades actualizadas.

**Dónde sí tuvo consecuencias**: en el configurador web (`web/index.html`) la ayuda decía
"el 0 es la posición natural y es lo que viene de fábrica", la opción `0` se llamaba
`0 — natural (por defecto)` y **la lista de valores por defecto de la página abría con
`epdRotation: 0`**. O sea: el usuario que tocara esa pestaña y guardara podía **torcer la
pantalla** y, de paso, el configurador recomendaba un valor que el firmware no considera el
de fábrica. Corregido el 2026-09-15 (ayuda, etiquetas de las cuatro opciones y valor por
defecto de la página, los tres al `1`).

Falta el mismo cambio en el configurador público (`CONFIGURADOR-WEB-APRS-EA2OY`), que está
**fuera de la zona de trabajo** y que ya estaba apuntado como pendiente.

---

## 3. Qué se ha cambiado

### `src/epaper_techo.cpp` — reescrito

- **Dos transportes**: bit-bang (el que se usa) y SPIM2 (seleccionable con `epdtrans 0|1`).
- **Transferencias con tope de tiempo**: el firmware **no puede quedarse mudo**.
- **`epdPinesBitBang()` apaga SPIM2 y desconecta sus `PSEL`** antes de quedarse los pines, y
  deja el reloj **en BAJO** (modo 0).
- **Secuencia de refresco igual a la del banco**: `0x22 0xE0` + `0x20` (tensiones), `0x24` y
  `0x26` con la imagen, `0x22 0xF7` + `0x20`, y `0x10 0x01` (dormir).
- **Orientación configurable**: `px()` traduce de coordenadas lógicas a la trama según
  `gRotacion`, que se lee de la **configuración** (`epdRotation`), con **aplicación en
  caliente** (si se guarda otro valor, se repinta sin reiniciar).
  **`1` = posición natural (de pie) y es el valor por defecto**; `2` = 90°, `3` = 180°,
  `0` = 270°. (La versión anterior de este párrafo decía que la natural era el `0` y que los
  números se habían renumerado: **era falso**, ver §2.4-bis.)
- **Imagen de prueba asimétrica (la `F`)** disponible con `epdrot N`.
- Se quita todo lo experimental: sondas de estado, bit-bang de pruebas, "recetas", esperas a
  BUSY, ciclos de corriente.
- `displayInit()` / `displayInitTrasRadio()` quedan **vacíos**: todo va en
  `displayArrancaPantalla()`, llamado a los 6 s desde el bucle, con el USB ya vivo.
- `textWidth()` arreglado (no contaba la escala).

### `src/config.h` / `src/config.cpp` — ajuste persistente

Campo **`epdRotation`** (0..3, por defecto 0), con validación `epdRotation 0..3`, incluido en
`configToJson` (sobrevive a reinicios y `factory_reset` lo devuelve a 0) y disponible en el
`set` del CLI **sin tocar `cli.cpp`**: el `typedSet` valida contra las claves de
`configToJson`, así que el campo quedó soportado al añadirlo a la config.

### `src/sensors.cpp` — bug real corregido

Medía la batería con `analogRead(31)` = AIN7 = **el SCK de la pantalla**, cuando la batería
del T-Echo es **P0.04** (`pins_techo.h`, `variants/techo/variant.h`). Por eso `bat` contestaba
`0.00V` y el status `Bat:--`: **la batería nunca se estaba midiendo**. Corregido a P0.04
(`BATTERY_ADC_PIN`). Sigue dando 0.00 V (en esta unidad el divisor no está poblado o no
llega), pero el pin de la pantalla ya no se toca.

### `web/index.html` — configurador web

Campo nuevo en la sección **"Pantalla"**: desplegable **`epdRotation`** con las cuatro
opciones etiquetadas honestamente (`0 — natural (por defecto)`, `1 — girada 90°`,
`2 — boca abajo (180°)`, `3 — girada 270°`), su texto de ayuda en los dos idiomas, y el valor
por defecto en la lista de la página.

**OJO:** el configurador se sirve desde dos sitios. Aquí se ha tocado **solo el del firmware**
(`_trabajo_ea2oy/web/index.html`). El repo público `CONFIGURADOR-WEB-APRS-EA2OY` está fuera de
la zona de trabajo y **no se ha tocado**: hay que copiar allí el mismo campo.

### `platformio.ini`, `src/main.cpp`, `tools/graba.ps1`

El entorno `techo_plus_s140v7` compila el driver de verdad (con la línea para volver al
sustituto escrita al lado); `APP_BUILD_NUM` de `"b3"` a `"b4"`; `displayArrancaPantalla()`
activado; y `graba.ps1` (nuevo) graba con comprobación de que el nodo vuelve a responder.

---

## 4. El menú del aparato: NO existe en la tinta electrónica

Comprobado en el código, no supuesto: `epaper_techo.cpp` implementa `menuOpen()`,
`menuClose()`, `menuShort()`, `menuLong()`, `menuIsEditing()` y `menuEditCancel()` **vacíos a
propósito**, con este comentario en el propio fichero:

> *"Menu: en esta version no hay menu de pantalla. El boton cambia de pantalla y el doble
> toque manda baliza, igual que en la OLED, pero sin arbol de ajustes."*

El árbol de menús sí existe en `src/display.cpp`, pero ese es el driver de la **OLED** y en el
T-Echo **no se compila** (`src_filter` lo excluye). Por tanto **no hay nada que enganchar**:
montar el menú en tinta electrónica es un trabajo aparte (máquina de estados + interfaz de
menú, con una pantalla que tarda ~2 s por refresco completo).

**Decisión: no se monta.** El ajuste de orientación se cambia desde el **configurador web** y
con **`set epdRotation N`**. Queda documentado como trabajo futuro.

---

## 5. Comandos de taller (por USB)

| Comando | Para qué |
|---|---|
| `epd` | Resumen: transporte, **rotación aplicada**, timeouts, errores, estado de los pines |
| `epdrot 0..3` | Cambia la orientación **en caliente** y repinta la `F` (sin grabar) |
| `epdrepinta` | Repinta el estado del nodo y dice cuántos bytes ha movido |
| `epdpines` | Sonda de pines (con el LED azul como control) |
| `epdvolcado` | Vuelca `PIN_CNF` y el comportamiento con pull-up/pull-down |
| `epdmueve` | Comprueba que SCK/MOSI se mueven de verdad, y saca el `bitidx` de cada rotación |
| `epdsonda` | Registros crudos de SPIM2 (tareas, eventos, EasyDMA) |
| `epdpwr 0\|1\|2` | Fuerza P1.11 (interruptor del panel): bajo / alto / suelto |
| `epdtrans 0\|1` | Elige transporte en caliente (bit-bang / SPIM2) |

Se dejan puestas a propósito: en una placa como esta (con un panel que no se deja gobernar
por el periférico) son la diferencia entre poder diagnosticar y no poder. Lo que sí está
documentado es que **`epdrot` por sí solo NO persiste** (abrir el puerto reinicia la placa):
para que un cambio dure hay que guardarlo con **`set epdRotation N`**.

---

## 6. Estado verificado del driver

```
EPD pines: SCK=0/01 MOSI=1/01 CS=1/01 DC=1/01 RST=1/01 | CONTROL LED P0.14=0/01 | fallos=0x00
PANTALLA: refresco: fallos=0 bytesBitBang=10002 transporte=0 PSEL.SCK=0xFFFFFFC0
EPD repintado: bytes=10034 fallos=0 ms=2773
EPD listo=1 transporte=bit-bang timeouts=0 errores=0 ultimoRefresco=2771ms | BUSY=1
```

- Los cinco pines se gobiernan (`fallos=0x00`), validado con el LED de control.
- Se mandan los dos planos completos (2 × 5000 + comandos) y el refresco dura ~2,7 s, que es
  el tiempo real del panel. **Cero transferencias abortadas.**
- **BUSY se mueve** (`BUSY=1`): el panel trabaja.
- Los `bitidx` del primer píxel con las cuatro rotaciones son **cuatro números distintos**
  (`R0=1206 R1=38208 R2=38391 R3=1791`): la rotación llega de verdad al mapa de píxeles.
- **Confirmado a ojo por el operador:** el texto se lee de pie con `epdRotation = 0`.

---

## 7. Cómo volver al firmware base sin pantalla

```powershell
$env:PLATFORMIO_CORE_DIR="C:\Users\Jesus\Desktop\LoRa_APRS_iGate-main\_pio_core"
cd C:\Users\Jesus\Desktop\LoRa_APRS_iGate-main\_trabajo_ea2oy
# en platformio.ini, entorno techo_plus_s140v7:
#   src_filter = +<*> -<display.cpp> -<epaper_techo.cpp> -<hello_techo.cpp>
& "C:\Users\Jesus\.platformio\penv\Scripts\platformio.exe" run -e techo_plus_s140v7
.\tools\graba.ps1 -Uf2 ".pio\build\techo_plus_s140v7\firmware.uf2"
```

Si el nodo no respondiera: **DOS toques seguidos al botón de RESET** (lo tiene que hacer una
persona) y grabar `RESCATE_hello.uf2` con `.\tools\graba.ps1 -Uf2 "RESCATE_hello.uf2" -NoDfu`.

---

## 8. Ficheros

| Fichero | Qué |
|---|---|
| `src/epaper_techo.cpp` | Driver reescrito: dos transportes, topes de tiempo, PSEL desconectado, secuencia del banco, rotación configurable |
| `src/config.h` / `src/config.cpp` | Campo `epdRotation` persistente (0..3, por defecto 0) |
| `src/sensors.cpp` | Batería de P0.31 a P0.04 (el pin de la pantalla ya no se toca) |
| `src/cli.cpp` | Comandos de taller |
| `src/main.cpp` | `displayArrancaPantalla()` activado |
| `web/index.html` | Campo `epdRotation` en la sección "Pantalla" |
| `platformio.ini` | `src_filter` del driver; `APP_BUILD_NUM` b3→b4 |
| `tools/graba.ps1` | Grabación con comprobación y red de seguridad |
| `docs/INTEGRACION_PANTALLA.md` | Este documento |

---

# PASO 1 — REFRESCO PARCIAL (2026-09-15, sesión siguiente)

> **Estado: ESCRITO Y COMPILADO. PENDIENTE DE GRABAR Y DE VALIDAR A OJO.**
> Lo que sigue se escribe ANTES de grabarlo, para que quede claro qué se espera ver y qué
> NO. Si lo medido no coincide, se corrige aquí mismo (regla 0 de `REGLAS.md`).

## P.1 Qué se ha cambiado y por qué

Hasta ahora **todos** los refrescos eran completos: ~2,7 s con su parpadeo a blanco y negro.
Eso es lo que hacía inviable el carrusel (Paso 2) y la cosmética (Paso 3).

**El error de fondo, y es de concepto, no de bytes:** el SSD1681 tiene DOS memorias de imagen
—la **actual** (comando `0x24`) y la **anterior** (`0x26`)— y al refrescar **las compara**,
moviendo sólo los píxeles que cambian. El driver mandaba **la misma imagen en los dos planos**.
Con los dos planos iguales el controlador concluye "no hay nada que cambiar" y el refresco se
comporta como un completo. **Ese era el motivo de que no hubiera parcial posible**: no había
nada que activar, había que mandar dos planos DISTINTOS.

Cambios en `src/epaper_techo.cpp`:

1. **Segundo framebuffer `gBufPrev` (5000 B)** + bandera `gPrevValido`. Al terminar bien un
   refresco, `gBuf` se copia a `gBufPrev`: ese es el plano "anterior" del siguiente.
2. **`epdRefresca(parcial, bufAnterior, bufActual)`**: el cuerpo común. Manda `0x26` con el
   plano anterior y `0x24` con el actual, y sólo cambia dos bytes entre las dos formas:
   - COMPLETO: `0x3C = 0x05` y `0x22 = 0xF7`.
   - PARCIAL: `0x3C = 0x80` y `0x22 = 0xFF`.
   (Copiado de `firm_ref_techo/.../src/epaper.c`, `FULL_UPDATE_SEQUENCE` y
   `PARTIAL_UPDATE_SEQUENCE`. En el completo se manda la misma imagen en los dos planos a
   propósito: es lo que fuerza el barrido entero que quita los fantasmas.)
3. **El parcial también hace su reset hardware** del panel (`epdReset()`), como el de
   referencia: las dos secuencias empiezan por reset + `0x12`. No es opcional.
4. **Completo forzado para quitar fantasmas**: cada **60 minutos** sin un completo (el número
   del de referencia, `main.c` → `m_epaper_force_full_refresh`) o cada **720 parciales**,
   lo que llegue antes. Y **siempre** el primero tras el arranque (al arrancar no se sabe qué
   hay pintado: la tinta es bistable y la imagen puede ser de cualquier refresco viejo).
5. **El parcial cae a completo si no hay plano anterior fiable**: si un refresco tuvo
   transferencias abortadas, lo que hay en el panel no es lo que dice `gBuf`, así que no se
   arrastra el error; se repinta entero.
6. **Red de seguridad nueva para BUSY**, que antes no existía en el refresco: antes de
   empezar se espera (con tope de 2 s) a que el panel suelte BUSY, y después de mandar
   `0x20` (a pintar) se espera a BUSY con tope (1,5 s en parcial, 4 s en completo) y además
   un mínimo por tiempo (350 ms / 2000 ms). **Nunca hay una espera sin salida**: si BUSY no
   informa —que es lo medido en esta unidad— se sale por tiempo y el nodo sigue vivo.
7. **`dibujaEscena()`**: se ha sacado a una función el "qué se ve" (aviso reciente o escena
   del carrusel). La usan el bucle normal y la prueba de taller, para que lo que se prueba
   sea exactamente lo que se verá en el carrusel y no una copia que puede divergir.

## P.2 Cómo se prueba (y por qué así)

Dos comandos nuevos de taller, y **una sola cosa que mirar cada vez**:

| Comando | Qué hace |
|---|---|
| `epdrepinta` | Repinta la pantalla de estado. **La primera vez tras el arranque es COMPLETO** (parpadea: es correcto). **La segunda ya es PARCIAL** (no debe parpadear). La respuesta dice cuál de los dos ha hecho y cuántos ms. |
| `epdparcial N` | Prueba del carrusel: cambia de escena N veces (por defecto 3) y termina siempre en la pantalla de estado. El primer refresco es completo y los siguientes parciales. |

**La trampa que invalida la prueba, otra vez:** abrir el puerto serie reinicia la placa. Por
eso **no se manda nada entre los dos comandos**: se manda el primero, se mira, se contesta, y
sólo entonces el segundo. (Es la trampa documentada en §2.3, la que costó una tarde.)

**Lo que hay que ver, en una frase:** en el parcial la pantalla **cambia de imagen sin el
parpadeo largo a blanco y negro**. Si parpadea igual que el primero, el parcial NO está
funcionando por mucho que el contador diga "parcial".

## P.3 Lo que hay que medir (y apuntar aquí)

| Dato | Cómo se saca | Valor |
|---|---|---|
| Duración real del parcial | `epdrepinta` → campo `parciales=… (ultimo Nms)` | **3.217 ms** (medido 2026-09-15) — **NO son los ~0,4 s que debería: ver P.6** |
| Duración real del completo (referencia) | el mismo comando → `completos=… (ultimo Nms)` | **4.811 ms** (arranque), ~2.700 ms con el driver anterior |
| Si BUSY informa en el parcial | campo `BUSYvisto=` del comando `epd` | `BUSYvisto=0`, `BUSYtimeouts=1`: **BUSY no soltó en 2 s antes de empezar** |
| Si aparecen fantasmas | a ojo del operador, tras varios parciales | sin fantasmas visibles tras 1 parcial |
| ¿Parpadea el parcial? | **a ojo del operador** | **NO parpadea** ([OPERADOR], 2026-09-15): "no se ha puesto en blanco y negro; ha escrito debajo de 'T-Echo Plus' sin parpadeos" |

> **Aviso de método**: los números de tiempo de los comandos son **medidos por el nodo**,
> no [OPERADOR]. Lo que dice si el parcial "no parpadea" es **sólo el operador**: aquí no hay
> cámara y no se supone lo que se ve.

## P.6 ★ EL PARCIAL FUNCIONA A OJO, PERO CUESTA 3,2 s: LOS 2 s SE LOS COME UNA ESPERA ★

**Lo que está confirmado** (2026-09-15, [OPERADOR] + medidas del nodo):

- El parcial **pinta de verdad y no parpadea**: el operador vio cambiar el texto de debajo de
  "T-Echo Plus" sin que la pantalla se pusiera en blanco y negro.
- El nodo informa `fallos=0` y `bytes=10034`: los dos planos han viajado.
- **Pero tarda 3.217 ms**, casi lo mismo que un completo (4.811 ms). Eso hace que el carrusel
  (Paso 2) siga sin ser cómodo.

**La causa, medida:** el parcial **no es lento**; lo que pasa es que **la espera previa de
BUSY se come 2.000 ms enteros** (`BUSYtimeouts=1`, `BUSYvisto=0`). La cuenta cuadra:

    2.000 ms (espera previa de BUSY, tope agotado) + ~1.200 ms (secuencia del parcial) = 3.220 ms

O sea: **BUSY está en ALTO justo antes de empezar** (el panel sale de su sueño profundo con el
pin arriba) y **el código le espera 2 segundos a que baje, y no baja**. Esa espera previa se
puso como red de seguridad al escribir el parcial; **no aporta nada aquí y cuesta 2 s por
refresco**. Es lo primero que hay que corregir (y hay que volver a medir después, porque
quitarla es justo lo que puede dejar el panel a medias: se quita **una cosa** y se mide).

**Nota honesta sobre los tiempos:** el anterior driver tardaba ~2,7 s en el completo y ahora
marca 4,8 s. La diferencia no es que el refresco sea más lento: es que **ahora se le suma la
espera previa de BUSY** (2 s) que antes no existía. El trabajo real del panel es el mismo.

**Y una pregunta del operador que conviene dejar escrita**, porque es la que decide el Paso 2:
**sí, se puede cambiar texto por otro y borrarlo sin parpadeo** — eso es exactamente lo que
hace el parcial: el controlador sólo mueve los píxeles que difieren entre el plano viejo y el
nuevo. Lo que **no** se puede es hacerlo indefinidamente: cada parcial deja un resto (fantasma)
y por eso hay que forzar un completo de vez en cuando. El límite práctico no es "qué texto",
es "cuántos parciales seguidos antes de limpiar".

## P.7 ★ RESULTADO FINAL DEL PASO 1 (2026-09-15): VALIDADO A OJO ★

**El parcial está hecho y funciona.** Lo que sigue está medido en el nodo y confirmado por el
operador, no deducido:

| Qué | Medida |
|---|---|
| ¿Parpadea el parcial? | **NO** — [OPERADOR]: "no se ha puesto en blanco y negro; ha escrito lo que dices debajo de la línea T-Echo Plus sin parpadeos" |
| Duración del parcial | **1.513-1.517 ms** (antes de bajar la espera: 3.217 ms) |
| Duración del completo | **3.109 ms** por el mismo camino (con el driver anterior: ~2.700 ms) |
| Fallos / timeouts / errores | **0 / 0 / 0** en todos los refrescos |
| Fantasmas tras **14 parciales seguidos** | [OPERADOR]: "voy viendo cómo cambia el carrusel sin mucho fantasma y no me disgusta" |
| Pantalla tras todo el trabajo | Sigue de pie, `rotacion=1`, `PANTALLA OK` |

**Lo que se hizo, en orden, y lo que costó cada paso** (esto es lo que hay que aprender):

1. **Los dos planos distintos** (`0x26` anterior / `0x24` actual): es lo que hace que el
   controlador mueva sólo lo que cambia. Sin esto no hay parcial posible, por muchos flags que
   se pongan.
2. **Primer intento medido: 3.220 ms.** Seguía sin parpadear, pero era casi tan lento como el
   completo. **La culpa era de una espera previa de BUSY con tope de 2.000 ms que se agotaba
   entera** (`BUSYtimeouts=1`): el panel sale del sueño profundo con BUSY en ALTO y no lo baja
   hasta que empieza a trabajar, así que la espera se pasaba los 2 segundos mirando un pin que
   no se movía.
3. **Bajada esa espera a 300 ms** (una sola cosa, medida antes y después): **3.220 → 1.516 ms**
   y el operador confirmó que seguía sin parpadear y sin fantasmas.
4. **Prueba del carrusel** (`epdparcial 3`, tres escenas + vuelta a la de estado): **4
   refrescos parciales en 7.563 ms**, sin parpadeo, con el operador mirando.

**Lo que queda abierto, y se dice tal cual** (no se inventa una explicación):

- **En las pruebas del carrusel el contador marcó `0 completos + 4 parciales`**, cuando lo
  esperado era que el primero fuera completo (al arrancar no hay plano anterior fiable). El
  contador general sí tenía sus completos (`completos=2`, los del arranque). **No está
  explicado**: la hipótesis más probable es de **momento**, no de lógica — el comando se
  procesa en el bucle y éste se pasa los primeros ~15 s en `displayArrancaPantalla()` (6 s de
  espera + dos refrescos completos), así que el comando cae justo al terminar el arranque, a
  veces antes y a veces después de que el bucle haga su primera pasada. **No afecta a lo que
  se ve** (el completo del arranque ya se ha hecho), pero hay que mirarlo antes de dar el
  carrusel por bueno, porque el carrusel automático **depende** de ese primer completo.
- **No se sabe cuántos parciales aguanta la pantalla antes de que el fantasma moleste.** Con
  14 seguidos el operador dice que no le disgusta; el tope puesto es 60 min o 720 parciales.
  Eso hay que medirlo con el aparato en marcha (Paso 2), no en el banco.

## P.9 EL CARRUSEL AUTOMATICO (Paso 2) — HECHO Y MEDIDO (2026-09-15)

**Qué se ha añadido** (todo en `src/epaper_techo.cpp`):

- **`gEscena` rota sola** cada `kEscenaAutoMs` = 45 s, si `sceneAutoAdvance` (el ajuste que la
  web ya tenia, ahora de verdad conectado) esta en on. El anterior avance solo existia con el
  boton.
- **Al pulsar el boton, el carrusel se para 2,5 min** (`kPausaTrasBotonMs`), para que no le
  cambie la pantalla a quien la esta leyendo. Mismo patron que la OLED, pero con un tiempo de
  pausa realista (alli es 15 s porque se redibuja al instante).
- **Se repinta solo si cambia de verdad.** El firware repintaba cada 5 s por reloj; con el
  carrusel se midieron **19 repintados en 50 s**. Se sustituyo por una **huella del contenido**
  (`huellaContenido()`, FNV-1a de 64 bits): escena, modo, aviso, GPS (posicion y velocidad),
  sensores, bateria y contadores. Si la huella no cambia, **no se manda nada al panel**.
  Queda un repintado "de refresco" cada 60 s (`kRepintadoMaxMs`) por si algo se escapa.

**LOS DOS CULPABLES DE LOS REPINTADOS DE MÁS, y como se cazaron** (medido con el instrumento
nuevo `epdhuella`, que enseña la huella y todos sus ingredientes para comparar):

1. **La bateria**: `powerReadMv()` lee el ADC cada vez y le pasa un suavizador. En esta unidad
   el divisor de bateria no esta poblado (ya estaba documentado, `bat` contesta 0,00 V), asi
   que el convertidor lee RUIDO y el numero cambia a cada muestra. **Solucion**: se dibuja y se
   compara con la MISMA lectura cacheada que el resto del firmware (`sensorsBatteryVolt()`),
   redondeada a 0,01 V; sin lectura creible se escribe "Bateria: sin lectura" en vez de un
   "0,00 V" que no era cierto.
2. **Los satelites "a la vista"** (`satsInView`): bailan de 5 a 11 en pocos segundos (entran y
   salen del cielo) y son lo que quedaba haciendo repintar sin parar. Se quitaron de la huella.
   La posicion y la velocidad, que si importan, siguen dentro.

**Medida final (reposo de 60 s, nada tocado):**

| Antes | Despues |
|---|---|
| 52 repintados parciales en 60 s | **2** (los del cambio de escena del carrusel) |

**Estado del Paso 2:** hecho y compilado. Pendiente de que el operador lo vea una vez (rotacion
por si misma a los 45 s, sin que el texto salga torcido), porque el que decide si se ve bien es
el ojo, no el contador.

---

## P.8 Trampa de taller nueva: cuándo llega de verdad un comando (2026-09-15)

`hello_send.ps1` espera 3,5 s por defecto antes de escribir, y **eso ya no es bastante** en
este firmware: el arranque de la pantalla empieza a los ~6 s (`displayArrancaPantalla()`,
llamada desde el bucle para que el USB esté vivo antes de tocar el panel) y **tarda unos 9,5 s
más** (6 s de espera + dos refrescos completos). Si se manda un comando con la espera corta,
cae **en mitad del arranque de la pantalla** y las trazas salen mezcladas y sin los mensajes
del arranque (es lo que pasó en las dos primeras pruebas del carrusel).

**Para trabajar con la pantalla: `-BootMs 17000`.** Los contadores que se leen así son los de
un arranque completo, que es lo que hace falta para saber si un refresco fue completo o
parcial.

## P.4 Decisiones que se han tomado sin poder preguntar (y por qué)

- **El `0x22 0xE0` (tensiones) se manda también en el parcial.** El de referencia **no** lo
  manda en el parcial (tiene un `0x22 0xB9` comentado en esa tabla). Aquí sí, porque entre
  refresco y refresco este firmware **manda el panel a dormir** (`0x10 0x01`) y no consta que
  conserve las tensiones. **Si el parcial sale sucio o a medias, esto es lo PRIMERO que hay
  que probar a quitar.**
- **Se mantiene el envío en dos transferencias** (`0x26` y después `0x24`), como el de
  referencia. Mandarlas en una sola conversación con CS bajo sería más rápido, pero cambia
  dos cosas a la vez y la regla es una cada vez.
- **El tope de 720 parciales** es el equivalente a los 60 minutos del de referencia en el
  peor caso de este firmware (un refresco cada 5 s). El que va a saltar de verdad es el de
  tiempo.

## P.5 Riesgo y vuelta atrás

- El firmware nuevo **no puede quedarse mudo por la pantalla**: todas las esperas de BUSY
  tienen tope y las transferencias ya lo tenían.
- Si el nodo se quedara mudo de todas formas: **doble toque al RESET** (lo hace una persona)
  y grabar `RESCATE_hello.uf2`, que responde siempre.
- Vuelta atrás del código: cambiar el `src_filter` del entorno `techo_plus_s140v7` como está
  documentado en `platformio.ini` (§7 de este mismo documento).

## P.10 KISSOFF: salir del modo KISS sin tocar el selector (2026-09-15)

**El problema que lo pide** (lo señaló el operador): con `tncProtocol` en KISS, el firmware
trataba **todo el tráfico como ordenes del programa host** (`tncHostDriven()` y
`autoBeaconAllowed()`), así que los comandos normales por USB —baliza manual, WX, telemetría,
mensajes— se negaban aunque **no hubiera ninguna app KISS hablando**. Puro enganche lateral
del modo KISS: el operador se quedaba sin poder mandar sus propias órdenes.

**La solución** (hecha y probada): comandos + estado, **en memoria** (no persistente; se
pierde al reiniciar, lo que es lo deseable para un *override*):

- `kissoff` → `tncKissPause()`: devuelve el mando al operador **sin tocar el selector**.
- `kisson` → `tncKissResume()`: KISS vuelve a mandar.
- `status` → `radio.kissPaused` lo refleja.

**Qué cambia exactamente (y qué NO):**

| Función | Antes (KISS) | Tras `kissoff` |
|---|---|---|
| `autoBeaconAllowed()` (aprs.cpp) | `false` | `true` |
| `tncHostDriven()` (main.cpp) | `true` | `false` |
| Convivencia por bytes (0xC0 = KISS, resto lineas) | intacta | **intacta** (una app KISS sigue pudiendo hablar) |
| El selector `tncProtocol` | KISS | **KISS** (no se toca) |

**Se coló de paso un error mío** que corrigo aquí y en el estado: al probar si `beacon`
transmitía, cambié el modo del nodo a `0` y **no lo restauré**. Lo he devuelto a `mode 2`.
La regla queda anotada: si se toca el modo para una prueba, restaurarlo en la MISMA tanda.

**Lo que NO es culpa del modo KISS** (medido, no supuesto): la baliza manual no transmitía
porque el nodo está en **modo 2 y sin fijación GPS** (la baliza de rastreador se niega a usar
la posición configurada para no publicar una posición falsa — por diseño). El WX no transmitía
porque **`wxSensorActive` está desactivado en la configuración** y además el formato necesita
sello de hora GPS y una sonda meteorológica, que esta unidad no tiene conectada. O sea: **con
`kissoff` las órdenes ya pasan; las compuertas que quedan son las legítimas (posición/hora
GPS, sensor).** Para ver el aviso TX en pantalla hace falta una transmisión real (fijación GPS
o tráfico repetible por el digi), no un comando.

## P.11 La interfaz al día y el PLAN DEL MENU (2026-09-15, cierre de sesión)

**Hecho y grabado** (commit base `d16d092`):
- Refresco **parcial** (1,5 s, sin parpadeo) + carrusel automático.
- **8 escenas**: Estado, Radio, Sensores, Sistema, Estaciones, Últimos RX, Últimos TX, GPS.
- **Splash de arranque** ~6 s (marca "EA2OY APRS SYSTEM", indicativo **sin SSID** vía
  `callSinSSID()`, barra de progreso animada). Quitado el texto de prueba "PANTALLA OK".
- **Batería**: % encima del icono + cuerpo que se vacía; borne visible.
- **Pie**: 8 puntitos del carrusel + batería.
- **Botones**: físivo corto = cambiar diapositiva; **capacitivo** (P0.11, activo en BAJO +
  INPUT_PULLUP) = cambiar diapositiva; **luz 5 s** tras cualquier toque.
- **KISSOFF/KISSON** (override del modo KISS) + comando `cap` (sonda del táctil).

**PLAN DEL MENU EN PANTALLA** (trabajo en marcha):
- Alcance: **menú completo como la OLED** (secciones RADIO/RASTREADOR/REPETIDOR/APRS/
  SENSORES/PANTALLA/ENERGÍA/CONTROL REMOTO/AJUSTES + acciones).
- Mapa de botones (decidido): **capacitivo navega** por las filas, **físico corto confirma/
  entra**, **físico largo vuelve atrás**. Fuera del menú, físico corto cambia de diapositiva.
- Peculiaridad de la tinta: cada pantalla cuesta ~1,5 s, así que el menú se repinta por
  parcial y solo cuando cambia la selección.
- Enfoque: subagentes por módulo (esqueleto, edición de valores, secciones, acciones),
  auditados y mergeados con cohesión; **un commit por módulo**.
- Enlace técnico: el árbol vive en `display.cpp` (OLED, no se compila aquí); hay que portarlo
  a `epaper_techo.cpp` con su propia máquina de estados, usando `config.cpp` para leer/validar
  y `store.h`/`storeSave` para persistir.

## P.12 MENU EN PANTALLA — PORTADO Y GRABADO (cierre 2026-09-15)

Hecho: el menú de 2 niveles (lista de categorías → ítems de una categoría) ya vive en
`epaper_techo.cpp`, con las 14 secciones de la OLED (Modo, GPS, Balizas, Mensajes, Tracker,
Radio, Digi, APRS, Bluetooth, Sensores, Pantalla, Energía, Remoto, Ajustes) y sus ~59 ítems.

**Mapa de botones (operador):** capacitivo = navegar / cambiar valor en edición; físico corto
= entrar / confirmar; físico largo = volver atrás. Fuera del menú, físico corto y capacitivo
cambian de diapositiva.

**Modelo de edición (adaptado a la tinta, edición "live"):** al entrar en un ítem, el valor se
ve en la fila; cada NAVEGAR (capacitivo) cambia y GUARDA (memoria + flash vía `cliTypedSet` +
`storeSave`, el mismo motor que `set`). BOOL se alterna al confirmar directamente. TNTP hubo
que exponer `cliTypedSet` (envoltorio público de `typedSet`, que era interno a cli.cpp).

**Acciones** portadas (baliza, telemetría, coordenadas, ver GPS, batería, silenciar, dormir,
reiniciar, valores de fábrica, borrado) con las mismas llamadas que la OLED.

**Render:** lista de categorías + filas con el valor a la derecha, selección en recuadro,
"< Volver" virtual, todo a escala 1 coherente. Commit `af28a56`.

**Pendiente que no se puede validar sin el operador (mañana):** EL ORDEN/COMPORTAMIENTO en la
pantalla real (navegación, edición y que nada se pise). El código compila, arranca y responde;
falta la vista humana.

### Auditoría del motor del menú (subagente, 2026-09-15) y arreglos aplicados
Un subagente auditor revisó el motor y encontró (todo [VERIFICADO]) y ya corregido, commit
`6ecfb4c`:
- **G1** edición de STRING no persistía ni cargaba el valor actual → ahora `comenzarEdicion`
  carga el valor y `menuShort` guarda el buffer limpio al confirmar (callsign, symbol, msgText...).
- **G2** el ítem confirmado no coincidía con el resaltado con ítems ocultos por modo → el
  índice es ahora ABSOLUTO (coherente en navegar, confirmar y pintar).
- **G3** `signalBandwidth` (62.5) quedaba inalcanzable (truncaba a "62") → se escribe el float
  con un decimal exacto.
- **M2** ACT_SLEEP** llamaba a `powerSleepNow()` (con guarda de USB, como la OLED); **M3**
  ACT_SET_COORDS avisa "No soportado en tinta".
- Verificado bien: TODAS las claves de kMenu coinciden con config.cpp; índices de sección
  coherentes; mapa de botones correcto.

### Perfiles de uso + editor (2026-09-15, commits 5c53129 y d46ed0d)
- **Modelo de datos** (`config.h/cpp`): 4 perfiles (0 Fijo/Digi, 1 Peatón, 2 Bici, 3 Coche),
  cada uno con **SSID + tiempo lento + tiempo rápido + metros**. Defaults: Peatón `-7`, Bici
  `-8` (para no chocar con el EA2KR-9 existente), Coche `-5`, Fijo = indicativo del nodo.
- **Regla anti-duplicado**: config valida que los perfiles 1..3 no repitan SSID.
- **Lógica TX** (`aprs.cpp`/`tracker.cpp`): la baliza de RASTREADOR usa el SSID del perfil
  activo (`profileTxSource()`), creando track separado en los mapas, SIN tocar el modo de
  trabajo ni el indicativo del digi. Tiempos y metros por perfil ajustables (0 = default
  comunitario).
- **Menú de la tinta** (`epaper_techo.cpp`): checkbox ">" en los ítems de ELECCIÓN (enum/bool)
  y un **editor de perfiles** (estado dedicado): elegir perfil activo (el que está activo se
  marca con ">") y "Editar ajustes" para cambiar su SSID/tiempos/metros. Grabado pendiente
  (el nodo no estaba conectado al commitár).
- **Pendiente**: configurador web (perfiles+SSID) y el menú de la OLED (`display.cpp`).
  > **ACTUALIZADO EL 2026-09-15 (noche)**: la parte del **configurador web YA ESTÁ HECHA**
  > (commit `b6bc621`: editor de los 4 perfiles con topes y anti-duplicado de SSID). Lo que
  > sigue pendiente es **solo el menú de la OLED**.

---

## 2026-09-15 (tarde) — Icono por perfil, "Fijar coords" en la tinta y aviso de la configuración

> Lo de arriba es la bitácora de cuando se portó el menú. Esto es lo que se hizo DESPUÉS,
> en la misma sesión, y conviene que esté aquí porque cambia piezas de la lista anterior.

### Icono del mapa por perfil de uso
- **Dato**: `config.h` estrena `profileSymbol[4][2]` + `profileOverlay[4][2]` (por defecto
  `#`, `[`, `b`, `>` con tabla `/`: repetidor, persona, bici, coche). Los códigos están
  comprobados contra la tabla de WA8LMF (la que usa go-aprs) y los apuntes de overlays de
  aprs.org; el porqué está escrito en `config.h`.
- **TX** (`aprs.cpp`): nueva `aprsProfileIcon()`, aplicada en la baliza de RASTREADOR (normal
  y comprimida) y en la baliza FIJA, incluida la tabla que va DENTRO del campo de posición.
  **Precedencia**: si el ajuste global `symbol`/`overlay` no está en su valor de fábrica
  (`#` y `/`), manda el usuario y el icono del perfil no se usa.
- **Menús**: en la tinta el editor de perfiles pasa de **4 a 5 campos** (`kPerfCampos`), con
  el nuevo campo **Icono**. **NO se añadió ninguna fila a `kMenu[]`**, así que los arrays de
  índice absoluto (`kMenuSectionFirst` / `kMenuSectionEnd`) **no se han tocado**: es la
  trampa que ya se rompió una vez en silencio. En la OLED se añadió la fila
  **«Icono del perfil»** con `ACT_PROFILE_ICON` (ese menú indexa por filas visibles, así que
  insertar una fila no desplaza nada).
- **Config**: `profiles[i].symbol` (el par de dos caracteres) se valida en `configFromJson()`
  y se publica en la lectura de vuelta.

### "Fijar coords" en el T-Echo (antes: "No soportado en tinta")
- `displaySaveCoords()` guarda de verdad: valida rango, valida las DOS claves con
  `cliTypedSet` y solo entonces llama a `storeSave()` **una vez** (nunca deja una latitud
  nueva con una longitud vieja).
- **La sesión la gobierna el rastreador** (`trackerSetCoordsStart/Tick`), y desde el
  2026-09-15 **la sesión manda sobre el modo de trabajo**: `gpsManage()` la atiende POR
  DELANTE del modo, así que **funciona en modo repetidor sin activar "GPS en repetidor"**
  (antes el `return` de `!needed` apagaba el módulo y ni leía el NMEA: la captura se quedaba
  en "Buscando GPS..." para siempre). **Sin tope de tiempo** (decisión del operador) y con
  **cancelación a mano** (`trackerSetCoordsCancel()`) desde la propia pantalla.
- **Pantalla de la tinta**: `pintaSesionCoords()` manda sobre menú y carrusel mientras dura
  (satélites a la vista → `Asentando...` con barra y `Muestra n/N` → `GUARDADO` con lat/lon).
  Entra en la huella del contenido y solo repinta por escalones; los avisos "GPS n/N" del
  bucle se descartan a propósito en `displayPopup()` para no gastar 20 refrescos de 1,5 s.
- **OLED**: `displayCoordsEstadoTick()` renueva el aviso mientras dura (antes se quedaba
  muda a los 1,5 s y durante minutos no decía nada).

### Aviso de la configuración (lo que se ignora, se dice)
- `configFromJson()` ya no descarta en silencio una clave CONOCIDA con un valor de tipo
  incorrecto: el nombre de la clave va a `errMsg` (**`ignorado (tipo incorrecto): ...`**) y a
  `flogLine()`, y el resto de la configuración sí se aplica. Todo con comprobaciones de
  **solo lectura** (`is<JsonObjectConst>()`, `is<JsonArrayConst>()`): las mutables dan
  siempre false en un `JsonVariantConst`, que es la trampa que tuvo los perfiles muertos.
- Se rechazan además un `symbol` que no sea ASCII imprimible y un icono de perfil con tabla
  o código no representables.

### Pantallas de lista: el indicativo ya no se corta
- `pintaUltimosRX`: el `%.6s` recortaba el indicativo (un `EA2KR-3` perdía el SSID entero:
  se veía "EA2KR-"). Ahora `%-12.11s` + columna de RSSI/SNR alineada, con el peor caso real
  (`EA2MKR-15`) calculado en el comentario.
- `pintaEstaciones`: no recortaba, pero el peor caso **no cabía** (17 caracteres × 12 px a
  escala 2 = 204 px sobre 200). Ahora indicativo con ancho y distancia en km enteros, 192 px
  con el peor caso. `pintaUltimosTX` no tiene el problema (solo pinta etiquetas cortas).

---

## P.14 Segunda mitad de la noche del 2026-09-15: el TNC en palabra y RESTAR en los ajustes

> Commit `9476002` (`src/epaper_techo.cpp` y `src/ax25.h`). Compilados los **cuatro entornos,
> SUCCESS**; **nada probado en hardware** (no había placa). Ojo: **P.12 dice que las filas de la
> lista llevan «el valor a la derecha» y eso YA NO ES ASÍ** — el rediseño posterior dejó **solo la
> etiqueta** en la fila, y para ver o cambiar el valor hay que **entrar** en el ítem. Corregido aquí.

- **La fila del APRS dice la PALABRA del protocolo USB** (`OFF` / `TNC2` / `KISS`), con
  `tncProtocoloPalabra()`: traduce **por valor** (no por índice; un número que no sea 0/1/2
  contesta `?`, no una palabra falsa) y **lee la configuración viva** (`gCfg`), así que si lo
  cambia el configurador web la fila lo dice **sin reiniciar**. Es el **único** ítem del menú con
  valor en la fila, y cabe: 9 caracteres a escala 2 = **122 px de los 200** del panel (medido con
  la fuente del propio fichero).
- ★ **El «TNC: 2» que se creía un defecto NO EXISTÍA**: la fila **no pintaba ningún valor**
  [VERIFICADO por tres vías: el código de hoy, el de antes del commit (`git show 9476002^`) y el
  historial del fichero, que no contiene ese texto]. No hay nada que arreglar ahí.
- **RESTAR en los ajustes numéricos**: gesto nuevo = **doble toque FÍSICO mientras se edita**.
  Antes el editor solo sumaba (el capacitivo llama a `editaSiguiente()`), así que un ajuste que
  empieza en 0 (Latitud, Longitud, Corr sonda, Ajuste chip) **no podía bajar a negativo** desde el
  aparato. Por qué ese gesto: es **el único que `main.cpp` entrega solo cuando el menú está
  editando** (`if (menuIsEditing()) menuEditCancel();`), y **no quita ninguna capacidad** porque
  salir de la edición sin guardar sigue en el toque **largo** (`menuLong`, mismo efecto). El corto
  y el capacitivo no se tocan. Topes: los mismos `it.min/it.max` de `kMenu` que el sumar, con su
  misma regla de rueda; los ítems sin límites (Latitud/Longitud) se validan en `configFromJson`.
- **Solo en los NUMÉRICOS**: en texto y path el doble toque **sigue cancelando** la edición
  (restar texto no tiene sentido). Lo filtra `menuEditCancel()`.
- ★ **CONFLICTO CONOCIDO, y se dice en la pantalla**: el toque corto tarda **hasta 800 ms** en
  resolverse (`button.cpp` espera a ver si es doble), así que **dos toques seguidos para confirmar
  cuentan como doble toque y restan** en vez de confirmar. **No se pierde nada** (se sigue editando
  y se sale con el largo), y la pantalla de edición avisa del gesto con
  **«capacitivo suma, doble resta»**, solo en los campos numéricos. Alternativa descartada: mover el
  restar al toque largo, porque el largo dejaría de ser «volver» justo donde más se usa.
- ★ **ARREGLO DE UN FALLO QUE YA EXISTÍA**: el número grande de la pantalla de edición
  (`gEditBuf`, el `drawTextCenter(70, gEditBuf, 3)` de `menuPinta`) **solo se rellenaba al ENTRAR**
  en el ítem; en los números quien guarda es `menuSave()` directamente en la configuración y
  **nadie volvía a escribir el buffer**, así que la pantalla enseñaba el valor de cuando entraste
  **aunque la configuración sí cambiaba**. Ahora `refrescaBufferNumerico()` lo rellena **desde la
  misma copia que se acaba de guardar**, después de sumar y después de restar: pantalla y
  configuración no pueden discrepar (y si el guardado fue rechazado, enseña el valor que de verdad
  hay). El texto tiene su propio buffer (la rueda) y no se toca.
- `src/ax25.h`: el comentario con la latitud real pasa a **`3000.00S`**, con el aviso de que en
  todos los ejemplos se usan coordenadas de relleno. Comprobado en el código que
  **`aprsCanBeacon()` descarta `lat==0 && lon==0`**, así que el relleno `0000.00N` era **mal
  ejemplo**.
