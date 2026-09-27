# Hardware del LilyGO T-Echo y T-Echo Plus (para portar nuestro firmware)

> ⚠️ **ESTO NO ES NORMATIVO. Es material de consulta.**
> Describe lo que estas placas llevan **hoy**, según fuentes de primera mano. Si contradice al
> código o a una medición en la placa, **GANA LA PLACA**. No entra en el orden de lectura obligatorio.

**Fecha**: 2026-09-14 · **Para qué**: preparar los entornos `techo` y `techo_plus` de nuestro
firmware Kacho System, y hacer la copia de seguridad antes de grabar nada.

## 0. De dónde sale cada dato (y una advertencia)

Fuentes de primera mano, tres, y las tres coinciden:

1. **`pinout.h` de cfr34k** (`t-echo-lora-aprs`) — firmware **que funciona en ese hardware**.
   Verificado por nosotros leyendo el fichero.
2. **README oficial de LilyGO** (`Xinyuan-LilyGO/T-Echo`) — tablas de pinout, pantalla, direcciones
   I2C y parámetros eléctricos.
3. **Definiciones de variante de Meshtastic** (`variants/nrf52840/t-echo` y `t-echo-plus`).

> **⚠️ AVISO IMPORTANTE, y está comprobado**: una lista de pines generada por otro asistente de IA
> sobre estas placas tenía **alrededor del 40 % de errores**, y los peores eran los de la pantalla:
> daba como pines de pantalla los **de la radio** (SCK 0.19 / MOSI 0.21), cuando la pantalla va por
> SCLK **0.31** y MOSI **0.29**. También inventaba un acelerómetro LIS3DH, un sensor de luz ALS-PT19
> y un modelo de GPS que no existen. **La moraleja: los pines se copian del `pinout.h` que funciona,
> nunca de una lista recordada.** Lo que sigue está sacado de las tres fuentes de arriba.

## 1. Lo importante primero: las dos placas son CASI la misma

LilyGO lo dice con todas las letras: **«t-echo and t-echo plus use the same pinout»**.
Todas las diferencias están en **accesorios del Plus**, no en la radio, la pantalla, el GPS ni la
memoria. Por eso los dos entornos pueden compartir casi todo.

| | T-Echo | T-Echo Plus |
|---|---|---|
| Batería | 850 mAh | **2400 mAh** |
| IMU | **ninguno** | **BHI260AP** (6 ejes, «smart») |
| Motor háptico | no | **DRV2605** (I2C 0x5A, enable en P0.08) |
| Zumbador | no | **sí** (P0.06) |
| Gancho de escalada | no | sí (carcasa) |
| Todo lo demás | **idéntico** | **idéntico** |

## 2. Radio — SX1262

| Señal | Pin | Nota |
|---|---|---|
| SCK | **P0.19** | SPI0, **no compartido con la pantalla** |
| MOSI | **P0.22** | |
| MISO | **P0.23** | |
| CS | **P0.24** | |
| RST | **P0.25** | |
| BUSY | **P0.17** | |
| DIO1 | **P0.20** | interrupción |
| DIO3 | **P0.21** | tensión del TCXO |
| Conmutador de RF | **no hay pin** | se usa el **DIO2 interno** del SX1262 (`SX126X_DIO2_AS_RF_SWITCH`) |

**No hay pin de RXEN ni de TXEN.** Esto es distinto de nuestras placas Faketec (donde el HT-RA62
usa P0.17 como RXEN y el E22P lo usa para alimentar el módulo). Aquí **P0.17 es BUSY de la radio**.

**Recomendación del fabricante** (LilyGO lo publica): tras fijar la potencia, poner
`radio.setCurrentLimit(80)` (límite de sobrecorriente a 80 mA). **Pendiente comprobar qué hace
nuestro `radio.cpp` con el límite de corriente**: hoy no lo tocamos.

## 3. Pantalla — tinta electrónica (¡no es una OLED!)

| Dato | Valor |
|---|---|
| Modelo | **GDEH0154D67** |
| Controlador | **SSD1681** |
| Tamaño / resolución | 1,54 pulgadas · **200 × 200** píxeles |
| Colores | blanco y negro (**2 niveles**, sin grises intermedios en la práctica) |
| Refresco completo | **2 segundos** |
| Refresco parcial | **0,26 segundos** |
| Táctil | **NO** |

**Pines (bus SPI1, separado del de la radio):**

| Señal | Pin |
|---|---|
| SCLK | **P0.31** |
| MOSI / SDI | **P0.29** |
| CS | **P0.30** |
| DC | **P0.28** |
| RST | **P0.02** |
| BUSY | **P0.03** |
| Alimentación / luz de fondo | **P1.11** |

> **★ CONFIRMADO POR LA PEGATINA DE LA PROPIA PLACA (2026-09-15)**: la unidad de **EA2MKR**
> lleva detrás una etiqueta del fabricante con los pines de la pantalla, y **coincide
> exactamente** con la tabla de arriba: `MISO P1.06 · MOSI P0.29 · SCLK P0.31 · CS P0.30 ·
> DC P0.28 · RST P0.02 · BUSY P0.03 · BL P1.11`. Es una cuarta fuente, de primera mano, y la
> única que **no** viene de documentación ni de firmware ajeno.
> El único pin que no teníamos declarado es **MISO P1.06** (`src/pins_techo.h` no lo define):
> este driver **no lo usa** porque por bit-bang solo escribe. Si algún día se quisiera leer el
> registro de estado del panel (0x2F), haría falta.

**Lo que esto significa para nuestro firmware (importante):**

1. **Nuestra capa de pantalla actual NO sirve aquí.** Está escrita para OLED SSD1306/SH1106 por
   I2C (escenas, menú, popups, cabecera con píldoras…). La e-paper es otro chip, otro bus y otras
   reglas. Es un **porte**, no un ajuste.
2. **Refresco completo = 2 segundos.** Nuestro menú actual se redibuja a cada toque: aquí eso es
   inviable (2 s por pulsación). Hay que diseñar la interfaz para e-paper: refresco parcial (0,26 s)
   y pantallas que cambian poco. Meshtastic limita a **20 refrescos rápidos seguidos** y luego
   obliga a uno completo.
3. **Al apagar hay que soltar los pines** (CS/DC/RST/BUSY como entrada) o hay **fuga de corriente**.
   Lo hace el `variant_shutdown()` de Meshtastic; es una lección aprendida por ellos.
4. **La radio y la pantalla NO comparten bus SPI** (radio en SPI0, pantalla en SPI1), pero **sí
   comparten la alimentación** (ver §4).
5. **Fallo de hardware documentado por Meshtastic**: en **algunas unidades** del T-Echo (no del
   Plus), **transmitir por LoRa dispara el botón táctil** como si alguien lo hubiera pulsado.
   Ellos bloquean el botón durante la transmisión. **Hay que hacer lo mismo o el menú se vuelve loco.**
6. Para la referencia de cómo se dibuja: Meshtastic usa un **fork propio de GxEPD2** y su sistema
   «InkHUD»; cfr34k usa un **driver propio** (`epaper.c`, con doble búfer para refresco parcial) y
   tiene incluso un **simulador de pantalla en SDL** para diseñar las pantallas sin la placa
   (`test/display/`, con capturas ya hechas en `doc/screenshots/`).

## 4. Alimentación (esto es lo que más se diferencia de nuestras Faketec)

Del esquema de LilyGO y del `notes.txt` de cfr34k (en alemán, traducido):

```
VBAT
 ├─→ «5V» ─→ nRF52840 (VDDH)
 └─→ Regulador 3,3 V  ──┬─→ Módulo LoRa          ← SIEMPRE alimentado
                        └─→ MOSFET (PWR_ON) ─┬─→ eInk
                            P0.12 = PWR_EN   ├─→ GPS
                                             ├─→ Flash
                                             ├─→ LEDs
                                             └─→ BME280
```

| Señal | Pin | Qué hace |
|---|---|---|
| **PWR_EN** | **P0.12** | Enciende el MOSFET que da corriente a **eInk, GPS, flash, LEDs y BME280**. **Con este pin en bajo, ninguno de esos cinco responde.** |
| **REG_EN** | **P0.13** | Enciende el **regulador de 3,3 V** (el módulo LoRa cuelga de él). En el Plus, este mismo pin va al DRV2605. |

**Consecuencia práctica y crítica para nuestro firmware**: igual que en las Faketec hay que subir el
rail de 3,3 V, **aquí hay que subir PWR_EN antes de buscar sensores, encender el GPS o pintar la
pantalla**. Y al revés: **bajarlo es lo que de verdad ahorra batería** (el consumo dormido que
publica LilyGO es 0,25 mA).

## 5. GPS — Quectel L76K

| Señal | Pin |
|---|---|
| TX (del GPS) | **P1.08** |
| RX (al GPS) | **P1.09** |
| PPS | **P1.04** |
| Wake up / standby | **P1.02** |
| Reset | **P1.05** |

- **No es un u-blox.** Es **L76K** (Quectel). Habla **NMEA por UART**, así que nuestro `gps.cpp`
  (TinyGPS++ sobre `Serial1`) debería valer, pero **hay que reasignar los pines de `Serial1`**.
- **No hay pin de «encendido» del GPS como en la Faketec** (allí es un MOSFET en P0.24): aquí el GPS
  se alimenta del MOSFET general (P0.12) y además tiene línea de **wake up** y de **reset**.
- cfr34k lo dice en su README: *«GNSS module L76K (other NMEA-via-UART capable modules should also
  work)»*. O sea, nuestro código de parseo NMEA sirve.

## 6. Sensores I2C (direcciones verificadas en la tabla oficial de LilyGO)

Bus I2C: **SDA P0.26 / SCL P0.27**.

| Sensor | Dirección | ¿En qué placa? | ¿Lo usamos hoy? |
|---|---|---|---|
| **BME280** (temperatura, humedad, presión) | **0x77** | **en las dos** | **SÍ**: nuestro `sensors.cpp` ya lo detecta por chip ID (0x60) — pero probamos 0x76/0x77, así que entra solo |
| **BHI260AP** (IMU 6 ejes) | 0x28 | solo Plus | no |
| **DRV2605** (motor háptico) | 0x5A | solo Plus | no |
| **PCF8563** (reloj de tiempo real) | 0x51 | en las dos (IRQ en P0.16) | no |

> **Corrección importante**: **las dos placas llevan BME280**, no BMP280 la estándar. Lo confirman
> el README de LilyGO (que lo escribe con una errata, «BEM280») y cfr34k, que usa `0x77` y
> **comprueba el identificador de chip `0x60`** (el del BME280, no el `0x58` del BMP280).
>
> **★ MEDIDO EN HARDWARE (2026-09-15) — y contradice lo de arriba en esta unidad ★**
> Con el comando `i2cscan` (por USB, `sensorsI2cScan()`), en la unidad **T-Echo Plus de EA2KR**:
>
> ```
> I2C: 28 51 5A | n=3 | wx: 76=00 77=00 (58=BMP 60=BME 61=BME680)
> ```
>
> - Contestan **tres** chips: `0x28` (BHI260AP, el IMU), `0x51` (PCF8563, el reloj) y `0x5A`
>   (DRV2605, el motor). Los tres son **exclusivos del Plus**, así que esa unidad **es un Plus**
>   (confirmado por hardware, no por la caja).
> - En `0x76` y `0x77` **no contesta nadie**: los dos devuelven chip ID `0x00`. O sea que en
>   **esa** unidad **no hay sensor meteorológico** (ni BME280, ni BMP280, ni BME680).
> - **El bus I2C funciona** (contesta a tres chips), así que **NO es un problema de pines**:
>   los pines declarados (SDA P0.26 / SCL P0.27) son los correctos.
>
> Queda por saber si es cosa de esa unidad, de un lote, o si las fuentes se equivocan. Se
> comprobará con **el mismo comando** en la unidad de EA2MKR. Mientras tanto, **manda lo que
> dice la placa** (regla 0): en la unidad de EA2KR, el firmware no puede publicar meteorología
> porque no hay sonda, y el status lo refleja (`sens.wx: false`).
> Nuestro driver ya distingue los tres chips por identificador, así que **el BME280 entra sin tocar nada**.

## 7. Batería y carga

| Dato | Valor | Fuente |
|---|---|---|
| Pin del ADC | **P0.04** (AIN2) | Meshtastic |
| Multiplicador del divisor | **×2,0** | `ADC_MULTIPLIER (2.0F)` en las dos variantes de Meshtastic |
| Fondo de escala | ~4,8 V | la fórmula de cfr34k: `raw × 600 × 2 × 4 / 4096` |
| Umbral de despertar (LiPo) | **3100 mV** | tabla de cfr34k |
| Curva de batería | 3000→0 % … 4100→100 % | tabla de cfr34k |
| USB detectado | lectura **> 4200 mV** | aviso del README de LilyGO |

**Esto NO se parece a nuestras Faketec**: allí el ADC está en **P0.31 (AIN7)** con el mismo divisor
0,5. Aquí es **P0.04 (AIN2)**.

> **Aviso del fabricante**: con el USB enchufado, la lectura del ADC **no es fiable** (está
> cargando). Por eso el truco de «>4200 mV = hay USB».

## 8. Botones, LEDs y otras cosas

| Elemento | Pin | Nota |
|---|---|---|
| Botón de usuario | **P1.10** | activo a nivel bajo. **Este es el botón «de verdad»** |
| Botón táctil capacitivo (arriba) | **P0.11** | activo a nivel alto |
| Reset | **P0.18** | **es el pin de reset del chip: no se puede usar para otra cosa** |
| LED azul | **P0.14** | |
| LED rojo | **P1.03** | |
| LED verde | **P1.01** | |
| Zumbador | P0.06 | **solo Plus** |
| DRV2605 enable | P0.08 | **solo Plus** |

> **Discrepancia que dejo escrita**: la definición de LEDs de Meshtastic **se contradice con el
> pinout de LilyGO y con cfr34k** (Meshtastic los pone en 0.13/0.14/0.15). Los otros dos coinciden
> entre sí (**P1.03 rojo, P1.01 verde, P0.14 azul**). Si algún día usamos los LEDs, **se comprueba
> en la placa** antes de darlo por bueno.

## 9. Los sensores que nadie usa: ¿nos sirven? (pregunta del operador)

### 9.1 BME280 — **SÍ, y es regalo**
Temperatura, humedad y presión en las **dos** placas, en 0x77. **Ya tenemos el driver** y ya
distinguimos chips por identificador. Es el mismo sensor que monta nuestra Faketec de banco, así que
la meteorología APRS y la telemetría funcionarían **sin escribir nada nuevo**. Lo único: el sensor
cuelga del MOSFET (P0.12), así que **con PWR_EN en bajo no contesta**.

### 9.2 PCF8563 (reloj de tiempo real) — **SÍ, y es el más valioso de los tres**
Es un RTC de NXP, direcciones conocidas y baratas de leer: **0x51**, con los registros de segundos
(0x02), minutos (0x03), hora (0x04), día (0x05), mes (0x07) y año (0x08) en BCD. Tiene además un
**detector de tensión baja** que avisa de si la hora ya no es de fiar.

- **Por qué nos importa**: hoy nuestro firmware **no manda el paquete meteorológico sin hora del
  GPS** (lo rechaza a propósito, para no inventar un sello falso), y tampoco sella posiciones
  cuando el GPS aún no ha fijado. Con un RTC **se puede arrancar con la hora buena** (puesta por el
  GPS la última vez que fijó, o a mano desde el configurador) y **seguir sellando paquetes** al
  encender antes de tener satélites.
- **Coste**: bajo. Son unas decenas de líneas (leer/escribir BCD por I2C) y no hace falta ninguna
  librería nueva.
- **Aviso de honestidad**: es una **idea mía, pendiente de que el operador la apruebe**, no algo que
  hayamos probado. Y hay que comprobar en la placa si el RTC conserva la hora sin batería (el
  PCF8563 tiene su propia pila o condensador: **NO CONSTA** cómo está montado en estos nodos).

### 9.3 BHI260AP (IMU del Plus) — **NO, en la práctica**
No es un sensor tonto al que se le leen registros: es un **procesador aparte** («Fuser2») que
**necesita que el anfitrión le descargue un firmware** antes de dar un solo dato, y se maneja con la
API **BHY2** de Bosch (blob binario + pila de funciones). Meshtastic lo tiene **desactivado en el
T-Echo Plus**, y el motivo está escrito en su propio fichero de configuración:
*«lewisxhe/SensorLib too big for nrf52»*.

- **¿Para qué nos serviría?** Para saber si el nodo se mueve o está parado (detector de movimiento
  para el rastreo y para despertar la pantalla). Es atractivo, pero:
- **Coste real**: firmware del sensor + API propietaria + memoria. Nuestro binario ya va por el
  **43 % de la flash** y el BHI260AP no cabe con holgura, igual que les pasa a ellos.
- **Veredicto**: **no entra** para mañana, ni probablemente después. Se queda anotado como «no».

### 9.4 DRV2605 y zumbador (solo Plus) — **NO, pero es barato si algún día se quiere**
El zumbador (P0.06) es un simple encendido/apagado: un pitido al mandar baliza o al recibir un
mensaje costaría **tres líneas**. El DRV2605 necesita su driver por I2C (0x5A) y un enable (P0.08).
**Para mañana: no.** Se anota como «fácil si se pide».

## 10. Lo que esto significa para el firmware (resumen honesto)

| Capa de nuestro firmware | ¿Sirve tal cual en el T-Echo? |
|---|---|
| Configuración, persistencia, CLI, protocolo USB | **Sí**, es independiente del hardware |
| Radio (RadioLib SX1262) | **Sí**, cambiando pines y quitando el RXEN (aquí no existe) |
| APRS (balizas, mensajes, digi, telemetría) | **Sí**, no toca hardware |
| Sensores (BME280 / INA219 / AHT20) | **Sí**, el BME280 entra por identificador; el INA219 no existe aquí |
| GPS (TinyGPS++ por UART) | **Sí**, reasignando los pines de `Serial1` |
| Batería y sueño | **A medias**: hay que generalizar el pin del ADC (P0.04, AIN2) y **los umbrales de 3400/3710 mV no valen** para una LiPo de 1 celda |
| **Pantalla** | **NO**. Es un **porte**, no un ajuste: hay que escribir una capa de pantalla nueva para e-paper |
| Botón | **Sí**, pero hay que añadir el bloqueo durante la transmisión (fallo de hardware) |

**Decisión de ingeniería que propongo (pendiente de tu OK)**: para mañana, portar **todo menos la
pantalla**, y que los nodos de tus amigos **funcionen como repetidor y rastreador** con su radio,
su GPS, su BME280 y su configuración por USB. La pantalla de tinta electrónica se merece una sesión
propia, porque no es un ajuste: es diseñar la interfaz otra vez.
