# T-ECHO PROJECT BUTTER — los toques del T-Echo y del T-Echo Plus

Fecha: 2026-09-15 · Placas: **LilyGO T-Echo** y **T-Echo Plus** (las dos, misma pantalla de
tinta y mismos dos botones) · Entornos: `techo_s140v7`, `techo_plus_s140v7` (y los dos
Faketec, que comparten el mismo `button.cpp`).

Encargo del operador, con sus palabras:

> «Analizar el comportamiento de cualquier toque que se realice en botones físico o
> capacitivo (…) La finalidad es analizar y solventar la latencia enorme y la cantidad de
> toques reales que se convierten en toques fantasma nunca obedecidos (…) para cualquiera
> de las dos T-Echo (…) que deje todo bien fluidito.»

Este documento es el resultado: **el diagnóstico con números**, **qué se ha cambiado**,
**qué números salen ahora** y **qué tiene que probar el operador**. Todo lo que se afirma
aquí es «el código hace esto» o «el cálculo dice esto»; no hay ninguna frase del tipo «se
siente mejor», porque no ha habido ninguna placa conectada.

---

## 1. Diagnóstico: por qué los toques no responden y por qué se pierden

### 1.1 La causa nº 1 (la gorda): el toque se perdía ENTERO mientras el panel pintaba

El botón se leía **por nivel**, una vez por vuelta del bucle:

```cpp
// button.cpp (ANTES), línea 47
const bool sample = (digitalRead(BUTTON_PIN) == LOW);
if (sample != gLastSample && now - gLastChangeMs > kDebounceMs) { ... }
```

Y el bucle se pasa **1,5-3 s seguidos** dentro del driver de la tinta, esperando al panel:

```cpp
// epaper_techo.cpp (ANTES), epdEsperaPintado()
if (gastado < minimoMs) delay(minimoMs - gastado);   // 350 ms (parcial) o 2.000 ms (completo)
```

**Un toque que empieza y acaba dentro de ese `delay()` no deja rastro**: al volver el
bucle, el pin está en el MISMO nivel que antes de pintar, así que `sample != gLastSample`
es falso y no hay nada que procesar. No es cosa del antirrebote ni de la ventana: **es que
nadie miraba el pin**.

Esto ya estaba detectado a medias en `main.cpp`: existe `drainButton()`, que se queda 12 ms
en espera activa llamando al botón seis veces por vuelta (72 ms de reloj por vuelta), con
este comentario:

> «Llamando aquí a buttonPoll() en ráfaga, los flancos que llegaron durante el atasco se
> procesan en cuanto el bucle vuelve: el toque no se pierde.»

**Es falso**, y se demuestra en el banco de pruebas (escenario 1): leyendo niveles, un toque
de 80 ms dentro de un refresco de 1.500 ms no produce **ninguna** acción. Solo se
recuperaba si el dedo seguía puesto al volver el bucle.

Cuánto se pierde, en números: con tráfico APRS el panel puede estar pintando una parte
grande del tiempo (cada RX/TX son **dos** refrescos, uno al entrar el aviso y otro al
caducar, ver la nota de `kAvisoMs`), y cada refresco parcial son 0,35-1,5 s con el bucle
ciego. De ahí la sensación de «toco y no hace nada» y de «toques reales que nunca se
obedecen»: **no eran fantasma, eran toques perdidos**.

Lo mismo vale para las transmisiones: `radioSendFrame()` bloquea cientos de ms
(hasta ~2 s con SF12) y en ese rato nadie miraba el botón.

### 1.2 La causa nº 2: la ventana del doble toque estaba mal medida (y partía el gesto)

```cpp
// button.cpp (ANTES), línea 70
if (gTaps > 0 && now - gLastTapMs <= kClickWindowMs) { gTaps = 0; return BTN_DOUBLE; }
```

`gLastTapMs` se escribía **al soltar** (línea 75) y se comparaba **al soltar**: o sea que el
operador tenía que **empezar y acabar** el segundo toque dentro de los 800 ms contados desde
la suelta del primero. Para un toque de 90 ms eso deja **710 ms reales para empezar** el
segundo. El comentario decía «el margen se mide de SUELTA a PULSACION», pero el código hacía
otra cosa: comentario y código no decían lo mismo.

Y había algo peor, que es el «toque fantasma» de verdad:

```cpp
// button.cpp (ANTES), línea 87: resolver el corto NO miraba si el botón estaba pulsado
if (gTaps == 1 && now - gLastTapMs > kClickWindowMs) { gTaps = 0; return BTN_SHORT; }
```

Con un doble toque lento (segundo toque a los 780 ms), **el corto saltaba a los 800 ms con
el dedo YA puesto** en el segundo toque: una acción que el operador no había pedido, en
mitad de un gesto. El banco de pruebas lo mide (escenario 4): el código anterior da
`CORTO t=1881` **dentro** de la segunda pulsación (1780-1950) y otro `CORTO t=2751` después.

### 1.3 La causa nº 3: el táctil capacitivo no tenía ningún filtro

```cpp
// main.cpp (ANTES), líneas 71-84
static bool gCapLast = false;
const bool cap = (digitalRead(PIN_BTN_TOUCH) == LOW);
if (cap && !gCapLast) { ...accion... }   // cualquier flanco = una acción
gCapLast = cap;
```

Sin antirrebote, sin tiempo mínimo de contacto y sin bloqueo. Y en esta placa la pastilla
táctil **se dispara con el RF de la propia emisora**: es un fenómeno conocido y documentado
por el autor del firmware de referencia de la T-Echo, que por eso **ignora el táctil
mientras está transmitiendo** («The transmitter interferes with the touch button»). El banco
lo mide: **5 acciones** con un roce de 30 ms seguido de un toque real (escenario 7), y **1
acción fantasma** cuando la pastilla se va a bajo por el RF de nuestra transmisión
(escenario 6).

### 1.4 La causa nº 4: 400 ms de aplazamiento que solo eran latencia

```cpp
// epaper_techo.cpp (ANTES), línea 4559
if (toqueReciente && !gMenuEditing && gDirty) return;   // espera a que dejes de tocar
```

Se puso para agrupar repintados cuando se toca rápido con el táctil (que puede ir a
~150 ms por toque). Pero **el botón físico lo pagaba igual**, y no puede tocar rápido: su
toque corto solo existe cuando han pasado 800 ms desde la suelta, así que dos cortos nunca
caen dentro de esos 400 ms. Resultado: **400 ms de retraso añadidos a cada cambio de
diapositiva** pedido con el botón, sin agrupar nada.

### 1.5 El presupuesto de latencia, ANTES (toque corto con el botón físico)

| # | Etapa | Cuánto | Dónde |
|---|-------|--------|-------|
| 1 | Muestreo del pin | hasta 77 ms (o **∞** si el bucle está pintando) | `main.cpp` drainButton 6×12 ms + delay(5) |
| 2 | Antirrebote | 25 ms | `button.cpp:16` |
| 3 | **Ventana del toque corto** | **800 ms** desde la suelta | `button.cpp:31,87` |
| 4 | Aplazamiento del repintado | **400 ms** | `epaper_techo.cpp:1061,4559` |
| 5 | Refresco del panel | **0,35-1,5 s** (parcial) / **2,0-3,0 s** (completo) | `epaper_techo.cpp:699-703` |

- **Lógica de los toques: 1.225 ms** (etapas 2+3+4). El panel: 0,35-3 s.
- Total sin refresco completo: **~1,6-2,7 s** desde el dedo hasta ver el cambio.
- Y si el toque cae mientras el panel pinta: **el toque se pierde** (no hay total).

Reparto: **la pantalla es la mayoría del tiempo, pero la lógica sumaba 1,2 s que se podían
quitar sin tocar el panel.** Con el panel ocupado, además, el toque no se atendía nunca.

---

## 2. Qué hace el firmware del alemán (y qué hace CA2RXU)

El firmware de referencia de esta placa está en el repositorio del proyecto, en
`firm_ref_techo/t-echo-lora-aprs-main/` (**cfr34k/t-echo-lora-aprs**, de Thomas Kolb;
nRF5-SDK, C). Es *la* referencia para la T-Echo: usa **los mismos dos botones y la misma
pantalla** GDEH0154D67/SSD1681 de 200×200.

### 2.1 Los botones: por INTERRUPCIÓN, y con cola

`src/buttons.c`:

```c
static app_button_cfg_t btn_config[NBUTTONS] = {
	{PIN_BTN_TOUCH, APP_BUTTON_ACTIVE_LOW, NRF_GPIO_PIN_NOPULL, cb_app_button},
	{PIN_BUTTON_1,  APP_BUTTON_ACTIVE_LOW, NRF_GPIO_PIN_NOPULL, cb_app_button}
};
...
VERIFY_SUCCESS(app_button_init(btn_config, NBUTTONS, 50));       // 50 ms de antirrebote
APP_ERROR_CHECK(app_timer_start(m_longpress_timer, APP_TIMER_TICKS(1000), NULL));  // 1 s
```

- Usa el **`app_button` de Nordic**, que es **interrupt-driven (GPIOTE)**: el flanco lo coge
  el hardware y arranca un muestreo por temporizador de interrupción cada 25 ms
  (`m_detection_delay/2`) que hace el antirrebote por ESTABILIDAD (2 muestras seguidas).
  **El muestreo NO depende del bucle principal**: por eso a él no se le pierde un toque
  aunque la pantalla esté pintando.
- **No tiene doble toque** (por eso no tiene ventana que esperar): los gestos son
  **corto = pulsar** (actúa en el flanco de bajada) y **largo = 1.000 ms**.
- El antirrebote es 50 ms (nosotros 25 ms) y el largo 1.000 ms (nosotros 600 ms).

### 2.2 La pantalla: NO bloquea

`src/epaper.c` es una máquina de estados con `app_timer` + SPI por EasyDMA: `epaper_update()`
**vuelve enseguida** y el refresco sigue por su cuenta; `epaper_is_busy()` dice si hay uno en
marcha. El bucle principal solo pinta cuando el panel está libre:

```c
// src/main.c, bucle principal
if(m_epaper_update_requested && !epaper_is_busy()) {
	m_epaper_update_requested = false;
	redraw_display(m_epaper_force_full_refresh);
}
```

Y el callback de los botones (`cb_buttons`) **no pinta nada**: cambia el estado lógico y
levanta una bandera (`m_epaper_update_requested = true`). Es exactamente el patrón «encolar
el toque y pintar cuando el panel esté libre»; el nuestro ahora hace lo mismo, adaptado.

También enciende la **retroiluminación 3 s en CUALQUIER evento de botón** (nuestro
`displayBacklightKick()`, 5 s) y **descarta el táctil mientras transmite** (`if(!m_lora_tx_busy)`).

### 2.3 CA2RXU (la referencia del ecosistema)

Comprobado en sus repositorios públicos (autor renombrado a **richonguzman**; `ca2rxu/*` ya
no existe): **no tiene soporte de T-Echo en absoluto**. Los dos proyectos son solo ESP32
(`platform = espressif32`), no hay variante nRF52, no hay pin táctil y el **iGate no lee
ningún botón** (en todo su `src/` solo aparecen llamadas al PMU). Su rastreador usa
`OneButton` **por sondeo** (`userButton.tick()` en `loop()`) con los valores por defecto de
la librería (antirrebote 50 ms, clic 400 ms, largo 800 ms) y **el clic simple se retrasa
400 ms** para poder distinguirlo del doble. Si el bucle está pintando, ese `tick()` no corre
y el toque se pierde: **no tiene nada equivalente a la solución del alemán**. O sea: para la
T-Echo, la referencia buena es cfr34k, y su idea clave (interrupción + no bloquear) es la
que se ha adoptado aquí.

### 2.4 Qué tenemos mejor que ellos

- **El doble toque** (ellos no lo tienen) y el mapeo de gestos del menú.
- **El aviso inmediato** al pulsar: luz + pitido + **vibración** (el Plus). El de referencia
  solo enciende la luz.
- **El antirrebote de 25 ms** (ellos 50 ms) y el largo de 600 ms (ellos 1.000 ms), que en un
  menú de varios niveles se agradece.
- **El filtro del táctil** es más completo que el suyo: él descarta el táctil durante el TX
  a mano; aquí además se exige estabilidad de 40 ms y hay bloqueo de 250 ms.

---

## 3. Qué se ha cambiado (y por qué)

### 3.1 `src/button.cpp` — captura por interrupción + buzón con marcas de tiempo

- **`attachInterrupt(..., CHANGE)`** en el botón físico y en el táctil (GPIOTE del nRF52840),
  instalados en `buttonInit()` (líneas 295-343). El ISR solo hace `digitalRead` + `millis()`
  y guarda el flanco en un **buzón de 16 huecos** (líneas 86-115). A partir de aquí **el
  repintado ya no puede tragarse un toque**, haga lo que haga el bucle.
- La máquina de gestos se alimenta de **esas marcas de tiempo**, no del `digitalRead` de
  turno: un toque ocurrido durante el refresco se resuelve con SUS tiempos.
- **Antirrebote por estabilidad** (el nivel debe aguantar 25 ms), no un simple «no aceptes
  cambios antes de 25 ms»: así no se come un toque rápido de 60-90 ms.
- **Tabla de plazos coherente** (publicada en `button.h`, líneas 13-25):

  | gesto | antes | ahora |
  |---|---|---|
  | antirrebote | 25 ms | 25 ms |
  | LARGO (salta mientras se mantiene) | 600 ms | 600 ms |
  | ventana del doble | 800 ms **de suelta a suelta** | **600 ms de suelta a PULSACIÓN** |
  | el CORTO se resuelve | a los 800 ms de soltar | a los 600 ms de soltar |

  Un solo plazo para las dos cosas, a propósito: así la tabla **no puede contradecirse**.
  Y el doble se decide **en la pulsación** del segundo toque (no al soltarlo), como hacen los
  ratones y `OneButton`.
- **El corto no se resuelve nunca con el botón pulsado** ni con un flanco sin confirmar
  (líneas 258-263): era el fallo que partía el doble toque en dos acciones.
- **Táctil endurecido** (líneas 126-131 y 216-233): estabilidad mínima **40 ms**, **bloqueo de
  250 ms** tras cada toque aceptado, descarte si la confirmación llega más de 250 ms tarde, y
  **descarte si el flanco cae dentro de una transmisión de radio** (`buttonNoteRadioTx`).
  Un toque = **una** acción.
- El botón **físico nunca se filtra** por radio: si el operador pulsa, es él.

### 3.2 `src/radio.cpp` — se apunta la ventana de transmisión

`radioSendFrame()` (líneas 123-131) llama a `buttonNoteRadioTx(inicio, fin)` al terminar de
emitir. Es lo que permite distinguir «un dedo» de «el RF de casa».

### 3.3 `src/epaper_techo.cpp` — el driver BOMBEA el botón mientras espera al panel

- Nuevo gancho `displaySetPumpBoton()` (línea 1070) y `ePDBombea()` (línea 427), llamados:
  - en `epdEsperaBusy()` (línea 619),
  - en **`epdEsperaPintado()`** (línea 645), donde antes había un `delay()` de 350-2.000 ms
    de un tirón; ahora se espera lo mismo en trozos de 2 ms,
  - en la espera de 200 ms del encendido de tensiones (línea 707),
  - y entre comandos del panel (línea 489), que parte también el trasvase de los dos planos.
- El aplazamiento del repintado pasa a ser **solo del táctil** (`kAgrupaToquesMs`, línea 1114):
  el botón físico ya no espera 400 ms de más. `displayNextScene(bool porToque)` (línea 3301).
- Lo que **no** se ha tocado: el panel, sus tiempos, el completo/parcial, la huella de
  contenido, el carrusel, el menú, la confirmación de 15 s y la sesión «Fijar coords».

### 3.4 `src/main.cpp` — cola de gestos + aviso inmediato

- **`bombearBoton()`** (línea 87): captura, resuelve y **encola**. Es lo que llama el driver
  de la tinta desde sus esperas. **No ejecuta acciones** (no se puede entrar a pintar desde
  dentro de un pintado).
- **`handleButton()`** (línea 185) es la única puerta: bombea y ejecuta la cola. Se llama
  además **justo después de `displayRefresh()`** (línea 538), que es donde se cobra el toque
  que llegó durante el repintado.
- **`feedbackPulsacion()`** (línea 99): luz + pitido **en el flanco de pulsación** (~25 ms
  después de poner el dedo), sin esperar a saber si el gesto es corto, largo o doble. Es lo
  que hace que «se note» al instante aunque la acción tarde.
- **`drainButton()` ya no espera** (línea 206): eran 6 × 12 ms = **72 ms de espera activa por
  vuelta** para nada (los flancos ya los coge la interrupción). Eso también da más aire a la
  radio, al GPS y al propio botón (y gasta menos batería).

---

## 4. Los números: ANTES y AHORA

Medidos con el **banco de pruebas** (`tools/banco_boton/`), que compila el `button.cpp` **de
verdad** (y su versión anterior sacada de git) en el ordenador, con reloj y pines simulados.
Salida guardada en `tools/banco_boton/salida_banco.txt`.

| Escenario | ANTES | AHORA |
|---|---|---|
| 1. Toque corto **dentro de un refresco** de 1,5 s | **PERDIDO** (ninguna acción) | **CORTO** ejecutado al acabar el refresco (t=2500), aviso en t=1226 |
| 2. Toque corto en reposo | CORTO a los **881 ms** del dedo | CORTO a los **681 ms**, aviso a los 25 ms |
| 3. Doble toque cómodo (2º a 370 ms) | DOBLE en t=1530 | DOBLE en t=1475 (decidido en la pulsación) |
| 4. Doble toque lento (2º a 700 ms) | CORTO en t=1881 **con el dedo ya puesto** + CORTO en t=2751 | CORTO en t=1681 (antes de que empiece el 2º) + CORTO en t=2551 |
| 5. **Pulsación larga dentro de un refresco** | **PERDIDA** | LARGO en t=2500, aviso en t=1226 |
| 6. Táctil disparado por **nuestro RF** | **1 acción fantasma** | **0** |
| 7. Táctil con **ruido** + un toque real | **5 acciones** | **1** (40 ms después del dedo) |
| 8. Botón **físico durante una transmisión** | **PERDIDO** | CORTO en t=3681, aviso en t=3650 |

### Presupuesto AHORA (toque corto, botón físico)

| # | Etapa | Cuánto |
|---|-------|--------|
| 1 | Captura del flanco | **0 ms** (lo coge la interrupción) |
| 2 | Aviso inmediato (luz + pitido) | **25 ms** |
| 3 | Antirrebote + ventana del corto | 25 + 600 = **625 ms** |
| 4 | Aplazamiento del repintado | **0 ms** |
| 5 | Refresco del panel | 0,35-1,5 s (parcial) / 2,0-3,0 s (completo) — **igual que antes** |

- **Lógica de los toques: 1.225 ms → 625 ms (−49 %).**
- Y un toque que caiga mientras el panel pinta **ya no se pierde**: se ejecuta en cuanto el
  panel queda libre (medido: toque a las 1200 ms dentro de un refresco que acaba a las 2500 →
  acción a las 2500).
- La pantalla sigue siendo la mayor parte del tiempo total y **no se puede reducir** (es
  tinta electrónica: no hay forma de pintar en 100 ms). Lo que se ha hecho es que la lógica
  no sume y que el toque se note al instante por otros medios (luz, pitido, vibración).

---

## 5. Cómo se comprueba

```powershell
cd _trabajo_ea2oy
$env:PLATFORMIO_CORE_DIR="<RUTA_DEL_PROYECTO>\_trabajo_ea2oy\_pio_core"
& "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" `
   run -e techo_plus_s140v7 -e techo_s140v7 -e faketec_sx1262_433 -e faketec_e22p_433
```

Los cuatro entornos dan **SUCCESS** (los dos T-Echo son los que importan).

El banco de pruebas del botón (no hace falta placa):

```powershell
cd _trabajo_ea2oy\tools\banco_boton
.\ejecuta_banco.ps1        # necesita zig, g++ o clang++ (ver la cabecera del script)
```

---

## 6. Qué tiene que probar el operador (en la placa)

1. **Toque corto con la pantalla pintando**: cambia de diapositiva y **toca otra vez
   inmediatamente** varias veces seguidas. Antes: la mayoría se perdían. Ahora: **cada toque
   cambia una diapositiva** (ninguno se pierde), aunque el cambio se vea cuando el panel
   acabe.
2. **Aviso inmediato**: al apoyar el dedo, la **luz se enciende** y el **Plus pita** en el
   acto, antes de que la pantalla cambie. Si eso se nota, la cadena va bien.
3. **Toque largo = menú**: mantener 600 ms. Debe abrir el menú aunque el panel esté pintando
   (antes, si el panel estaba pintando, no abría nada).
4. **Doble toque = baliza**: dos toques con el segundo **empezando** antes de 600 ms tras
   soltar el primero. Debe salir **a la primera** (antes hacía falta que los dos toques
   cupieran enteros en 800 ms). Si le sale mal, el margen es **una sola constante**
   (`BUTTON_CLICK_WINDOW_MS` en `button.h`).
5. **Táctil capacitivo**: un toque = **una** fila de menú / **una** diapositiva. Que no salten
   varias de golpe al rozarlo, y que **no salte solo** cuando el nodo transmite (baliza,
   telemetría, repetir un paquete). Para comprobarlo con el USB: el comando de taller `CAP`
   dice el nivel de P0.11 (en reposo `1/1`; tocando, `0/0`).
6. **Lo que NO debe cambiar**: el menú (navegar/entrar/volver), la sesión «Fijar coords»
   (que se pueda cancelar con un toque y que no se cancele sola al arrancarla), la
   confirmación de 15 s de las acciones destructivas, el carrusel de 45 s y el modo de la
   pantalla.

---

## 7. Lo que NO se ha podido comprobar

- **No había ninguna placa conectada.** No se ha podido medir en el hardware: ni el tiempo
  real de un refresco (`gMsUltimoParcial`), ni si la pastilla táctil de esta unidad concreta
  se comporta igual que la del firmware de referencia, ni si el RF la dispara con la misma
  fuerza. Todo lo del panel son **los tiempos que declara el propio driver** (350 ms/2.000 ms
  de espera mínima + 200 ms de encendido + reset + trasvase bit-bang).
- **El banco de pruebas mide la LOGICA del botón**, no el panel: los 0,35-3 s del refresco se
  suman aparte y son iguales antes y después.
- **La polaridad del táctil**: `pins_techo.h` dice «activo a nivel alto» y el firmware (antes
  y ahora) lo lee **activo a nivel bajo** con resistencia de subida, que es lo que al
  operador le funcionaba. Se ha dejado en BAJO y con nombre propio
  (`kToqueActivo`, `button.cpp:122`) para que cambiarlo sea **una línea** si alguna unidad
  sale al revés. El comando `CAP` por USB sirve para confirmarlo en la placa.
- **Que «se sienta fluidito»**: eso solo lo puede decir el operador. Aquí solo se puede
  afirmar que la lógica ha pasado de 1,2 s a 0,6 s, que los toques durante el repintado ya no
  se pierden y que el táctil pasa de 5 acciones por roce a 1.
