# Uso del aire y normas de la red APRS

Análisis pedido por el operador el 2026-09-12. **No se ha cambiado nada en el
firmware**: esto es una foto de la situación y una comparación con lo que se oye
en la zona y con lo que hace el resto de nodos.

Se puede recalcular cuando se quiera:

```
tools\send.ps1 -Port COM36 -Line "log dump" -WaitMs 30000 | Set-Content .pio\log_completo.txt
node tools\airtime.js .pio\log_completo.txt
```

---

## 1. Cuánto tarda cada trama nuestra (SF12 / ancho 125 / CR 4-5)

Medido con la fórmula de LoRa, contando los 3 bytes de prefijo del ecosistema
(`0x3C 0xFF 0x01`). Ajuste de baja velocidad (LDRO) activado, que es lo normal
en SF12 a 125 kHz; sin él los tiempos bajan ~10 %.

| Tamaño de la trama | Tiempo de aire |
|---|---|
| 40 bytes (mensaje, boletín, WX corto) | **2,1 s** |
| 85 bytes (baliza fija del repetidor) | **3,6 s** |
| 101 bytes (baliza de rastreo típica) | **4,1 s** |
| 130 bytes (baliza con todo el texto) | **5,1 s** |
| 178 bytes | 6,7 s |
| 255 bytes (el máximo que admite el firmware) | **9,2 s** |

Referencia: en APRS clásico (1200 baudios) una trama así ocupa **~0,5 s**. En
LoRa a SF12 ocupa **8 veces más**: ese es el motivo de fondo de todo lo que
viene después.

## 2. Reglas de la red

**APRS-IS** (la red de Internet, según [aprs-is.net](http://www.aprs-is.net/connecting.aspx)):
- Las tramas van en texto TNC2 y **ninguna línea puede pasar de 512 bytes** con CR/LF incluidos.
- Solo pueden enviar clientes con *passcode* válido; los paquetes del cliente deben llevar únicamente `TCPIP*` en la ruta.
- Los constructos `q` (`qAR`, `qAO`) los pone el iGate, no el nodo.
- **No hay un límite numérico publicado de paquetes por minuto**, pero los servidores sí limitan y cortan clientes abusivos (ha habido episodios de saturación y se ha hablado de límites en la lista oficial). La regla real es de sentido común: no destacar.

**Buenas prácticas APRS** (Protocolo APRS 1.0, capítulo 2, de Bob Bruninga):
- **"Net cycle time"**: todas las estaciones deberían balizar dentro del ciclo de la red, que es de **10 minutos** en local, **20** con dos repetidores y **30 minutos** en uso rutinario. Balizar mucho más a menudo no aporta nada y estropea el canal.
- El canal es compartido y de tipo Aloha: cuando se satura, **todo el mundo pierde** paquetes. La doctrina clásica es que un canal de 1200 baudios aguanta unos 100-150 nodos; con LoRa SF12, al ocupar cada trama 8 veces más, la "cuota justa" por nodo es proporcionalmente menor.
- **Duplicados**: la red (y nuestros propios repetidores, con 25 s internos) descarta paquetes idénticos en una ventana corta, en torno a **30 s**. Emitir más a menudo que eso es tirar aire.
- En el ecosistema **LoRa APRS**: los rastreadores comerciales y de referencia balizan cada **1-3 minutos** en movimiento, y los iGates cada **~30 minutos**. Esa es la referencia de "normalidad".
- En 433 MHz (banda de aficionados en España) no hay tope legal de ciclo de trabajo como en los 10 % de los equipos sin licencia, pero el plan de banda pide contención. Como dato: **superar el 10 % de ocupación sería pasarse del criterio de los equipos exentos**.

## 3. Lo que emite nuestro nodo (medido en su propio registro)

Ventana analizada: **2026-09-11 15:34 → 2026-09-12 12:25** (unas 21 horas).
Incluye pruebas mías y el gazapo de balizas fantasma (ya corregido), así que la
situación normal es **mejor** que esta media.

| Tipo de trama | Cuántas | Aire total |
|---|---|---|
| Telemetría (cada 10 min) | 77 | ~165 s |
| Rastreo (baliza GPS) | 75 | ~308 s |
| Meteorología (cada 15 min) | 71 | ~152 s |
| Baliza fija del repetidor | 26 | ~94 s |
| Mensajes | 3 | ~6 s |
| Objetos | 2 | ~8 s |
| **TOTAL** | **254** | **861 s (14,4 min)** |

Por horas: entre el **0,5 % y el 3 % del canal**, con una media de **~0,7 %**.
(El pico del 3 % fue la hora de las pruebas y de las balizas fantasma.)

**Situación por escenarios:**

| Escenario | Tramas/hora | Aire/hora | Ocupación del canal |
|---|---|---|---|
| Reposo con GPS (modo Ambos) | ~10 | ~21 s | **0,6 %** |
| Repetidor solo (modo 0) | ~12 | ~35 s | **1,0 %** |
| Rastreo en coche, parado | ~12 | ~30 s | **0,8 %** (2 son la baliza de parado: ~0,2 %) |
| **Rastreo en coche, en marcha** | **hasta 120** | **~492 s** | **hasta 13,7 %** |

El caso de en marcha es el techo: la configuración actual (perfil coche, 150 m,
tiempo mínimo 30 s) puede llegar a **2 balizas por minuto**, que es exactamente
el límite en el que la red empieza a descartar duplicados. Con 4 s por baliza,
eso es **casi el 14 % del canal** mientras conduces.

## 4. Lo que se oye en tu zona

En esas 21 horas el nodo ha oído **48 tramas, todas de EA2XXX-10** (unas 2,3 por
hora, coherente con un iGate balizando cada ~30 minutos). **No se oye ningún
otro nodo**: el canal de tu zona está prácticamente vacío.

Es decir: hoy **nuestro nodo es el que más habla de la comarca**, pero con
números absolutos minúsculos (12 tramas por hora frente a 2 del iGate). En una
zona así, el uso del aire no es un problema en absoluto.

## 5. ¿Y quién recibe la "sanción": el iGate o yo?

Pregunta del operador: si lo que sube el iGate lleva **su** indicativo, la cuenta y
el posible baneo, ¿no irían contra él? Respuesta: **las dos cosas, pero en capas
distintas**.

**Capa técnica (el corte):** el límite y el corte de conexión los aplica el
servidor sobre el **cliente TCP**, es decir sobre el iGate. Si algo se pasara de
rosca, al que desconectan es a EA2XXX-10, no al nodo. Nuestro nodo no habla con
Internet.

**Capa de atribución (la "anotación"):** ahí sí es todo del indicativo. Cada
paquete viaja como `EA2OY-7>APLRG1,TCPIP*,qAR,EA2XXX-10:...`, así que quedan los
dos: el origen (tú) y la puerta (el iGate). Y **aprs.fi publica un aviso en la
ficha de la estación** según su ritmo de emisión (guía oficial, "Packet rate
advisor"): calcula el **intervalo medio** entre los últimos paquetes (hasta 50,
cortando en el primer hueco de una hora: o sea, mira el "último viaje") y:

| Intervalo medio | Lo que aprs.fi escribe en tu ficha |
|---|---|
| **< 30 s** | *"This station is transmitting packets at a high rate, which can cause congestion in the APRS network."* |
| **< 15 s** | *"This station is transmitting packets at a very high rate, which causes serious congestion in the APRS network. **This could be considered an abuse of the network resources**."* |

Además, la ficha muestra el **ritmo de paquetes** y hay gráficas de **paquetes
por hora** y de posiciones nuevas por hora: el uso del aire queda público con tu
indicativo. También hay un "path advisor" que avisa si la ruta pide más de 3
salts.

**Capa de duplicados:** aprs.fi confirma que APRS-IS **descarta los duplicados
dentro de los 30 segundos** siguientes al primero. Es decir: emitir más a menudo
que eso no llega ni a la base de datos.

**Otras puertas donde sí se puede "vetar" un indicativo**: la lista negra del
propio iGate (el firmware de CA2RXU la tiene) o un filtro de un sysop. Eso ya no
es automático, es decisión de una persona.

### Nuestra cuenta concreta (esto es lo importante)

Con el rastreador actual (perfil coche, 150 m, **mínimo 30 s**) una hora de
conducción serían ~120 balizas + 6 de telemetría + 4 de meteorología = **130
paquetes en 3600 s → intervalo medio 27,7 s**. Es decir: **por debajo del umbral
de 30 s**, así que en un viaje largo lo más probable es que aparezca el aviso
público de "high rate" en la ficha de EA2OY-7 (no el de abuso, ese pide <15 s).

Números según el mínimo que se ponga:

| Mínimo entre balizas | Paquetes/hora | Intervalo medio | ¿Aviso público? |
|---|---|---|---|
| **30 s (actual)** | ~130 | **27,7 s** | **Sí** (el de "high rate") |
| 45 s | ~90 | 40,0 s | No |
| 60 s | ~70 | 51,4 s | No |

**Conclusión:** el riesgo de baneo técnico es del iGate, pero **la etiqueta
pública es tuya**, y con el mínimo de 30 s estamos justo por debajo del primer
umbral de aviso de aprs.fi. Con 45 s se sale de la zona con muy poca pérdida de
detalle de ruta. Decisión del operador: no se ha tocado nada.

## 6. Rutas y saltos (cuántos pedir y qué cuestan)

El campo de ruta ("path") es lo único que decide **si los repetidores repiten lo
nuestro**. El modo de trabajo (repetidor/rastreador/ambos) no influye: solo
cambia lo que hacemos *nosotros* con los paquetes de los demás.

Rutas admitidas por el firmware: `0`, `WIDE1-1`, `WIDE1-1,WIDE2-1`,
`WIDE1-1,WIDE2-2`, `WIDE2-1`, `WIDE2-2`, `RFONLY` (esta última no es un salto,
pide que no se suba a Internet).

**Lo que dicen las guías APRS** (p. ej. las de Alabama/N8DEU, que recogen el
consenso del "New-N paradigm"):

| Estación | Saltos | Ruta recomendada |
|---|---|---|
| Móvil/rastreador | 1 | `WIDE1-1` |
| Móvil/rastreador | 2 | **`WIDE1-1,WIDE2-1`** (la habitual) o `WIDE2-2` |
| Móvil/rastreador | 3+ | **Nunca**: no se recomiendan más de 2 saltos |
| Fija | 1 | `WIDE2-1` (o `WIDE1-1` si no alcanzas un WIDEN-n) |
| Fija | 2 | `WIDE2-2` |
| Fija | 3+ | Nunca (usar una ruta concreta con indicativos) |

Y un dato que lo resume: *"el objetivo de cualquier estación es alcanzar un
iGate, y la mayoría de los iGates se alcanzan con 2 saltos o menos"*.

**Cuánto cuesta cada salto** (LoRa SF12, baliza de ~101 bytes = 4,1 s):

| Ruta | Emisiones por baliza | Aire por baliza | Ocupación con 30 s | Ocupación con 90 s |
|---|---|---|---|---|
| `0` | 1 (la nuestra) | 4,1 s | 13,7 % | 4,6 % |
| `WIDE1-1` | 2 (nosotros + 1 digi) | 8,2 s | 27,3 % | 9,1 % |
| `WIDE1-1,WIDE2-1` | 3 (nosotros + 2 digis) | 12,3 s | 41,0 % | **13,7 %** |

De ahí la regla práctica: **cada salto que pidas, multiplica el tiempo mínimo
entre balizas**. Dos saltos con 30 s de separación ocuparían el 41 % del canal
(una barbaridad); con 90 s vuelves al 13,7 % de hoy.

**Recomendación**:
- **En casa, con iGate a la vista: `0`.** La mitad de aire y mismo resultado.
- **De ruta con cobertura: `0` o `WIDE1-1`.**
- **De ruta por zonas sin iGate: `WIDE1-1,WIDE2-1`** (el caso típico: un iGate
  lejano que solo lo oye un repetidor) **y subir el mínimo a 60-90 s** para
  compensar el aire extra.
- **Nunca 3 o más saltos.**

### Ruta por modo: tres campos separados (decisión del operador, 2026-09-12)

El firmware y el configurador tienen **tres campos de saltos**, uno por modo de
trabajo, cada uno configurable por separado y con su valor recomendado:

| Modo de trabajo | Campo | Valor recomendado | Saltos |
|---|---|---|---|
| 0 · Repetidor | `pathDigi` | `WIDE1-1` | 1 |
| 1 · Rastreador | `pathTracker` | `WIDE1-1,WIDE2-1` | 2 |
| 2 · Ambos | `pathBoth` | `WIDE1-1,WIDE2-1` | 2 |

Cada modo usa la suya; en **todos** los casos `0` significa «no pido que me
repita nadie». El campo antiguo `path` se mantiene solo como respaldo (si alguno
de los tres estuviera vacío). La ruta se aplica a **todo lo que emite el nodo**:
balizas (fija y de rastreo), mensajes, boletines, objetos, meteorología, estado,
telemetría y respuestas automáticas.

El botón **«Recuperar valores recomendados»** del configurador rellena estos tres
campos (y el resto de ajustes habituales) de una vez; el indicativo y las
coordenadas **no se tocan** (son de cada uno). Nada se envía al nodo hasta pulsar
«Guardar en el nodo».

**Nota importante**: pedir saltos **no cambia el aviso de ritmo de aprs.fi**
(las copias repetidas se descartan como duplicado en APRS-IS); lo que cambia es
el **aire de radio**: con 2 saltos y 30 s de separación, el canal ve 3 emisiones
por baliza allí donde se solapen los repetidores. El operador ha decidido
mantener los 30 s; en su zona no hay tráfico (solo un iGate, 2 tramas/hora), así
que hoy no molesta a nadie.

## 7. Conclusión y lo que yo vigilaría

1. **En reposo y como repetidor: perfecto.** Un 0,6-1 % de ocupación con
   telemetría cada 10 min y meteorología cada 15 min es comportamiento de nodo
   estándar (una estación meteorológica CWOP manda cada 10 min).
2. **En marcha: es lo único que se sale de la media.** Hasta un 13,7 % del canal
   y 120 paquetes/hora al iGate. Los rastreadores LoRa de referencia van a
   1-3 minutos por baliza (3-7 %); nosotros podemos ir al doble por el mínimo de
   30 s combinado con el disparo cada 150 m.
3. **No hay riesgo de "autobaneo" documentado**, porque APRS-IS no publica un
   límite numérico y nuestro tráfico pasa por el iGate, no directo. Pero si algún
   día ruedas por una zona con muchos nodos LoRa, ese 13,7 % sí molesta a los
   demás, y es lo primero que un sysop miraría.
4. **Si algún día se quiere afinar (sin tocar nada hoy)**, basta con subir
   `trackerMinSpacing` de 30 a 45-60 s o el disparo de distancia de 150 a 250 m:
   se pierde muy poco detalle de la ruta y se baja a la mitad el uso del canal.
   El registro de viaje (GPX) apenas lo notaría.
5. **Lo que no hay que tocar**: los extras (telemetría, WX, estado, avisos de
   batería) son precisamente lo que hace que el nodo sea útil y están dentro de
   lo normal. El problema nunca serían ellos, sino la cadencia del rastreador.
