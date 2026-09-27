---
title: "Manual de uso"
subtitle: "Nodo APRS-LoRa para 433 MHz · Kacho System"
author: "EA2OY"
date: "Edición de septiembre de 2026 · revisión 2"
lang: es
toc: true
toc-depth: 2
numbersections: true
---

# Qué es este aparato

Es un **nodo de radioaficionado** para la banda de **433 MHz** que habla **APRS** por
**LoRa**. Dicho de otra forma: es un pequeño equipo que, por un lado, **repite** las
tramas que oye para que lleguen más lejos, y por otro **publica dónde está** con ayuda
del GPS. Todo lo que hace lo hace por radio; no necesita internet ni teléfono.

Funciona en cuatro combinaciones de placa y radio:

- **Faketec / ProMicro con nRF52840** y radio **Heltec HT-RA62**.
- La misma placa con radio **Ebyte E22P-433M30S** (que lleva amplificador propio).
- **LilyGO T-Echo**.
- **LilyGO T-Echo Plus**.

Todas llevan **el mismo firmware por dentro** y hacen lo mismo, pero **no son iguales**:
unas llevan pantalla OLED y otras de tinta electrónica (y se manejan de forma distinta),
y solo una lleva zumbador y motor de vibración. Las diferencias están contadas, una por
una, en el capítulo **«Las placas: qué lleva cada una»**.

## Qué NO es

- **No es un iGate.** Un iGate es lo que conecta la radio con internet, y eso exige
  wifi o cable de red. Este aparato no tiene ninguna de las dos cosas: **solo habla por
  radio**. Para llegar a internet necesita que **otro** equipo lo oiga, y ese otro
  equipo (el iGate) es el que lo sube a la red.
- **No es un teléfono ni un walkie.** No sirve para hablar; sirve para mandar datos y
  mensajes cortos de APRS.
- **No cifra nada.** El APRS es un servicio abierto de radioaficionado: lo que emite se
  puede oír, y está bien que así sea.

## Cómo llega tu posición a los mapas

El camino es siempre el mismo, y conviene tenerlo claro porque explica por qué a veces
no apareces en el mapa aunque el aparato parezca funcionar:

```
Tu nodo  →  (radio 433 MHz)  →  un iGate que te oye  →  APRS-IS  →  aprs.fi, findu, mapas LoRa
```

Es decir: **si no hay ningún iGate que te oiga, no apareces**. No es un fallo del
aparato: es que no hay nadie escuchando. En casa, con un iGate propio, el enlace es
fácil; en el monte, depende de la altura y de la antena.

> **Esta edición** documenta la versión del firmware que ya **dibuja la pantalla de
> tinta electrónica** del T-Echo, que trae **menú en la propia pantalla**, **perfiles de
> uso**, **avisos por vibración y por sonido** en el T-Echo Plus y las **nuevas
> cadencias de emisión**. Si tienes un manual anterior, este es el bueno.

---

# Las placas: qué lleva cada una

| | Faketec / ProMicro con HT-RA62 | Faketec / ProMicro con E22P | LilyGO T-Echo | LilyGO T-Echo Plus |
|---|---|---|---|---|
| **Pantalla** | OLED 128×64 | OLED 128×64 | Tinta electrónica 200×200 | Tinta electrónica 200×200 |
| **Radio** | SX1262, hasta 22 dBm | E22P con amplificador | SX1262 | SX1262 |
| **Batería** | 1 celda de litio o 3 pilas NiMH | Igual | 850 mAh | 2400 mAh |
| **GPS** | u-blox | u-blox | Quectel L76K | Quectel L76K |
| **Sensor de ambiente** | El que le pongas | El que le pongas | BME280 (de serie) | BME280 (de serie) |
| **Zumbador** | No | No | No | **Sí** |
| **Motor de vibración** | No | No | No | **Sí** |
| **Acelerómetro (IMU)** | No | No | No | La placa lo lleva, **el firmware no lo usa** |
| **Botones** | Uno | Uno | Táctil + físico | Táctil + físico |

## Qué hace el firmware con cada cosa

- **La pantalla.** En las Faketec, una **OLED** que se ilumina y se apaga, con **ocho
  escenas** que van rotando cada 4 segundos y un **menú que se maneja con un solo
  botón**. En los T-Echo y T-Echo Plus, una **pantalla de tinta electrónica** que **no se
  ilumina** (no tiene luz propia), tarda un par de segundos en cambiar y **conserva la
  imagen aunque el aparato esté apagado**; también tiene **ocho escenas**, pero cambian
  mucho más despacio, y su menú **se maneja con dos botones**. Todo esto está explicado
  en sus dos capítulos.
- **El zumbador y el motor de vibración** los lleva **solo el T-Echo Plus**, y solo en
  esa placa suenan y vibran. En las demás **no pasa nada**: no es que estén apagados, es
  que no existen. Los avisos están contados en el capítulo **«Avisos por vibración y por
  sonido»**.
- **El acelerómetro del T-Echo Plus NO se usa.** El firmware no lo lee para nada: ni
  cuenta pasos, ni detecta si andas o estás parado. Para saber si te mueves usa **el
  GPS**, que es lo que necesita de verdad para decidir cuándo emitir.
- **La batería es distinta** y por eso los umbrales de apagado y despertar vienen
  puestos **más bajos** en los T-Echo (3200 y 3400 mV) que en las Faketec (3400 y 3710
  mV; en la variante E22P, 3500 y 3710). **No son intercambiables**: si copias los de una
  placa en la otra, o se apaga antes de tiempo o se queda sin proteger la celda.
- **La potencia de fábrica** depende de la radio: **22 dBm** con la HT-RA62 y **8 dBm**
  con el E22P. En la E22P el firmware **no deja pasar de 12 dBm**, porque el módulo
  lleva su propio amplificador y no conviene apretarlo más.

> **Los T-Echo llevan dos firmwares distintos** según el cargador de arranque que traiga
> la placa (versión 6 o versión 7). No son intercambiables. Está explicado, con cómo
> saber cuál es el tuyo, en el capítulo de actualización del firmware.

---

# Requisitos de hardware: léelo antes de montar

## El divisor de la batería: DOS resistencias de 1 MΩ + 1 MΩ

**Esto es un requisito, no una recomendación.** En la Faketec (y en cualquier placa
nRF52840 con la radio E22P), el divisor de tensión que mide la batería **tiene que estar
hecho con dos resistencias iguales de 1 megaohmio (1 MΩ + 1 MΩ)**, de forma que el
factor del divisor sea **exactamente 2**.

> **Si no se cumple, pasan tres cosas, y las tres son graves:**
>
> 1. **No funciona el sistema de protección contra caída de tensión.** Ese sistema es el
>    que vigila la batería y manda el aparato a dormir **antes** de que se apague solo
>    por falta de tensión, para que no se quede a medias con el GPS o la radio
>    encendidos.
> 2. **El nodo se puede quedar dormido en la torre**: en un sitio al que no puedes subir
>    a arreglarlo, y creyendo que todavía le quedaba batería.
> 3. **Tampoco leería la tensión real de la batería.** Lo que enseñe en pantalla y lo
>    que mande por radio en la telemetría **no sería la tensión de verdad**, así que
>    toda la información de batería sería mentira.
>
> No lo vas a notar hasta que el aparato falle donde no puedes ir a buscarlo. Monta las
> dos resistencias iguales de 1 MΩ y comprueba la lectura antes de subirlo.

En las placas T-Echo y T-Echo Plus el divisor **ya viene montado de fábrica** y no hay
que hacer nada.

## La antena

**Sin antena de 433 MHz no transmitas**: además de no llegar, puedes dañar la etapa de
salida. Y la antena no es un adorno: es la diferencia entre que te oiga alguien o no.
Cuanto más alta y más despejada, mejor.

## La alimentación

Una **batería de litio de una celda**, **tres pilas NiMH**, o el propio **cable USB** para
tenerlo en la mesa. Con el USB enchufado el aparato **nunca se duerme** (ver el capítulo
de autonomía).

## Un iGate cerca

Si quieres aparecer en los mapas, hace falta que **alguien te oiga**: tu propio iGate o
el de algún compañero. Sin eso, el aparato funciona, pero tu posición no llega a ningún
visor.

## Un ordenador con Chrome o Edge

El configurador web solo funciona en **Chrome o Edge** (Firefox y Safari no tienen la
función que hace falta para hablar por USB). No hay que instalar nada más.

---

# Los tres modos de trabajo

El aparato hace tres cosas, y tú eliges cuáles con un ajuste llamado **modo**.

| Modo | Qué hace |
|---|---|
| **Repetidor** | Escucha las tramas de otros y **las repite** para que lleguen más lejos. Además anuncia su propia posición fija |
| **Rastreador** | **Publica dónde estás** con posición, rumbo, velocidad y altura. **No repite** a nadie |
| **Ambos** | Las dos cosas a la vez: repite a los demás **y** publica tu posición |

**El repetidor y el rastreador son independientes.** Repetir consiste en oír una trama y
volver a emitirla; rastrear consiste en mandar tu posición cada cierto tiempo. El modo no
enciende ni apaga el repetidor: lo que decide es **si el rastreador trabaja** (modos
Rastreador y Ambos) y **qué posición se anuncia**.

## Cuándo manda posición el nodo, en resumen

| Situación | ¿Manda posición? |
|---|---|
| Repetidor, al arrancar | **Sí**, la posición fija configurada |
| Repetidor, cada X minutos | **Sí**, la posición fija configurada |
| Repetidor con «GPS en repetidor» y fijación | **Sí**, la del GPS en vivo |
| Rastreador o Ambos, **sin** fijación | **No.** Solo el aviso «buscando satélites» |
| Rastreador o Ambos, **con** fijación | **Sí**, la real. La primera en cuanto fija |
| Rastreador o Ambos, en movimiento | **Sí**, según el perfil, los giros y la distancia |
| Rastreador o Ambos, **parado** | **Sí**, una baliza lenta **cada 15 minutos** |
| Dos toques seguidos del botón | Sí: posición real, o la última conocida |

---

# Las tres reglas de oro sobre la posición

Estas tres reglas son deliberadas y explican casi todo lo que hace el aparato cuando lo
ves en un mapa. Merece la pena entenderlas.

## Regla 1 — El rastreador nunca publica una posición inventada

Si estás en modo **Rastreador** o **Ambos** y el GPS todavía **no ha fijado satélites**,
el nodo **no manda ninguna posición**. Manda solo un aviso de texto diciendo que está en
marcha y buscando satélites.

Esto es a propósito. Antes sí mandaba la posición fija que tuvieras configurada, y eso
era engañoso: en el mapa aparecías en un sitio donde no estabas. **Ya no se hace.**

## Regla 2 — En modo Repetidor, la posición fija es la suya

Un repetidor está clavado en un sitio, y ese sitio es su razón de ser. Así que en modo
**Repetidor** el nodo anuncia **la posición que le hayas configurado**, y eso es
correcto. Si además activas «GPS en repetidor», usará la del GPS en vivo.

## Regla 3 — Puedes mandar la última posición conocida a mano

Si quieres publicar dónde estuviste, aunque ahora el GPS no tenga fijación: **dos toques
seguidos del botón**.

- Si el GPS **tiene** fijación: manda tu posición **de ahora**.
- Si **no** la tiene: manda la **última posición real** que fijó, aunque sea de un paseo
  anterior (el nodo la recuerda aunque lo apagues).
- Si **nunca** ha fijado: no manda nada y te lo dice en la pantalla. Mejor eso que
  publicar algo falso.

Esa posición viaja marcada como histórica de dos formas a la vez: lleva **su hora real**
(la de cuando se tomó, no la de ahora) y además el aviso **`ULTIMA CONOCIDA`** al
principio del comentario. Así cualquiera que la vea sabe que es antigua sin tener que
deducirlo.

> **Nota**: el sello de hora de APRS solo lleva **día, hora y minuto**, sin el año. Por
> eso una posición de **más de 24 horas** puede interpretarse como del día anterior.
> Para un paseo del mismo día es exacta; si es más vieja, el aviso `ULTIMA CONOCIDA` es
> lo que evita el malentendido.

---

# Cómo viene el aparato de fábrica

Esto es lo que te vas a encontrar la primera vez que lo enciendas. Conviene leerlo
entero, porque hay una cosa que sorprende y es **a propósito**.

1. **Viene en modo «Ambos»**: repite lo que oye **y** publica su posición.
2. **Viene SIN indicativo.** El que trae puesto, `NOCALL-11`, es un hueco vacío: no es
   nadie.
3. **Mientras no le pongas un indicativo tuyo, NO TRANSMITE ABSOLUTAMENTE NADA.** Ni
   balizas, ni telemetría, ni meteorología, ni mensajes, ni repeticiones. Nada de nada.
4. **Eso es una protección, no un fallo.** El aparato no puede saber si el indicativo
   que lleva es de verdad tuyo, así que prefiere quedarse callado antes que **ensuciar
   la frecuencia con una estación que no existe**. En cuanto le pongas tu indicativo,
   empieza a transmitir solo. La pantalla y el configurador funcionan igual mientras
   tanto, para que puedas configurarlo con tranquilidad.
5. **La telemetría y la meteorología vienen ACTIVADAS.** Si el aparato tiene sonda de
   clima, la aprovecha sin que nadie tenga que encender nada. El que no las quiera, las
   apaga en el menú o en el configurador.
6. **La baliza de posición viene cada 30 minutos** (el mínimo son 15).
7. **El registro de viaje viene encendido**, porque el modo de fábrica (Ambos) lo
   necesita.
8. **Los perfiles vienen puestos** así: peatón con el número 7, bici con el 8 y coche
   con el 5.

---

# Los tiempos de emisión (y por qué son esos números)

El aparato tiene **tres emisiones automáticas**, cada una con su propio tiempo:

| Qué se emite | De fábrica | Se puede poner | Qué es |
|---|---|---|---|
| **Baliza de posición** | **30 minutos** | De **15** a 240 minutos | Dice dónde estás (o dónde está el repetidor) |
| **Telemetría** | **53 minutos** | 0 (solo a mano) o de 15 a 720 minutos | Los números de los sensores y la batería |
| **Meteorología** | **55 minutos** | 0 (solo a mano) o de 15 a 720 minutos | El paquete de tiempo, el que hace que aprs.fi dibuje las gráficas |

**Los tres tiempos se pueden cambiar** desde el menú del aparato y desde el
configurador web. **El mínimo son 15 minutos** en los tres casos: por debajo de eso el
firmware no acepta el valor.

## Por qué esos números y no otros

La frecuencia es **compartida**: es de todos los radioaficionados de la zona, y **mientras
el nodo transmite está sordo** (la radio no puede oír y hablar a la vez). Cada paquete
ocupa unos segundos de aire, así que hablar de más no es solo una descortesía: es
**perder paquetes de los demás**.

Por eso los tres tiempos van **a propósito desincronizados**: 30, 53 y 55 **no comparten
factores** (30 = 2×3×5, 53 es primo y 55 = 5×11), así que **casi nunca coinciden en el
mismo minuto** y no se juntan en ráfagas de paquetes seguidos. Con tres números
redondos, en cambio, se encontrarían cada pocas horas.

**La baliza de un repetidor fijo no hace falta que sea rápida**: un repetidor no se
mueve, así que su posición no cambia. Tampoco conviene subirla mucho más de 30 minutos,
porque los visores marcan las estaciones como «no recientes» pasados esos minutos y el
nodo desaparecería del mapa.

> **Un detalle práctico**: si pides dos saltos en tus tramas, cada baliza se emite tres
> veces (la tuya y dos copias). En ese caso, **sube el tiempo entre balizas** en vez de
> dejarlo en 30 minutos.

---

# Los perfiles de uso

Un **perfil** es un juego de ajustes guardado para una forma de moverte. Sirve para no
tener que tocar nada cada vez que cambias de actividad: eliges el perfil y el aparato
sabe **con qué número de indicativo sale al aire** y **cada cuánto emite**.

Hay **cuatro perfiles**:

| Perfil | Para qué sirve | Número de indicativo (SSID) de fábrica | Icono de fábrica | Cada cuánto emite |
|---|---|---|---|---|
| **Fijo / Digi** | El aparato plantado en un sitio: una casa, un repetidor, una torre | Usa tu indicativo **tal cual**, sin número | Estrella (repetidor) | Baliza cada 30 minutos |
| **Peatón** | Andando | **-7** | Persona | Entre 5 y 30 minutos, según la velocidad |
| **Bicicleta** | En bici | **-8** | Bici | Entre 3 y 15 minutos, según la velocidad |
| **Coche** | En coche o moto | **-5** | Coche | Entre 30 segundos y 5 minutos, según la velocidad |

**Cada perfil usa un número distinto a propósito.** Así los mapas crean **un recorrido
separado por perfil**: el del coche y el del paseo a pie no se mezclan en el mismo
trazo, y luego puedes mirar cada uno por su cuenta. El sistema **no deja** poner el
mismo número en dos perfiles.

Los perfiles **de peatón, bici y coche son para el rastreador**: son ellos los que deciden
cada cuánto se manda tu posición mientras te mueves. **El perfil «Fijo / Digi» es el del
aparato plantado**: sale con tu indicativo tal cual (sin número) y no aplica cadencias por
velocidad, sino el tiempo fijo de la baliza.

## Cómo se cambian

En el **T-Echo y T-Echo Plus**, desde el menú: **Tracker → Perfil**. Se abre una lista
con los cuatro perfiles (el activo va marcado), y debajo una fila, **Editar ajustes**,
para cambiar los ajustes del perfil que tengas puesto: el **número del indicativo
(SSID)**, el **tiempo lento** (en segundos), el **tiempo rápido** (en segundos), los
**metros** (la distancia a la que quieres que mande una posición aunque no toque por
tiempo) y el **icono del mapa**.

En la **Faketec**, desde el menú: **Rastreador → Perfil de movimiento**. Ahí se elige el
perfil y nada más (el icono del perfil se cambia con la fila de al lado, **Icono del
perfil**). Los tiempos y los números finos de cada perfil se cambian desde el menú del
T-Echo o desde la consola del configurador.

> **Si dejas los tiempos a cero, el aparato usa los valores normales de la comunidad**,
> que son los de la tabla de arriba. Solo hace falta tocarlos si quieres ir más
> despacio o más rápido de lo normal.

## El icono del mapa va con el perfil

Cada perfil lleva **su propio icono** para el mapa, así que al cambiar de perfil **no solo
cambia el número del indicativo: también cambia el dibujo** con el que apareces.

| Perfil | Icono en el mapa |
|---|---|
| **Fijo / Digi** | La **estrella** del repetidor |
| **Peatón** | Una **persona** |
| **Bicicleta** | Una **bici** |
| **Coche** | Un **coche** |

El icono de cada perfil **se puede cambiar** desde los dos menús:

- En el **T-Echo y T-Echo Plus**: **Tracker → Perfil → Editar ajustes → Icono**, que va
  recorriendo los cuatro.
- En la **Faketec**: **Rastreador → Icono del perfil**. Cada pulsación pasa al siguiente
  icono del perfil que tengas activo, y te lo dice en pantalla.

En el **configurador web** no se tocan: allí solo se elige qué perfil está activo.

> **El detalle que hay que entender**: el icono del perfil solo actúa **mientras no hayas
> puesto uno a mano**. Si tú fijas un icono con el ajuste **Símbolo** (en el menú APRS o en
> el configurador web), **manda el tuyo** y el del perfil deja de usarse. Es a propósito:
> así nadie pierde el icono que había elegido. Si quieres que el icono vuelva a ir con el
> perfil, deja el **Símbolo** en su valor de origen (la estrella, con la tabla normal).

En la **Faketec**, el menú solo deja **elegir el perfil** (Rastreador → Perfil de
movimiento): los iconos de cada perfil se cambian desde el menú del T-Echo o desde la
consola. En el **configurador web** tampoco se tocan: allí solo se elige qué perfil está
activo.

---

# Primeros pasos

## Paso 1 — Alimentar y comprobar que vive

Conecta el nodo al ordenador por USB. Verás la pantalla de presentación con el nombre,
la versión y el modo de trabajo.

- En la **Faketec**, la OLED se enciende sola y se ve el logotipo y los datos.
- En el **T-Echo**, la pantalla de tinta tarda **unos segundos** en pintarse (es lenta,
  es normal) y **no se ilumina**: si estás a oscuras, se enciende una luz de fondo
  durante 5 segundos cada vez que tocas un botón.
- En el **T-Echo Plus**, además **notarás una vibración** al arrancar: es el aviso de que
  ha empezado a funcionar. Ese detalle tiene su gracia cuando despiertas el aparato
  pulsando el botón de reset, porque no hay otra señal.

## Paso 2 — Poner tu indicativo (lo primero de todo)

**Mientras no le pongas un indicativo, el aparato no transmite nada.** Se pone en el
menú del aparato (**APRS → Indicativo**) o desde el configurador web, y hay que
escribirlo **en mayúsculas**, con el número de equipo si lo quieres (**por ejemplo,
`EA2ABC-7`**). Los números del 1 al 15 sirven para distinguir tus equipos.

## Paso 3 — Configurarlo con el configurador web

El configurador es **un solo fichero de página web** que habla con el nodo por el cable
USB. No hay que instalar nada.

1. Abre **Chrome o Edge** en el ordenador.
2. Abre el fichero del configurador (**`index.html`**, en la carpeta `web`).
3. Pulsa **Conectar** y elige el puerto del nodo.
4. La página **lee la configuración real** del nodo y rellena el formulario.
5. Rellena lo que necesites y pulsa **Guardar**. La respuesta te enseña **los valores que
   el nodo ha aceptado** (si ha recortado alguno, lo verás).

## Paso 4 — Lo mínimo que hay que rellenar

| Ajuste | Por qué importa |
|---|---|
| **Indicativo** | Sin indicativo no eres nadie en el aire, y el nodo no emite nada |
| **Coordenadas** | Si están a cero, aparecerás en medio del Atlántico |
| **Modo de trabajo** | Repetidor, rastreador o ambos |
| **Rutas (saltos)** | Deciden si te repiten los demás. Ver el capítulo de rutas |

El botón **«Recuperar valores recomendados»** rellena el resto con ajustes sensatos de la
red... **menos el indicativo y las coordenadas**: esos son tuyos y hay que ponerlos a mano.

## Paso 5 — Mirar que funciona

Lo que de verdad dice si funciona **es la radio**, no la pantalla:

1. Si tienes un iGate cerca, mira en su página web si te oye.
2. En los visores (**aprs.fi**, el mapa LoRa de tu zona) busca tu indicativo.
3. En el propio nodo, el **registro de viaje** te dice qué ha mandado y qué ha oído.

---

# Cómo se maneja: los botones

**Las dos familias de placas no se manejan igual.** Esta es la parte que más confunde si
vienes de la otra, así que va separada.

## En la Faketec: un solo botón

| Gesto | Qué hace |
|---|---|
| **Toque corto** | Enciende la pantalla si está apagada; si no, pasa a la escena siguiente. Dentro del menú, baja de línea |
| **Toque largo** (sin soltar) | Abre el menú. Dentro del menú: entra, edita, ejecuta o confirma |
| **Dos toques seguidos** | **Manda una baliza de posición a mano** (ver la regla 3) |

El toque largo se dispara **mientras mantienes el dedo** (a partir de medio segundo), sin
esperar a que sueltes: no hay nada que adivinar. Los dos toques seguidos se reconocen si
el segundo llega **dentro de los 0,8 segundos** siguientes a soltar el primero: no hace
falta que sea rapidísimo, lo que hay que evitar es **dejar el dedo apoyado**.

## En el T-Echo y T-Echo Plus: dos botones

Estas placas tienen **un botón táctil** (una superficie que notas al rozarla, sin partes
móviles) y **un botón físico** al lado.

| Botón y gesto | Fuera del menú | Dentro del menú |
|---|---|---|
| **Táctil** (un toque) | Pasa a la escena siguiente y enciende la luz | **Mueve el cursor** (y cambia el valor, si estás editando) |
| **Físico, toque corto** | Pasa a la escena siguiente | **Entra y confirma** (y en un texto, pasa a la letra siguiente) |
| **Físico, toque largo** | **Abre el menú** | **Vuelve atrás** (y guarda lo que estabas escribiendo) |
| **Físico, dos toques seguidos** | **Manda una baliza de posición a mano** | No manda nada (ver el aviso de abajo) |

En resumen, para que no se olvide: **el táctil navega y cambia cosas; el físico confirma
con un toque corto y vuelve atrás con uno largo**.

El botón de reset de la placa **no es un botón de manejo**: sirve para grabar el firmware
y para despertar el aparato cuando está dormido (ver el capítulo de autonomía).

## Dentro del menú, cuidado con no confundir los toques

- **Dos toques seguidos del botón físico, dentro del menú, NO mandan baliza.** Si estabas
  escribiendo un valor, **cancelan lo escrito** y dejan el valor que había. Si no estabas
  escribiendo, no hacen nada. Esto es a propósito: antes podían ejecutar dos veces la
  misma orden, y dos veces un «borrar» o un «reiniciar» es exactamente lo que no
  queremos.
- **Mientras hay un aviso de confirmación en pantalla** (ver más abajo), el táctil **no
  hace nada**: no se mueve el cursor, para que no te lleves un susto. Se sale
  confirmando, o dejando pasar el tiempo.
- **Dos toques del táctil** son dos toques del táctil: cada uno mueve una línea. No hay
  gesto doble en el táctil.

---

# La pantalla OLED (placas Faketec)

## Las 8 escenas

La pantalla va enseñando **ocho escenas** que cambian solas cada **4 segundos**, y
**cada una tiene su propio dibujito abajo** para que sepas en cuál estás:

| # | Escena | Qué enseña |
|---|---|---|
| 1 | **Estado** | Tu indicativo, el modo y los contadores de tráfico (recibidas, emitidas, repetidas) |
| 2 | **Últimos RX** | Las últimas tramas **recibidas**, con la señal que traían |
| 3 | **Últimos TX** | Lo último que ha **emitido** el nodo |
| 4 | **Radio** | Frecuencia, velocidad y ancho de banda |
| 5 | **Sensores** | Temperatura, humedad y presión |
| 6 | **Sistema** | Versión del firmware y datos internos |
| 7 | **Estaciones** | Las estaciones que ha oído, con distancia y rumbo |
| 8 | **GPS** | Tu posición, o «sin fijación» si aún no la tiene |

Un toque corto pasa de escena y **pausa el avance automático** unos segundos, para que te
dé tiempo a leer. Se puede desactivar el avance automático en **Pantalla → Rotar
pantallas solo**.

## El menú, sección por sección

El menú está organizado **por categorías**, para que con un solo botón no sea una lista
interminable. Son **catorce secciones**, y cada una lleva dentro sus opciones:

### Modo de trabajo

| Opción | Qué hace |
|---|---|
| **Modo** | Elige **Repetidor**, **Rastreador** o **Ambos**. Al entrar se ven las tres opciones de una vez, con la que está puesta marcada |

### GPS

| Opción | Qué hace |
|---|---|
| **GPS en repetidor** | En modo repetidor, usar la posición del GPS en vivo en vez de la fija. **Solo sale en modo repetidor** |
| **Ahorro de GPS** | En modo repetidor, encender y apagar el GPS por ciclos para gastar menos. **Solo sale en modo repetidor** |
| **Fijar coords actuales** | Hace todo el trabajo solo: enciende el GPS, espera a que fije (sin tope de tiempo), espera a que la posición se asiente, la guarda como posición fija del repetidor y **vuelve a apagar el GPS**. Funciona en cualquier modo, sin tocar antes «GPS en repetidor». Mientras trabaja puedes **cancelar con el botón** |
| **Ver GPS** | Enseña si hay fijación, cuántos satélites ve y la posición |

> Las dos primeras opciones **no salen** en modo Rastreador ni en Ambos, porque allí el
> GPS va siempre encendido y no harían nada. Si no las encuentras, mira en qué modo estás.

### Balizas

| Opción | Qué hace |
|---|---|
| **Enviar baliza ahora** | Manda la baliza en ese momento: en repetidor, la posición fija; en rastreador, tu posición **real**. Si el GPS no tiene fijación, **no manda nada** (un rastreador no inventa una posición): lo dice en pantalla y por USB |
| **Baliza de rastreador** | Manda tu posición real. Si no hay fijación, espera unos segundos a ver si fija y, si no lo consigue, **no manda nada** y lo avisa. Para mandar la última posición conocida está el **doble toque del botón** |
| **Enviar telemetría** | Manda los números de los sensores y la batería |
| **Formato de telemetría** | Manda la descripción de los canales, para que los visores sepan qué es cada número |
| **Baliza cada (min)** | Cada cuánto se manda la baliza de posición. De **15** a 240 minutos |
| **Posición comprimida** | Manda la posición en un formato más corto, que ocupa menos aire |
| **Ocultar dígitos (0-4)** | Difumina la posición (borra decimales) para una estación fija que no quiera dar la dirección exacta |

### Mensajes

| Opción | Qué hace |
|---|---|
| **Mensaje rápido** | El texto que se manda con un par de toques, sin escribirlo cada vez |
| **Reintentos sin acuse** | Cuántas veces se repite un mensaje si el otro no contesta. De 0 a 5 |
| **Enviar al último oído** | Manda el mensaje rápido a la última estación que hayas oído |

### Rastreador

| Opción | Qué hace |
|---|---|
| **Baliza cada (seg)** | Cada cuánto mandar posición **sin perfil** (de 10 a 3600 segundos) |
| **Distancia (m) cada** | Mandar también cuando te hayas movido esos metros (0 = no) |
| **Tiempo mínimo (seg)** | El mínimo entre dos balizas, para no saturar. De 10 a 120 segundos |
| **Perfil de movimiento** | Elige entre **Sin perfil**, **A pie**, **Bici** y **Coche** |
| **Icono del perfil** | Cambia el icono del mapa del perfil que tengas puesto: cada pulsación pasa al siguiente (Repetidor, Persona, Bici, Coche) y te lo dice en pantalla |
| **Enviar altitud** | Incluir la altura en la baliza |
| **Dormir entre balizas** | Apagar el aparato entre una baliza y la siguiente, para durar más |

### Radio

| Opción | Qué hace |
|---|---|
| **Frecuencia** | 433.775 MHz (la de la red), 433.900 o 868.200 |
| **Velocidad (SF)** | La velocidad de la modulación, de 5 a 12. La red usa **12** |
| **Codificación (CR)** | De 4/5 a 4/8. La red usa **4/5** |
| **Ancho de banda (kHz)** | 62.5, 125, 250 o 500. La red usa **125** |
| **Potencia (dBm)** | La potencia de salida |
| **Escuchar antes de hablar** | Antes de transmitir, comprueba que el canal está libre |

> **Estos ajustes y el modo módem necesitan reiniciar el aparato** para aplicarse. Los
> demás cambios se aplican en marcha.

### Repetidor

| Opción | Qué hace |
|---|---|
| **Modo repetidor** | **Apagado**, **WIDE1-1** o **WIDE1+WIDE2**: qué repite |
| **Lista negra** | Indicativos que **no** quieres repetir, separados por espacios |
| **Silenciar (no emitir)** | El nodo deja de transmitir, pero sigue oyendo |

### APRS

| Opción | Qué hace |
|---|---|
| **Indicativo** | Tu indicativo. **Sin él, el aparato no transmite nada** |
| **Identificador (tocall)** | La «matrícula» del dispositivo, la que hace que los visores digan de qué aparato se trata |
| **Ruta (path)** | La ruta de respaldo, si alguna de las de abajo está vacía |
| **Ruta en repetidor** | Los saltos que pides cuando el nodo está fijo |
| **Ruta en rastreador** | Los saltos que pides cuando vas de rastreador |
| **Ruta en ambos** | Los saltos que pides en modo Ambos |
| **Símbolo** | **El icono con el que sales en el mapa.** Es un ajuste aparte del perfil |
| **Comentario** | El texto que acompaña a tu baliza |
| **Latitud** y **Longitud** | La posición fija del repetidor |
| **Responder consultas** | Si contesta a las preguntas que le hagan por radio |
| **Modo TNC (USB)** | **Apagado**, **TNC2 texto** o **KISS**, para usarlo como módem |

### Bluetooth

| Opción | Qué hace |
|---|---|
| **Bluetooth** | **No hace nada.** El Bluetooth está fuera de esta versión |
| **PIN Bluetooth** | **No hace nada**, por el mismo motivo |

### Sensores

| Opción | Qué hace |
|---|---|
| **Enviar tiempo (WX)** | Mandar el paquete de meteorología. **Viene activado** |
| **Enviar telemetría** | Mandar la telemetría. **Viene activada** |
| **Telemetría cada (min)** | Cada cuánto. **0 = solo a mano**; si no, de 15 a 720 |
| **Meteorología cada (min)** | Cada cuánto. **0 = solo a mano**; si no, de 15 a 720 |
| **Corregir sonda (C)** | Ajuste fino de la sonda exterior de temperatura |
| **Ajuste chip (C)** | Ajuste del sensor que va dentro de la caja |

### Pantalla

| Opción | Qué hace |
|---|---|
| **Rotar pantallas solo** | Que las escenas vayan cambiando solas |
| **Apagar pantalla (seg)** | Apagarla sola a los X segundos. **De fábrica, 0 = no se apaga nunca** |
| **Avisos en pantalla** | Enseñar los avisos que van llegando |

### Energía

| Opción | Qué hace |
|---|---|
| **Tensión de apagado (mV)** | Por debajo de esta tensión, el nodo se duerme para proteger la batería |
| **Tensión de despertar (mV)** | Cuando la tensión sube por encima de esta, vuelve a arrancar |
| **Ver batería** | Enseña la tensión que está midiendo |
| **Apagar (dormir)** | Apaga el aparato (pide confirmación) |

### Control remoto

| Opción | Qué hace |
|---|---|
| **Control por radio** | Aceptar órdenes mandadas por radio. **Viene apagado** |
| **Operadores autorizados** | La lista de indicativos que pueden mandarle órdenes |
| **Silenciar / Reactivar** | Cambia el silencio de la radio |

### Ajustes

| Opción | Qué hace |
|---|---|
| **Reiniciar** | Reinicia el aparato (pide confirmación) |
| **Modo grabación (USB)** | Lo deja listo para grabar firmware por el cable (pide confirmación) |
| **Valores de fábrica** | Borra la configuración y deja la de origen. **Conserva tu indicativo** (pide confirmación) |
| **Borrado total (solo USB)** | Borra todo, incluido el indicativo (pide confirmación) |

## Cómo se navega y cómo se cambian los valores

- **Toque corto**: baja de línea. Al final de la lista vuelve arriba.
- **Toque largo**: entra en la opción.
- Arriba y abajo de cada sección hay una fila para **salir** y para **volver**.
- Las **listas cortas** (el modo de trabajo, el perfil, la ruta) se abren con el toque
  largo y se recorren con toques cortos: la pantalla te enseña la palabra, no un número.
  El toque largo **confirma** la que esté marcada.
- Los **valores de lista** (la frecuencia, el ancho de banda, el modo repetidor, el modo
  módem) **cambian directamente con el toque largo**: cada pulsación pasa a la siguiente
  opción.
- Los **números** se editan con una **rueda de caracteres**: el toque corto cambia el
  carácter donde estás, el toque largo pasa al siguiente. Al final de la rueda hay dos
  símbolos especiales: **`<` borra la letra anterior** y **`~` guarda y sale**.
- Los **interruptores** (sí/no) cambian con el toque largo, y ya está.
- **Dos toques seguidos mientras editas** = cancelar: se queda el valor que había.

## Las acciones peligrosas piden confirmación: hay que pulsar largo DOS veces

Las **cinco** acciones que **apagan o borran algo** son: **Apagar (dormir)**,
**Reiniciar**, **Modo grabación**, **Valores de fábrica** y **Borrado total**.

Con esas **no basta con una pulsación larga**:

1. La primera pulsación larga **no hace nada**: sale un aviso, **«Pulsa largo:
   confirmar»**, y el aparato se queda esperando.
2. Hay que **volver a pulsar largo en la misma opción**, dentro de los **3 segundos**
   siguientes. Entonces sí se ejecuta.
3. **Si no vuelves a pulsar, no pasa nada**: la confirmación se olvida sola. Y si pulsas
   largo en **otra** acción distinta, la anterior se olvida y empieza a contar la nueva.

Este doble paso existe porque una pulsación larga de más en «Borrado total» borraba la
configuración sin preguntar. Ahora hay que querer hacerlo dos veces.

## El menú se cierra solo a los 20 segundos

Si lo dejas abierto y no tocas nada durante **20 segundos**, se cierra y la pantalla
vuelve al carrusel de escenas. Así un nodo que te has dejado olvidado no se queda
plantado en el menú toda la tarde.

**Con aviso**: en los **3 últimos segundos** aparece una cuenta atrás en la esquina. Y
cualquier toque reinicia la cuenta, así que **nunca se cierra mientras lo estás usando**.

> Si estás escribiendo un valor (por ejemplo el indicativo) y te quedas parado, el menú
> se cierra y **lo escrito se descarta**: se queda el valor que había. No se guarda nada
> a medias.

## La pantalla se apaga (o no)

De fábrica **la pantalla NO se apaga nunca**. Así, un nodo recién montado siempre enseña
algo y no parece muerto. Si lo pones en un sitio donde no quieras luz, se pone un tiempo
en **«Apagar pantalla (seg)»**: 30 o 60 segundos van bien. Cualquier toque la enciende
otra vez, y si tenías el menú abierto **vuelve donde lo dejaste**.

---

# La pantalla de tinta electrónica (T-Echo y T-Echo Plus)

> **Este capítulo es para las placas LilyGO T-Echo y T-Echo Plus.** Si tienes una
> Faketec, tu pantalla es la OLED y lo que te interesa es el capítulo anterior.

**La pantalla de estos aparatos funciona**: dibuja las escenas, dibuja el menú y se
maneja con los dos botones. Lo que pasa es que **es una tinta electrónica, no una
pantalla normal**, y eso cambia la forma de usarla.

## Lo primero que hay que entender

1. **No se ilumina.** Es tinta: se lee con la luz que hay, como el papel. En la oscuridad
   no se ve nada. Por eso el aparato **enciende una luz de fondo durante 5 segundos cada
   vez que tocas un botón**.
2. **Tarda un par de segundos en cambiar**, y al cambiar hace un parpadeo. No es un
   cuelgue: es cómo funciona esta pantalla.
3. **La imagen se queda aunque el aparato esté apagado.** Es lo bueno que tiene: cuando
   lo duermes, en la pantalla queda escrito que está dormido, con tu indicativo en
   grande, en vez de quedarse en blanco.
4. **Cambia despacio a propósito.** Las escenas rotan cada **45 segundos** (en la OLED
   son 4), y **cuando tú tocas un botón, el carrusel se calla 2,5 minutos** para que te
   dé tiempo a leer lo que has puesto. Refrescar muy a menudo, además, obligaría a tener
   el GPS encendido más tiempo y gastaría batería.

## Las 8 escenas

| # | Escena | Qué enseña |
|---|---|---|
| 1 | **Tu indicativo** (Estado) | El indicativo, el icono del perfil activo, los contadores (R = recibidas, T = emitidas, D = repetidas), la posición, la velocidad y los satélites, el tiempo, la calidad de la última señal y la batería |
| 2 | **Radio** | El modo de trabajo, la frecuencia, la velocidad y el ancho de banda, y los contadores uno a uno |
| 3 | **Sensores** | Temperatura, humedad y presión |
| 4 | **Sistema** | Datos internos del aparato |
| 5 | **Estaciones** | Las estaciones que ha oído, con distancia y rumbo |
| 6 | **Últimos RX** | Lo último que ha recibido |
| 7 | **Últimos TX** | Lo último que ha emitido |
| 8 | **GPS** | Posición, velocidad, satélites, altura, rumbo y precisión; o «buscando posición» |

El **modo de trabajo** (repetidor, rastreador o ambos) se ve en la escena **Radio**, no
en la de Estado: en una pantalla de 200 puntos, con la letra grande que se usa, no caben
las dos cosas juntas.

Cuando llega o sale tráfico, encima de la escena sale un **aviso** de un par de segundos
(por ejemplo, la estación que acaba de oír y con qué señal), y la escena sigue debajo.

## El menú, sección por sección

Son también **catorce secciones**. La diferencia con el de la OLED está en los detalles
que se marcan aquí:

### Modo

| Opción | Qué hace |
|---|---|
| **Modo** | Elige **Repetidor**, **Rastreador** o **Ambos**. Al entrar en la sección sale directamente la lista de los tres modos |

### GPS

| Opción | Qué hace |
|---|---|
| **GPS en repetidor** | Usar la posición del GPS en vez de la fija (solo tiene efecto en modo repetidor) |
| **Ahorro de GPS** | Encender y apagar el GPS por ciclos (solo tiene efecto en modo repetidor) |
| **Fijar coords** | Hace todo el trabajo solo: enciende el GPS, espera a que fije (sin tope de tiempo), espera a que la posición se asiente, la guarda como posición fija del aparato y **vuelve a apagar el GPS**. Funciona en cualquier modo, sin tocar antes «GPS en repetidor». Lo va contando en pantalla (satélites, `Muestra 7/20`, barra) y **se puede cancelar con el botón** mientras trabaja |
| **Ver GPS** | Enseña si hay fijación, los satélites y la posición |

> **Ojo con la primera y la segunda**: en la OLED se esconden cuando no sirven, y aquí
> **salen siempre**, porque el operador pidió ver todas las opciones. Si no tienes el
> nodo en modo repetidor, esas dos no hacen nada.

### Balizas

| Opción | Qué hace |
|---|---|
| **Enviar baliza** | Manda la baliza en ese momento (en rastreador, solo si hay fijación GPS) |
| **Baliza rastreador** | Manda tu posición real, y solo si hay fijación: sin ella no manda nada y lo avisa |
| **Telemetria** | Manda la telemetría |
| **Baliza cada (min)** | Cada cuánto se manda la baliza. De **15** a 240 minutos |
| **Posicion comprimida** | Manda la posición en formato corto |

### Mensajes

| Opción | Qué hace |
|---|---|
| **Msg rapido** | El texto del mensaje rápido |
| **Reintentos** | Cuántas veces se repite un mensaje sin acuse. De 0 a 5 |

> En esta pantalla **no hay** una fila para mandar el mensaje al último oído (la OLED sí
> la tiene): el mensaje se manda desde el configurador web o desde la consola.

### Tracker

| Opción | Qué hace |
|---|---|
| **Tracker cada (seg)** | Cada cuánto mandar posición sin perfil. De 10 a 3600 segundos |
| **Distancia (m)** | Mandar también cada X metros (0 = no) |
| **Tiempo min (seg)** | El mínimo entre dos balizas. De 10 a 120 segundos |
| **Perfil** | **Abre el editor de perfiles**: los cuatro perfiles y, debajo, **Editar ajustes** para cambiar el número del indicativo, el tiempo lento, el tiempo rápido, los metros y **el icono del mapa** del perfil activo |
| **Enviar altitud** | Incluir la altura en la baliza |
| **Dormir entre** | Apagar el aparato entre una baliza y la siguiente |

### Radio

| Opción | Qué hace |
|---|---|
| **Frecuencia** | 433.775 MHz (la de la red), 433.900 o 868.200 |
| **SF** | La velocidad de la modulación, de 5 a 12. La red usa **12** |
| **CR** | La codificación, de 4/5 a 4/8. La red usa **4/5** |
| **Ancho banda** | 62.5, 125, 250 o 500 kHz. La red usa **125** |
| **Potencia** | La potencia de salida |
| **CAD** | Escuchar antes de hablar |

### Digi

| Opción | Qué hace |
|---|---|
| **Modo digi** | **Apagado**, **WIDE1-1** o **WIDE1+WIDE2**: qué repite |
| **Blacklist** | Indicativos que no quieres repetir |
| **Silenciar** | El nodo deja de transmitir, pero sigue oyendo |

### APRS

| Opción | Qué hace |
|---|---|
| **Indicativo** | Tu indicativo. **Sin él, el aparato no transmite nada** |
| **Tocall** | La «matrícula» del dispositivo |
| **Path** | La ruta de respaldo |
| **Path digi** | Los saltos que pides como repetidor |
| **Path tracker** | Los saltos que pides como rastreador |
| **Path ambos** | Los saltos que pides en modo Ambos |
| **Simbolo** | **El icono con el que sales en el mapa.** Es un ajuste aparte del perfil |
| **Comentario** | El texto que acompaña a tu baliza |
| **Latitud** y **Longitud** | La posición fija del repetidor |
| **Consultas** | Si contesta a las preguntas que le hagan por radio |
| **TNC usb** | **Apagado**, **TNC2 texto** o **KISS**, para usarlo como módem |

### Bluetooth

| Opción | Qué hace |
|---|---|
| **Bluetooth** y **PIN BT** | **No hacen nada.** El Bluetooth está fuera de esta versión |

### Sensores

| Opción | Qué hace |
|---|---|
| **Enviar WX** | Mandar el paquete de meteorología. **Viene activado** |
| **Enviar telem** | Mandar la telemetría. **Viene activada** |
| **Telem cada (min)** | Cada cuánto. **0 = solo a mano**; si no, de 15 a 720 |
| **WX cada (min)** | Cada cuánto la meteorología. **0 = solo a mano**; si no, de 15 a 720 |
| **Corr sonda** | Ajuste de la sonda exterior de temperatura |
| **Ajuste chip** | Ajuste del sensor de dentro de la caja |

### Pantalla

| Opción | Qué hace |
|---|---|
| **Auto-avance** | Que las escenas vayan rotando solas cada 45 segundos |
| **Apagar pantalla(s)** | **En esta pantalla no hace nada** (la tinta no se apaga por tiempo: la imagen se queda). El menú lo marca con un asterisco |
| **Avisos** | **En esta pantalla no hace nada**: los avisos de tráfico salen siempre. También va marcado con un asterisco |

### Energia

| Opción | Qué hace |
|---|---|
| **Corte (mV)** | Tensión por debajo de la cual se duerme para proteger la batería |
| **Despertar (mV)** | Tensión a la que vuelve a arrancar |
| **Ver bateria** | Enseña la tensión medida |
| **Dormir** | Apaga el aparato. **En el T-Echo no pide confirmación** (la propia elección de la fila es la confirmación, decisión del 2026-09-16); en la Faketec sí |

### Remoto

| Opción | Qué hace |
|---|---|
| **Control remoto** | Aceptar órdenes por radio. **Viene apagado** |
| **Operadores** | La lista de indicativos autorizados |
| **Silenciar/act** | Cambia el silencio de la radio |

### Ajustes

| Opción | Qué hace |
|---|---|
| **Reiniciar** | Reinicia el aparato (pide confirmación) |
| **Valores fabrica** | Vuelve a los valores de origen. **Conserva tu indicativo** (pide confirmación) |
| **Borrado** | Borra todo, incluido el indicativo (pide confirmación) |

Además, en el **menú principal** (antes de las secciones) hay una fila **Dormir**, que
apaga el aparato sin entrar en la sección de energía **y sin pedir confirmación** (decisión
del 2026-09-16: elegir esa fila ya es confirmar; dormir no borra nada y se sale con el
reset), y una fila **Salir** arriba y otra a media lista.

> **El asterisco.** En las secciones donde hay algún ajuste que **en esta pantalla no
> hace nada** (Pantalla y Bluetooth), el menú lo marca con un **asterisco** y lo explica
> en la leyenda. Es a propósito: antes se podían tocar y parecía que habían cambiado
> algo. Se dejan a la vista para que el menú sea igual que el de la otra placa, pero
> avisando.

## Cómo se navega y cómo se cambian los valores

- **Táctil**: mueve el cursor por la lista, y **cambia el valor** cuando estás dentro de
  una opción.
- **Físico, toque corto**: entra en la opción y **confirma**. En un texto, pasa a la letra
  siguiente.
- **Físico, toque largo**: **vuelve atrás** un nivel. Si estabas escribiendo, guarda lo
  escrito.
- En las opciones de elegir entre varias (el modo, la frecuencia, el ancho de banda) se
  abre una **lista con todas las opciones**, la que está puesta marcada y las filas
  **Volver** y **Salir** arriba.
- Los **números y las listas se guardan al momento**: cada toque del táctil cambia el
  valor **y lo deja guardado**, no hay que confirmar nada.
- Los **textos** (el indicativo, el comentario) se escriben letra a letra: el táctil
  cambia la letra, el físico corto pasa a la siguiente posición y el físico largo guarda
  y sale.
- **El editor de perfiles** funciona igual: en la lista de perfiles, el táctil se mueve y
  el físico corto **elige el perfil** (queda activo al momento); en **Editar ajustes**,
  el táctil cambia el valor del campo y el físico corto pasa al campo siguiente (SSID,
  tiempo lento, tiempo rápido, metros e **Icono**). El campo **Icono** va recorriendo los
  cuatro iconos y enseña el nombre y el par de códigos APRS, por ejemplo
  **`Icono: Persona (/[)`**.

## Las acciones peligrosas piden confirmación: dos pulsaciones cortas

**Dormir NO está en esta lista** (decisión del 2026-09-16): si eliges la fila **Dormir**, el
nodo se duerme y ya. Aquí se hace de otra forma que en la OLED, porque la pantalla es lenta,
y solo lo piden las acciones que **borran o reinician**: **Reiniciar**, **Valores fabrica**
y **Borrado**:

1. La primera pulsación **corta** del físico **no ejecuta nada**: la pantalla se llena con un
   aviso que pone **CONFIRMAR**, el nombre de la acción y **«pulsa corto otra vez para
   confirmar»**.
2. Hay que **volver a pulsar corto**, dentro de los **15 segundos** siguientes. Entonces se
   ejecuta. Deja pasar medio segundo entre las dos, o el nodo lo leerá como un **doble toque**
   (que dentro del menú no hace nada: te quedarás en el aviso).
3. **Una pulsación larga mientras está el aviso CANCELA**: no hace nada y vuelve a la
   lista. Si te has equivocado, esa es la salida.
4. Mientras el aviso está en pantalla, **el táctil no hace nada** (para que no muevas el
   cursor sin querer).
5. Si dejas pasar los 15 segundos, el aviso desaparece solo y no pasa nada.

> **OJO: esto estuvo mal escrito hasta el 2026-09-16.** La pantalla y este manual decían
> «pulsa **largo** otra vez», copiado de la OLED, donde el gesto que confirma **sí** es el
> largo. En el T-Echo el mapa es el contrario —**corto = entrar y confirmar; largo = volver
> atrás**—, así que el aviso mandaba hacer justo el gesto que cancela (y se contradecía con su
> propia última línea, «físico largo: cancelar»). Lo cazó el operador: «mantengo pulsado y solo
> sale del menú». Ya dice lo que hace.

## El menú se cierra solo a los 15 segundos

Si no tocas nada durante **15 segundos**, el menú se cierra y la pantalla vuelve al
carrusel de escenas. Es el mismo tiempo que dura el aviso de confirmación, así que el
aviso nunca se queda más rato que el menú.

## La luz de la pantalla

La luz de fondo se enciende **5 segundos** cada vez que tocas un botón, y se apaga sola.
En reposo está apagada, que es como menos gasta.

## La orientación del texto

Si tu unidad trae el panel montado girado, el texto se puede girar. Se ajusta en el
configurador web, en **«Orientación de la pantalla»**, y se aplica al guardar, **sin
reiniciar**: **1** es la posición natural (de pie, y es la que viene de fábrica), **2** es
girada 90 grados, **3** es boca abajo y **0** es girada 270 grados.

---

# Avisos por vibración y por sonido (solo T-Echo Plus)

El **T-Echo Plus** lleva un **motor de vibración** y un **zumbador**. En las demás placas
no hay nada de esto, y no pasa nada: simplemente no avisan.

> **Por qué existen**: la pantalla de tinta **no se ilumina** y tarda un par de segundos
> en cambiar. Hay cosas que, sencillamente, **no se pueden ver**: que el aparato ha
> arrancado, que has acertado al tocar, o que te acaba de llegar un mensaje. Para eso
> está la vibración.

## Los avisos por vibración

Cada aviso tiene **su propio patrón**, para que los reconozcas de memoria sin mirar:

| Cuándo | Cómo se nota | Para qué sirve |
|---|---|---|
| **Al arrancar** | Un golpe suave | Saber que ha arrancado, sobre todo al despertarlo con el botón de reset, que no da ninguna otra señal |
| **Al tocar el botón táctil** | Un clic seco y corto | Saber que el toque ha contado, sin esperar a que la pantalla cambie |
| **Al coger la posición del GPS** | Dos golpes | Saber que ya tiene posición |
| **Al recibir un mensaje** | Tres golpes | Que te han escrito |
| **Al llegar el acuse de un mensaje tuyo** | Un zumbido corto | Que tu mensaje ha llegado |
| **Al preguntarte por radio** | Una alerta larga | **Encontrar el aparato cuando no sabes dónde está**: le preguntas por radio desde otro equipo y el nodo avisa donde esté |

Cuatro detalles que conviene saber:

- **El aviso del GPS solo suena al coger la posición**, no al perderla, y **como mucho
  una vez por minuto**. En el monte o entre edificios el GPS coge y pierde la señal todo
  el rato, y no puede estar vibrando sin parar.
- **El aviso de mensaje no suena con los acuses ni con las preguntas**: esos tienen el
  suyo, precisamente para distinguirlos.
- **El aviso de «búscame» suena aunque el aparato tenga las consultas desactivadas.** Si
  alguien le pregunta, vibra igual: para eso está, para encontrarlo.
- **Cuando vibra el aparato no se queda tonto**: el motor hace su trabajo por su cuenta
  mientras el nodo sigue atendiendo la radio y los botones.

## Los avisos por sonido

- **Un pitido corto cada vez que pulsas un botón** (el físico o el táctil). Es corto y no
  muy agudo, para que no moleste.
- **Una melodía de batería baja antes de apagarse.** Cuando la batería está en las
  últimas, el aparato suena con una melodía descendente —la de los avisos de batería de
  toda la vida— **antes** de dormirse. Así te enteras aunque no estés mirando la pantalla,
  que es justo lo que hace falta cuando se apaga solo en mitad del monte.

---

# La posición: el GPS en la práctica

## La antena es lo más importante

El GPS necesita **ver cielo**. No es como la radio de 433: **es otra antena**, más
pequeña, y si no está bien puesta el receptor queda sordo del todo. Dentro de casa puede
tardar **minutos** o no fijar nunca, aunque esté en una ventana.

## En frío y en caliente

- **En caliente** (el GPS guarda datos de órbita recientes): fija en **segundos**.
- **En frío** (sin datos, por mucho tiempo apagado): hay que descargar el almanaque
  entero, y eso son **minutos** con cielo abierto.

## Cuando no fija, ¿está roto?

No necesariamente. Mira en el menú **«Ver GPS»**: si dice **satélites a la vista** mayor
que cero, el receptor **oye** satélites y el problema es de cielo o de tiempo. Si dice
**cero** y estás al aire libre con la antena puesta, entonces sí hay algo que revisar.

## Fijar la posición del repetidor con el GPS (útil en el monte)

**El caso**: vas a colocar el repetidor en un sitio nuevo y quieres que su posición sea
exactamente donde lo dejas, sin teclear coordenadas.

- **En la Faketec** está en el menú, en **«Fijar coords actuales»**, y el aviso va
  contando el proceso: **`Buscando GPS...`** con los satélites a la vista, luego
  **`GPS 7/20 asentando`**, y al terminar **`Guardado <latitud> <longitud>`**. Si no hay
  fijación, el aviso se queda ahí y **se actualiza cada segundo** con los satélites que ve:
  así sabes que el aparato sigue trabajando aunque tarde.
- **En el T-Echo y T-Echo Plus** está en el menú, en **«Fijar coords»**. La pantalla se
  pone entera para la ocasión y va diciendo **`Buscando GPS...`** (con los satélites a la
  vista), después **`Asentando...`** con una barra de progreso y **`Muestra 7/20`**, y al
  terminar **`GUARDADO`** con la latitud y la longitud. Esa pantalla **se queda fija**
  —la tinta no gasta por quedarse quieta— y **el primer toque de botón la quita**, así que
  puedes mirar el resultado con calma. Mientras busca o asienta, en esa misma pantalla pone
  **`Toca un botón para cancelar`**: si no quieres esperar más, un toque y se acabó.

Con una pulsación hace **todo** el trabajo: enciende el GPS aunque estuviera apagado,
espera a que fije **sin tope de tiempo**, deja que la posición se asiente (**20 lecturas
seguidas**, unos 20 segundos, porque justo después de fijar el receptor da saltos de
decenas o cientos de metros), guarda **la última válida** como posición fija del aparato y
**vuelve a apagar el GPS** para no gastar batería.

**Funciona en cualquier modo de trabajo**, también en Repetidor y con «GPS en repetidor»
apagado: **el GPS lo enciende y lo apaga la propia operación**, sin que tengas que tocar
ningún ajuste antes ni después.

**Se puede cancelar.** Como la fijación puede tardar lo que necesite, no te quedas
atrapado: **mientras la pantalla dice `Buscando GPS...` o `Asentando...`, un toque de
botón cancela** la operación, apaga el GPS (si lo había encendido ella) y vuelve al menú.
Se cancela sin guardar nada. En la tinta el aviso lo dice: **`Toca un botón para cancelar`**.

**Tiempo esperable**: menos de un minuto si el GPS trae datos recientes; **2-3 minutos** en
frío, y **puede ser más** si el cielo está tapado (debajo de un árbol, entre edificios, con
el aparato dentro de una caja metálica). Que tarde **no es un fallo**: mira los satélites a
la vista, que es lo que te dice que el receptor está trabajando. Si quieres parar, cancela.

> El encendido del GPS de esa sesión es **temporal y no se guarda**. Si el nodo se
> reinicia a mitad, el GPS vuelve apagado: no se queda chupando batería sin que nadie lo
> haya pedido.

> **Si ya tenías el GPS encendido** por otro motivo (modo Rastreador, «GPS en repetidor»
> activado, «Ahorro de GPS»), la operación lo usa pero **no lo apaga al terminar**: lo deja
> como estaba. El ahorro de batería del repetidor no se toca.

## «Ahorro de GPS» y «GPS en repetidor»

Estas dos opciones **solo actúan en modo Repetidor**. Allí el GPS se apaga y se enciende
por ciclos para no gastar batería, o se usa la posición del GPS en vivo en vez de la
fija. **En Rastreador y en Ambos el GPS va siempre encendido**: apagarlo le hace perder
los satélites y volver a empezar.

> **En la OLED estas dos opciones se esconden** cuando no sirven (en Rastreador y en
> Ambos), para que no toques algo que no hace nada. **En el T-Echo salen siempre**, pero
> solo tienen efecto en modo repetidor.

> **Estas dos opciones NO hacen falta para «Fijar coords».** Esa operación enciende y
> apaga el GPS por su cuenta, en cualquier modo: si te dijera que activaras «GPS en
> repetidor» sería pedirte a mano algo que el aparato hace solo. Lo único que cambia es
> que, si el GPS ya estaba encendido por estas opciones, la operación lo deja encendido
> al terminar (no te lo apaga por sorpresa).

---

# Rutas y saltos: si te repiten o no

Un nodo que emite **puede pedir** que otros lo repitan. Eso se hace con la **ruta** (en
inglés, *path*), y es una petición, no una orden.

| Ruta | Qué pide |
|---|---|
| **Vacía** o «0» | **Que no te repita nadie.** Solo te oyen los que están a tiro directo |
| **WIDE1-1** | Un salto |
| **WIDE1-1,WIDE2-1** | Dos saltos |

El firmware trae **tres rutas separadas**, una por modo (repetidor, rastreador y ambos),
porque no conviene pedir lo mismo desde un sitio fijo que desde uno que se mueve.

**Recomendaciones de la red**:

- **Estación fija**: un salto (`WIDE1-1`), o ninguno si tienes un iGate cerca.
- **Móvil**: dos saltos (`WIDE1-1,WIDE2-1`) si vas por zonas sin cobertura.
- **Nunca tres o más**: no aporta y gasta el canal de todos.

> **Cada salto multiplica el aire que gastas.** Es la regla práctica que resume todo este
> capítulo: si pides dos saltos, la trama se emite tres veces (la tuya y las dos copias).
> Por eso, cuando se piden dos saltos, conviene **subir el tiempo entre balizas**.

No confundas dos cosas que van separadas:

- **La ruta** decide si **te repiten a ti**.
- **El modo repetidor** decide si **repites tú** a los demás.

---

# Balizas, telemetría y meteorología

## La baliza de posición

Es el mensaje que dice dónde estás. Lleva:

- La **posición** (o la posición fija, según el modo).
- La **hora del GPS**, el **rumbo**, la **velocidad** y, si quieres, la **altura**.
- El **símbolo** que elijas (coche, persona, bici, repetidor...), para que en el mapa se
  vea de un vistazo qué eres.
- Un **comentario** tuyo, si lo rellenas.
- Los datos de los **sensores** que tengas, en texto legible: por ejemplo
  `H37% 26.1C 969.0hPa`.
- La **tensión de la batería**.
- El **aviso de batería baja** cuando toca.

**Cada cuánto sale**: **30 minutos** de fábrica, y **se puede cambiar** (mínimo 15
minutos). Los motivos de esos números están explicados en el capítulo de los tiempos de
emisión.

## Lo bien que se porta cuando estás parado

Parado, el nodo **sigue publicando su posición cada 15 minutos**. Son unas dos tramas por
hora, ocupación mínima del canal, y sirve para que el mapa no se quede con una posición
vieja. Se salta a propósito el filtro de distancia, porque un nodo parado nunca recorre
esa distancia.

Y al revés: **no manda balizas fantasma por un salto tonto del GPS**. El disparo por
distancia exige **movimiento sostenido de verdad**, así que el ruido del receptor no gasta
aire.

## La meteorología

Cada **55 minutos** (de fábrica; se puede cambiar, y 0 significa «solo a mano») el nodo
manda un **paquete de tiempo** en el formato estándar de APRS, **sin posición**, para no
cambiar tu símbolo. Eso es lo que hace que aprs.fi y findu te reconozcan como **estación
meteorológica** y dibujen las gráficas históricas. Solo se manda cuando el GPS da la
hora: **sin sello de tiempo no se manda**, porque un sello falso es peor que no mandar
nada.

**Viene activada de fábrica.** Si no tienes sonda de clima, no se inventa nada: se manda
lo que haya.

## La telemetría

Cada **53 minutos** (de fábrica; también se puede cambiar) el nodo manda **telemetría
estándar** con canales y unidades (tensión, corriente, temperatura, humedad y presión).
Es lo que hace que aprs.fi dibuje las gráficas. Los nombres de los canales se ajustan
solos a los sensores que tengas: si no hay sonda de temperatura, ese canal se llama `na`
y **no se manda un cero falso**.

## El aviso de batería baja

Cuando la batería baja de un umbral, la propia baliza lo dice en su texto (`BAT BAJA`),
para que se vea en el mapa sin abrir gráficas. Y si está a punto de dormirse, añade
`DORMIR`. **Con el USB enchufado no avisa**, porque con el cargador la tensión no dice
nada del estado real. En el T-Echo Plus, además, **suena la melodía de batería baja**
antes de apagarse.

## Lo que manda por su cuenta

Además de las balizas, la telemetría y la meteorología, el nodo manda unos cuantos
paquetes por decisión propia. Conviene saberlos, para no extrañarse al verlos en un
visor:

- **Aviso de arranque**, poco después de encenderse, para que se sepa que está vivo. Si
  está en modo Rastreador o Ambos y el GPS todavía no ha fijado, ese aviso dice que está
  **buscando satélites** (y no lleva posición, claro).
- **`GPS OK`**, una sola vez, en cuanto el GPS fija. Sirve para que el mapa deje de
  mostrar «buscando satélites» cuando ya hay posición.
- **Mensaje de estado**, al arrancar y luego una vez al día, si has escrito uno.
- **`NODO ONLINE`** cuando vuelve de dormirse por batería baja: así se sabe que ha
  despertado solo.
- **Avisos de batería baja y de que se va a dormir**, con su propio texto.

> **Todos estos paquetes pasan por la misma puerta que las balizas**, así que **sin
> indicativo no sale ninguno**.

---

# Mensajes, boletines y objetos

## Mensajes a otra estación

El nodo puede mandar **mensajes de texto** a otra estación, como un SMS por radio. Se
manda desde el configurador web, por comandos, o desde la propia pantalla del nodo.

- Si el otro **contesta con un acuse**, el nodo te lo avisa en pantalla (y en el T-Echo
  Plus, con una vibración distinta).
- Si **no contesta**, el nodo **reintenta** el mismo mensaje cada 30 segundos (3 veces de
  fábrica) y luego se rinde y lo apunta.
- Los mensajes que **te llegan** salen en pantalla y quedan en el registro.

Puedes guardar **un mensaje fijo** («mensaje rápido») y mandarlo al último que hayas oído
con un par de toques, sin escribir nada.

## Boletines

Un boletín es **un mensaje para todos** (`BLN0` a `BLN9`). Útil en una quedada o en un
evento para avisar a todo el mundo a la vez sin repetir el mensaje estación por estación.

## Objetos

Un objeto es **un punto con nombre en el mapa de los demás**: un puesto de control, un
repetidor portátil, un campamento. Se publica y se borra cuando quieras.

> Los objetos necesitan **hora del GPS**. Si no hay fijación, el nodo no los manda: un
> sello de tiempo falso es peor que no mandarlo.

---

# El registro de viaje

El nodo guarda en su memoria interna **todo lo que pasa**: balizas enviadas, tramas
repetidas, recepciones y eventos, con la **hora y fecha del GPS**.

- **Sobrevive a los apagones**: si se corta la corriente mientras escribe, no se corrompe.
- **Se recicla solo**: al llenarse, borra lo más antiguo y sigue grabando.
- **Se activa en Rastreador y en Ambos** (y el modo de fábrica es Ambos, así que viene
  encendido). En Repetidor puro está apagado a propósito, para que el nodo fijo de casa
  no gaste memoria.

## Cada punto dice por qué salió

En el registro, cada baliza de posición anota **el motivo**:

| Letra | Significa |
|---|---|
| **F** | Primera baliza al fijar (arranque de la sesión) |
| **R** | Por tiempo (le tocaba) |
| **C** | Por un giro |
| **D** | Por distancia recorrida |
| **M** | A mano (se la pediste tú) |
| **P** | Parado (la baliza lenta de los 15 minutos) |
| **L** | Es la **última posición conocida** (histórica, no de ahora) |

Así, al exportar la ruta, **un punto histórico no se confunde con un punto nuevo**.

## Descargarlo y verlo en un mapa

Desde la pestaña **«Registro de viaje»** del configurador:

- Se descarga lo que el nodo guardó, **se ve el recorrido en un mapa** y se puede
  exportar a **GPX, KML o CSV**, listo para Wikiloc, Google Earth, Garmin o Strava.
- El registro se parte **por sesiones** (un recorrido por cada encendido), y el fichero
  sale con nombre **`INDICATIVO_AAAAMMDD_HHMM`**, así que se sabe de qué paseo es.
- El mapa se maneja como cualquier mapa: se arrastra, la rueda acerca, hay botones
  **+ / −**, doble clic acerca donde pinchas, y el botón **«Ajustar a la ruta»** la
  encuadra entera. El principio sale con **A** (verde) y el final con **B** (rojo).

> **El mapa es propio, sin librerías de internet.** Es a propósito: este nodo vive en el
> monte y en casas sin cobertura, y no tiene sentido que el programa dependa de que hoy
> haya conexión para enseñarte tu ruta. **Internet solo hace falta para las fotos del
> terreno**; sin conexión, la ruta se ve igual sobre un fondo con rejilla.

---

# Autonomía y sueño

## Dormir a mano desde el menú

En el menú, **Energía → Dormir** (en el T-Echo hay además una fila **Dormir** en el menú
principal). En la Faketec pide confirmación, como las acciones peligrosas; **en el T-Echo no:
elegir la fila es la confirmación** (2026-09-16).

**Con el cable USB puesto, el aparato NO se duerme.** Avisa en pantalla —«solo sin cable
USB»— y vuelve a lo que estaba. Es a propósito: un nodo dormido con el cable puesto no se
podría tocar ni grabar sin levantarse a pulsar el reset, y eso es incómodo y se presta a
sustos.

**Sin cable, se apaga de verdad.** Y aquí hay algo que **hay que saber de antemano**:

> **Para despertarlo hay que pulsar el botón de RESET de la placa.**
>
> En los **T-Echo y T-Echo Plus**, el botón de usuario **no lo despierta**: se dejó así a
> propósito, porque al despertar el chip arranca de cero y lo primero que hace es mirar
> si el botón sigue pulsado... y como acabas de pulsarlo tú para despertarlo, entraba en
> modo de grabación en vez de arrancar. Con el **reset** arranca limpio siempre.
>
> En las **Faketec**, en cambio, **el botón sí lo despierta**.
>
> Y en todas, si la batería sube de tensión (por ejemplo, porque empieza a cargar el
> panel solar), **vuelve a arrancar solo**.

## Se duerme solo cuando la batería baja

Si la batería baja de un umbral, el nodo **se apaga solo** para no dañarla, y **vuelve a
arrancar cuando la tensión sube** (por ejemplo, cuando el panel solar carga). No hace
falta que nadie pulse nada.

- **Nunca se duerme si está enchufado al USB.**
- Y **nunca se duerme por un dato falso**: lee la batería cada 20 segundos y exige
  **ocho lecturas seguidas** por debajo del umbral antes de decidir. Es lento a propósito:
  con radiofrecuencia cerca, el divisor de tensión puede dar valores irreales, y la
  prioridad es que una lectura falsa **no** duerma el nodo.
- **Avisa antes de dormirse**: manda una baliza con el cartel correspondiente, la
  pantalla lo dice, y en el T-Echo Plus **suena la melodía de batería baja**. Al volver,
  avisa también.

## Dormir entre balizas

En modo **Rastreador** (no en Ambos) hay una opción avanzada: **dormir entre balizas**.
El nodo se apaga entre una baliza y la siguiente, para durar mucho más en una excursión
larga. **En modo Ambos no se duerme**, porque el repetidor no puede estar sordo. Y
tampoco se duerme si está enchufado al USB.

## Los umbrales

Los umbrales de apagado y despertar se ajustan en el menú y en el configurador. Los
valores de fábrica valen para **batería de litio de 1 celda y para 3 pilas NiMH** en las
Faketec, y para la celda de litio de los T-Echo, sin tocar nada. Para otros montajes hay
que ajustarlos.

> **Aviso**: los umbrales son **de seguridad para la batería**, no una medida exacta de
> carga. Si ves que el nodo se duerme antes de lo que esperabas, sube o baja el umbral
> según tu montaje. Y recuerda el capítulo de hardware: **si el divisor de la batería no
> está bien, la medida no vale y esta protección no funciona**.

---

# El nodo como módem (TNC)

El nodo puede hacer de **módem** para un programa de APRS que tengas en el ordenador o en
el móvil. Hay un selector de **tres posiciones**:

| Ajuste | Qué hace |
|---|---|
| **Apagado** | El nodo funciona por su cuenta, como siempre |
| **TNC2** | Manda las tramas en **texto** (para programas antiguos) |
| **KISS** | Manda las tramas en **binario**, que es lo que piden casi todas las apps modernas (APRSdroid, APRSIS32...) |

**Muy importante en modo KISS**: el nodo **se calla**. Deja de mandar sus propias balizas,
telemetría y meteorología, porque en ese modo **manda el programa** que tengas conectado.
El repetidor sigue igual, y lo que le pidas a mano (botón, menú, configurador) sigue
funcionando. **El cambio necesita reiniciar el nodo.**

La consola y el configurador siguen funcionando en los tres modos, así que **nunca te
quedas sin poder cambiarlo**.

---

# El control remoto por radio

El nodo admite **órdenes por radio**, mandadas como mensaje APRS desde otro equipo. Por
seguridad **viene apagado** y hay que poner una **lista de operadores autorizados**: sin
esa lista, el nodo no obedece a nadie por radio.

Con el control remoto activo se puede, por ejemplo, pedirle que mande una baliza, que
diga cómo está, o preguntarle qué ha oído. Puede resultar muy útil para un nodo en el
monte al que no quieres subir a tocarlo.

Las **consultas** son otra cosa: son preguntas sueltas (por ejemplo, «¿dónde estás?») que
cualquiera puede hacerle **si las tienes activadas**. Vienen apagadas. Curiosidad útil:
aunque estén apagadas, **si alguien le pregunta, el T-Echo Plus vibra** con la alerta
larga, que es justo lo que se quiere para encontrar el aparato.

> **Recomendación**: activa el control remoto solo si lo vas a usar, y con la lista de
> autorizados bien puesta. Es una puerta abierta a tu nodo.

---

# El configurador web

Es **una sola página** que se abre en **Chrome o Edge** y habla con el nodo por el cable
USB. Tiene cuatro pestañas: **Estado en vivo**, **Configuración**, **Acciones** y
**Registro de viaje**.

## Qué se puede hacer desde él

- **Ver el estado en vivo** del nodo, y una **consola** donde se ve lo que el nodo contesta
  y lo que avisa (las órdenes se mandan con los botones de la página; en la consola no se
  escribe).
- **Encender y apagar el modo diagnóstico** con el botón **Diagnóstico**. Apagado (como viene),
  la consola solo enseña lo que se le pide; encendido, el nodo cuenta además lo que va haciendo
  por dentro (cada repintado de la pantalla, los cambios de diapositiva, el menú, «fijar
  coords»…), que es lo que hace falta cuando algo no va y hay que mirarlo con lupa. **Es de
  usar y tirar**: se apaga solo al reiniciar el nodo, y en modo módem (TNC) el nodo lo silencia
  él solo, porque el puerto es entonces del programa de APRS.
- **Leer la configuración de verdad** que tiene el nodo y **rellenar el formulario** con
  lo que necesites: la estación (tu indicativo, el comentario), las rutas y saltos, las
  balizas, el repetidor, la radio, los sensores y la telemetría, el rastreador, la
  pantalla, la energía y el control remoto.
- **Guardar** y ver **los valores que el nodo ha aceptado** (si ha recortado alguno, lo
  verás).
- **Guardar y recuperar la configuración en un fichero** (exportar e importar), para
  tener una copia o para repetirla en otro nodo.
- **Acciones sueltas**: mandar una baliza, mandar telemetría, mandar el formato de
  telemetría, leer el estado y **silenciar o reactivar** la radio.
- **Mandar mensajes**, **boletines** y **objetos** sin escribir comandos.
- **Cambiar los perfiles de uso**: hay una sección **«Perfiles de uso»** con la tabla de los
  cuatro (SSID, tiempos, metros e **icono del mapa**), con el mismo selector de iconos de
  siempre. Así no hace falta ir al menú del T-Echo para tocar los perfiles.
- **Descargar el registro de viaje**, verlo en el mapa y exportarlo a GPX, KML o CSV.
- La **zona delicada**: reiniciar, volver a valores de origen (borra la configuración,
  **no** reinicia), borrado total y modo de grabación. **Todas piden confirmación
  escrita**, para que no se hagan de un clic.

## Qué NO se puede hacer desde él

- **No se puede probar el zumbador ni la vibración.** Son avisos automáticos: no tienen
  ajustes ni botón de prueba en la web.
- **La sección de Bluetooth no hace nada.** Está a la vista, pero el Bluetooth está fuera
  de esta versión, y la propia página lo avisa.
- **Los ajustes de radio** (frecuencia, velocidad, ancho de banda y potencia) **no se
  aplican al guardar**: necesitan **reiniciar** el nodo. La página también lo dice.
- **No se puede usar en Firefox ni en Safari**, porque no tienen la función que hace
  falta para hablar por USB.
- **No se puede configurar el nodo por radio ni por wifi**: siempre por el cable.

> **Un detalle del formulario de la baliza**: el mínimo de la baliza de posición son
> **15 minutos**, igual que en el menú del aparato. Por debajo de eso, el nodo **rechaza
> el valor** al guardar.

> **Si el nodo no entiende un valor, ahora lo dice.** Antes, una clave conocida con un
> valor de un tipo que no le encaja (por ejemplo `beaconInterval` con el texto «cada media
> hora», o una latitud escrita como «42N») **se descartaba en silencio**: el nodo
> contestaba que todo había ido bien y el ajuste se quedaba como estaba. Ahora esos casos
> **se avisan**: el mensaje de vuelta dice **`ignorado (tipo incorrecto):`** y la lista de
> claves que no se han podido leer, y queda también anotado en el registro del viaje
> (`log dump`). El resto de la configuración sí se aplica: lo que no encaja **no se cambia
> y no se calla**.

## El mapa del recorrido y el servidor local

Para ver **las fotos del terreno** en el mapa del recorrido, la página tiene que estar
servida desde un servidor local, no abierta haciendo doble clic en el fichero. El motivo
es del servidor de mapas: exige que las peticiones vengan de una dirección web y
**prohíbe el uso sin conexión**, así que desde un fichero no manda las fotos. No es un
fallo del programa y no se debe burlar.

La solución es servir la página en local, desde la carpeta del proyecto:

```
powershell -File tools\sirve_web.ps1
```

y abrir **`http://localhost:8000/web/`** en Chrome o Edge. **La ruta se ve igual aunque no
haya fotos**, sobre un fondo con rejilla.

## Los avisos del formulario

Cuando cambias cualquier cosa, el configurador **avisa** de las cosas que no se hacen
bien: un nodo sin indicativo, coordenadas a 0,0, una frecuencia que no es la de la red,
una ruta con demasiados saltos para un nodo fijo, una baliza demasiado frecuente...
También avisa de **ajustes que no van a hacer nada** en el modo que tienes puesto.

**Los avisos no te impiden guardar.** Son consejos: si sabes lo que haces, puedes guardar
igual. Lo que sí bloquea el guardado es un valor fuera de rango, que lo rechaza el propio
nodo.

> **El indicativo y las coordenadas son lo primero que hay que rellenar.** Un nodo sin
> indicativo **no transmite nada**, y con las coordenadas a cero aparece en el mapa en
> medio del Atlántico.

---

# Cómo se actualiza el firmware

## A mano, con la unidad de memoria

1. **Dos toques seguidos al botón de reset** de la placa (en el T-Echo, el de arriba a la
   izquierda) hasta que aparezca una **unidad de memoria** en el ordenador.
2. Se copia el fichero del firmware (`.uf2`) a esa unidad.
3. Se espera a que la unidad desaparezca y el nodo arranca solo.

> **Antes de grabar nada, copia lo que traía la placa.** La unidad expone un fichero con
> **todo** lo que lleva grabado, así que siempre se puede volver atrás.

## Qué fichero va en cada placa

Los ficheros se descargan de la carpeta **`firmware/release/`** del repositorio del proyecto.
Estos son los nombres exactos, y **estos ocho son todos los que hay**:

| Placa | Fichero | Ojo con |
|---|---|---|
| Faketec con HT-RA62 | `KachoSystem_v1.0alpha_b13_Faketec_HT-RA62_433.uf2` | — |
| Faketec con E22P | `KachoSystem_v1.0alpha_b13_Faketec_E22P-433M30S.uf2` | Si grabas el otro, la potencia y el encendido del módulo no son los correctos |
| T-Echo **o T-Echo Plus** con cargador de arranque **versión 6** | `KachoSystem_v1.0alpha_b3_LilyGO_T-Echo.uf2` | Es el último que hay para la versión 6: **no existe uno más nuevo** (ver abajo) |
| T-Echo con cargador de arranque **versión 7** | `KachoSystem_v1.0alpha_b13_LilyGO_T-Echo_S140v7.uf2` | Si te equivocas, se pisan los últimos 4 KB del cargador y la radio deja de funcionar |
| T-Echo **Plus** con cargador de arranque **versión 7** | `KachoSystem_v1.0alpha_b13_LilyGO_T-Echo-Plus_S140v7.uf2` | Es el que lleva los avisos de vibración y sonido |

> **Los cuatro ficheros viejos siguen en la carpeta a propósito**, no son basura que se haya
> olvidado: `KachoSystem_v1.0alpha_b3_Faketec_HT-RA62_433.uf2`,
> `KachoSystem_v1.0alpha_b3_Faketec_E22P-433M30S.uf2`,
> `KachoSystem_v1.0alpha_b3_LilyGO_T-Echo-Plus.uf2` y
> `KachoSystem_v1.0alpha_b3_LilyGO_T-Echo.uf2`. Los tres primeros los han sustituido los `b13`
> de arriba para esas mismas placas; **el cuarto, no**: es el que sigue valiendo para un T-Echo
> con cargador **versión 6**, porque **no hay build `b13` para la versión 6**. Si tu T-Echo trae
> la versión 6 y quieres lo último, hay que **actualizar antes el cargador de arranque** a la
> versión 7, y eso no lo hace este proyecto.

**Cómo saber qué versión de cargador de arranque trae tu T-Echo**: entra en modo
grabación (dos toques al reset), abre la unidad y mira el fichero **`INFO_UF2.TXT`**. Si
pone **`SoftDevice: S140 version 7.x`**, te toca el firmware de la versión 7. Si pone
**6.1.1**, usa el `..._b3_LilyGO_T-Echo.uf2`. **No son intercambiables.**

> **El fichero de la versión 6 vale para las dos placas** (T-Echo y T-Echo Plus): el pinout es
> el mismo, así que cruzarlos no rompe nada. Lo que **no** lleva, por ser más antiguo, es el
> **menú y las escenas de la pantalla de tinta electrónica** ni los **avisos de vibración y
> sonido del Plus**.

## El atajo de los 1200 baudios

Estas placas llevan el cargador de arranque del mundo Arduino, que conserva una
costumbre: si alguien abre su puerto serie **a 1200 baudios**, la placa lo entiende como
«quiero grabar» y **se reinicia sola en modo grabación**. La página del flasher tiene un
botón para probarlo. **En algunos ordenadores (y en Windows en particular) eso da error de
dispositivo y no entra**; no es un fallo de tu nodo, es del controlador del puerto de ese
PC. **Si no entra, los dos toques al reset funcionan siempre.**

## Si algo sale mal y el nodo no arranca

Dos toques al reset y copiar un firmware bueno conocido. Ese es el rescate, y funciona
aunque la aplicación esté rota. En las placas de este proyecto hay además **un firmware
mínimo de prueba de la pantalla**, que sirve para comprobar que el panel está bien
cuando se sospecha de él.

---

# Recomendaciones y buenas maneras

Esto no son reglas del programa: son las costumbres de la red, y seguirlas hace que todos
oigamos mejor.

1. **No ocupes el canal.** Es compartido y de tipo «el que habla primero, gana». Si todos
   hablamos mucho, perdemos todos.
2. **Respeta los tiempos mínimos.** La baliza de posición no baja de 15 minutos en un
   nodo fijo, y en movimiento no conviene bajar de 30 segundos entre balizas.
3. **Recuerda que el nodo se queda sordo mientras transmite**: cada paquete son unos
   segundos en los que no oye a nadie.
4. **Pide los saltos que necesites, no más.** Cada salto multiplica el aire.
5. **No repitas con dos nodos el mismo indicativo**: se producen duplicados.
6. **Mira el uso del aire de tu zona** antes de poner cadencias agresivas. En una comarca
   vacía no molesta a nadie; en una ciudad, sí.
7. **Identifícate siempre.** Es una obligación legal y una cortesía. Además, el aparato
   no emite nada hasta que le pongas un indicativo.
8. **No uses el indicativo ni la clave de otra estación.** Nunca. Ni para leer.

---

# Preguntas frecuentes

**No aparezco en el mapa.**
Comprueba por este orden: (1) que le has puesto **tu indicativo** (sin él no emite
nada); (2) que el GPS tiene fijación; (3) que hay un iGate oyéndote; (4) que no estás en
modo rastreador **sin** fijación, porque entonces el nodo **a propósito** no manda
posición, solo el aviso.

**El aparato está encendido y no transmite nada.**
Mira el indicativo. De fábrica viene sin configurar y **no emite absolutamente nada hasta
que le pongas uno**: es una protección para no ensuciar la frecuencia con una estación
que no existe.

**El GPS no coge satélites.**
Míralo al aire libre y con la antena bien puesta. Dentro de casa puede tardar minutos o no
fijar. Si en el menú salen satélites «a la vista» pero no fija, está en ello: dale tiempo
y cielo.

**¿Por qué tarda tanto en aparecer la primera posición?**
Porque el rastreador no publica nada hasta que el GPS fija de verdad. Es deliberado. Si
tienes prisa, **dos toques seguidos del botón** mandan la última posición conocida.

**He cambiado de modo y el GPS sigue encendido.**
Eso ya no pasa: el estado del GPS depende **siempre del modo actual**, sin heredar nada
del anterior.

**¿Se puede cambiar el modo sin reiniciar?**
Sí, el modo se aplica en marcha. Lo que **sí** necesita reinicio son los ajustes de radio
(frecuencia, velocidad, ancho de banda, potencia) y el modo módem.

**En el T-Echo, la pantalla no se ilumina.**
Es tinta electrónica: no tiene luz. Se enciende una luz de fondo 5 segundos cada vez que
tocas un botón. Y si acabas de encender el aparato, dale unos segundos: la pantalla tarda
en pintarse.

**En el T-Echo, un botón mueve y el otro confirma, y me hago un lío.**
Regla rápida: **táctil = moverse y cambiar; físico corto = entrar y confirmar; físico
largo = volver atrás**. Y dos toques seguidos del físico, fuera del menú, mandan una
baliza a mano.

**He pulsado largo en «Borrado» y no ha hecho nada.**
Ojo con el gesto, que aquí cada placa tiene el suyo: **en el T-Echo las acciones peligrosas
piden dos pulsaciones CORTAS** (el largo es «volver atrás», así que manteniéndolo sales de la
lista). La primera pulsación corta solo avisa, con la pantalla **CONFIRMAR**; vuelve a pulsar
**corto** en la misma opción antes de que se acabe el tiempo (15 segundos). En la **Faketec**
(OLED) es al revés: allí **sí** son dos pulsaciones **largas**, y el aviso dura 3 segundos.

**He mandado el nodo a dormir y no responde.**
Está dormido, no roto. **En el T-Echo hay que pulsar el botón de RESET** para
despertarlo; en la Faketec, el botón. Y si estaba enchufado al USB, ni siquiera se habrá
dormido: avisa y se queda encendido.

**El nodo se ha quedado sin batería en el monte y no responde.**
Es lo esperado: se ha dormido para proteger la batería. Vuelve solo cuando la tensión suba
(sol). Si tienes prisa y estás a mano, el reset lo despierta.

**He montado el divisor de la batería con otras resistencias.**
Vuelve a montarlo con **dos de 1 MΩ iguales**. Si no, la tensión que ves no es la real y
la protección que evita que se duerma con la batería agonizante no funciona.

**El nodo no contesta por USB.**
Puede estar el puerto ocupado por otro programa (**solo lo puede abrir uno a la vez**), o
el nodo acaba de arrancar y el puerto aún no está listo. Cierra el otro programa y vuelve
a intentarlo.

**¿Puedo usar dos nodos a la vez en casa?**
Sí, pero **con indicativos distintos** (por ejemplo `EA2ABC-7` y `EA2ABC-10`). Dos nodos
con el mismo indicativo se pisotean y producen duplicados.

**¿Por qué no me sale la opción de mandar el mensaje al último oído en el T-Echo?**
Porque esa fila todavía no está en el menú de la pantalla de tinta. El mensaje rápido se
manda desde el configurador web o desde la consola.

**¿Se puede apagar el Bluetooth?**
No hay Bluetooth en esta versión del firmware. Los ajustes existen pero **no hacen nada**;
se dejan para cuando vuelva.

**¿Se puede usar el acelerómetro del T-Echo Plus?**
No. La placa lo lleva, pero **el firmware no lo usa** para nada: el movimiento lo saca del
GPS.

---

# Ficha técnica

| Dato | Valor |
|---|---|
| Banda | **433 MHz** (aficionados, uso secundario) |
| Frecuencia de la red | **433,775 MHz** |
| Modulación | LoRa · **SF12** · ancho **125 kHz** · codificación **4/5** |
| Potencia | Hasta **22 dBm** con la HT-RA62. Con el módulo **E22P**: 8 dBm de fábrica y **12 dBm como máximo** (lleva amplificador propio) |
| Microcontrolador | Nordic **nRF52840** (Cortex-M4) |
| Memoria | 1 MB de flash, 256 KB de RAM |
| Alimentación | Litio de 1 celda, 3 pilas NiMH, o USB |
| Medida de la batería | Divisor de tensión **obligatorio de 1 MΩ + 1 MΩ** (factor 2) en las placas nRF52840 con E22P |
| Pantalla | OLED 128×64 en las Faketec · tinta electrónica 200×200 en los T-Echo |
| Sensores | BMP280 / BME280 / BME680 · AHT20 · INA219 · BME280 de serie en los T-Echo |
| GPS | u-blox (Faketec) · Quectel L76K (T-Echo) |
| Avisos | Zumbador y motor de vibración, **solo en el T-Echo Plus** |
| Cadencia de fábrica | Baliza 30 min · telemetría 53 min · meteorología 55 min |
| Licencia del firmware | **GPL-3.0** (software libre) |

---

# Glosario

**APRS** — *Automatic Packet Reporting System*. El sistema de radioaficionado para
mandar posición, mensajes cortos, meteorología y objetos. Nació en los años 80 y sigue
vivo.

**APRS-IS** — La red de internet por la que circulan las tramas APRS. Es lo que leen
aprs.fi y los demás visores.

**Baliza** — El mensaje periódico que dice dónde estás.

**Digipeater / repetidor** — Equipo que oye una trama y la vuelve a emitir para que
llegue más lejos.

**iGate** — La puerta entre la radio y APRS-IS. **Este aparato no lo es** (no tiene
internet).

**Indicativo y SSID** — Tu identificación de radioaficionado (por ejemplo `EA2ABC`) y un
número que distingue varios equipos tuyos (`EA2ABC-7`, `EA2ABC-10`).

**KISS / TNC2** — Dos formas de hablar por el cable con un programa de APRS: en binario
(KISS, el moderno) o en texto (TNC2, el clásico).

**LoRa** — La modulación de radio que usamos. Llega muy lejos con muy poca potencia, pero
es lenta: una trama ocupa unos segundos de aire.

**Perfil de uso** — Un juego de ajustes guardado para una forma de moverte (a pie, en
bici, en coche), con su propio número de indicativo y sus propios tiempos.

**Ruta (path)** — La lista de repetidores que **pides** que te repitan.

**SmartBeaconing** — La cadencia inteligente: emitir más a menudo cuando vas rápido, y
menos cuando vas despacio o estás parado. Es lo que hacen los perfiles.

**Telemetría** — Los números que el nodo publica (tensión, corriente, temperatura...) en
un formato que los visores saben dibujar como gráficas.
