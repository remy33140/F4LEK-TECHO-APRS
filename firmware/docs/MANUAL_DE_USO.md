> **AVISO — VERSIÓN ANTIGUA — NO usar como manual vigente.**
>
> **QUÉ MANDA HOY**: `MANUAL_USUARIO.md`, el manual vigente y el único que genera el PDF.
> Si algo de aquí lo contradice, GANA ÉL (y si el código contradice a los dos, gana el
> código). El contenido de abajo **no se ha reescrito a propósito**: su trabajo es contar
> lo que se pensaba entonces, y explica las cosas con otras palabras, así que sigue
> sirviendo para consultar.
>
> Se ha quedado atrás, entre otras cosas: el toque largo del botón (aquí 800 ms, el
> firmware usa **600 ms**), los intervalos automáticos (aquí «telemetría cada 10 minutos y
> meteorología cada 15»; el firmware usa **53 y 55 minutos** por defecto, ajustables), el
> menú y las escenas de la pantalla de tinta electrónica del T-Echo (que aquí no existen),
> y los avisos por vibración y por sonido del T-Echo Plus.

# Manual de uso — Nodo APRS-LoRa Faketec

Este manual explica **cómo funciona el nodo y cómo se usa**, en lenguaje llano.
Está pensado para que lo entienda cualquiera que coja el aparato, sin necesidad de
saber programación.

---

# 1. Qué es este aparato

Un nodo de radioaficionado para la banda de **433 MHz** que habla **APRS** por
LoRa. Puede hacer tres trabajos, y tú eliges cuál con un ajuste llamado **modo**:

| Modo | Qué hace |
|---|---|
| **0 · Repetidor** | Escucha las tramas de otros y **las repite** para que lleguen más lejos. Además anuncia su propia posición fija |
| **1 · Rastreador** | **Publica dónde estás**, con posición, rumbo, velocidad y altura. **No repite** a nadie |
| **2 · Ambos** | Las dos cosas a la vez: repite a los demás **y** publica tu posición |

**El repetidor y el rastreador son independientes.** Repetir consiste en oír una
trama y volver a emitirla; rastrear consiste en mandar tu posición cada cierto
tiempo. El modo no enciende ni apaga el repetidor: lo que hace es decidir si el
rastreador trabaja (modos 1 y 2) y qué posición se anuncia.

---

# 2. Las tres reglas de oro sobre la posición

Estas reglas son deliberadas y conviene entenderlas, porque explican casi todo el
comportamiento del aparato en el mapa.

## Regla 1 — El rastreador nunca publica una posición inventada

Si estás en modo **rastreador** o **ambos** y el GPS **todavía no ha fijado**, el
nodo **no manda ninguna posición**. Manda solo un aviso de texto diciendo que está
en marcha y buscando satélites.

Antes sí la mandaba: al arrancar, y otra vez a los 10 minutos si seguía sin fijar,
usaba la **posición fija que tuvieras configurada**. Eso era engañoso: en el mapa
aparecías en un sitio donde no estabas. **Ya no se hace.**

## Regla 2 — En modo repetidor, la posición fija es la suya

Un repetidor está clavado en un sitio, y ese sitio es su razón de ser. Así que en
**modo 0** el nodo anuncia la **posición que le hayas configurado**, y eso es
correcto. Si además activas «GPS en repetidor», usará la del GPS en vivo.

## Regla 3 — Puedes mandar la última posición conocida a mano

Si quieres publicar dónde estuviste, aunque ahora el GPS no tenga fijación:

**Doble toque del botón.**

- Si el GPS **tiene fijación** → manda tu posición **de ahora**.
- Si **no** la tiene → manda la **última posición real** que fijó, aunque sea de un
  paseo anterior (el nodo la recuerda aunque lo apagues).
- Si **nunca** ha fijado → no manda nada y te lo dice en pantalla. Mejor eso que
  publicar algo falso.

**Cómo viaja esa posición**, y esto es importante: va marcada como histórica de
dos formas a la vez. Lleva **su hora real** (la de cuando se tomó, no la de ahora)
y además el aviso **`>ULTIMA CONOCIDA`** al principio del comentario. Así
cualquiera que la vea —una persona o un programa— sabe que es una posición
antigua, sin tener que deducirlo.

> **Nota técnica**: el sello de hora de APRS solo lleva **día, hora y minuto**, sin
> el año. Por eso una posición de **más de 24 horas** puede interpretarse como del
> día anterior. Para un paseo del mismo día es exacto; si es más vieja, el aviso
> `>ULTIMA CONOCIDA` es lo que evita el malentendido.

---

# 3. Cuándo manda posición el nodo (resumen)

| Situación | ¿Manda posición? |
|---|---|
| Modo 0 (repetidor), arrancando | **Sí**, la fija configurada |
| Modo 0, cada X minutos | **Sí**, la fija configurada |
| Modo 0 con «GPS en repetidor» y fijación | **Sí**, la del GPS en vivo |
| Modo 1 o 2, **sin** fijación | **No.** Solo el aviso «buscando satélites» |
| Modo 1 o 2, **con** fijación | **Sí**, la real. La primera en cuanto fija |
| Modo 1 o 2, en movimiento | **Sí**, según cadencia, giros y distancia |
| Modo 1 o 2, **parado** | **Sí**, una baliza lenta **cada 15 minutos** |
| Doble toque del botón | Sí: posición real, o la última conocida |

---

# 4. El GPS: lo que hay que saber en la práctica

## La antena es lo más importante

El GPS necesita **ver cielo**. No es como la radio de 433: **es otra antena**, más
pequeña, y si no está bien puesta el receptor queda sordo del todo. Dentro de casa
puede tardar **minutos** o no fijar nunca, aunque esté en una ventana.

## Arranque en frío y en caliente

- **En caliente** (el GPS guarda datos de órbita recientes): fija en **segundos**.
- **En frío** (sin datos, por ejemplo tras mucho tiempo apagado o tras borrarlos):
  hay que descargar el almanaque entero, y eso son **minutos** con cielo abierto.

## Cuando no fija, ¿está roto?

No necesariamente. Para saberlo, mira **«Ver GPS»** en el menú o el informe de
diagnóstico: si dice **satélites a la vista** mayor que cero, el receptor **oye**
satélites y el problema es de cielo o de tiempo. Si dice **cero** y estás al aire
libre con la antena puesta, entonces sí hay algo que revisar.

## La opción «Ahorro de GPS»

**Solo actúa en modo repetidor.** Allí el GPS se apaga y se enciende por ciclos
para no gastar batería. **En modo rastreador va siempre encendido**: apagarlo le
hace perder los satélites y volver a empezar.

## La opción «GPS en repetidor»

Hace que el repetidor use la **posición del GPS en vivo** en vez de la fija. Útil
si el nodo no está siempre en el mismo sitio. **Ojo**: obliga a llevar el GPS
encendido, y eso gasta batería.

> ### Estas dos opciones solo salen en modo repetidor
>
> Si el nodo está en **rastreador** o en **ambos**, en la sección GPS **no verás**
> ni «Ahorro de GPS» ni «GPS en repetidor». No es un fallo ni se han perdido: se
> **esconden a propósito**.
>
> **Por qué.** En rastreador y en ambos el GPS va **siempre encendido**, que es lo
> correcto para esos modos. Allí esos dos ajustes **no harían absolutamente nada**:
> los podrías tocar y no pasaría nada, que es la peor clase de ajuste que existe
> (el usuario cree que ha cambiado algo y no ha cambiado nada). Escondiéndolos,
> lo que ves en el menú es siempre lo que de verdad funciona.
>
> **Lo que hayas dejado puesto no se pierde ni te puede fastidiar.** Si tenías
> «Ahorro de GPS» encendido en modo repetidor y cambias a rastreador, el ajuste se
> queda guardado pero **deja de aplicarse**: el código solo lo mira en modo
> repetidor, así que en rastreador el GPS sigue encendido pase lo que pase. Vuelve
> a modo repetidor y lo tendrás como lo dejaste.
>
> **Regla práctica:** si buscas un ajuste de GPS y no lo encuentras, mira primero
> en qué **modo de trabajo** estás.
>
> **Y no hacen falta para «Fijar coords»** (el apartado 5). Esa operación enciende y
> apaga el GPS por su cuenta, en cualquier modo: no tienes que activar «GPS en
> repetidor» antes. Lo único que cambia es que, si el GPS ya estaba encendido por
> estas opciones, la operación lo deja encendido al terminar.

---

# 5. Fijar la posición del repetidor con el GPS (útil en el monte)

**El caso**: vas a colocar el repetidor en un sitio nuevo y quieres que su
posición sea exactamente donde lo dejas, sin teclear coordenadas a mano.

**El menú tiene la opción `Fijar coords actuales`, en la sección GPS.** Con una
pulsación hace **todo el trabajo**:

1. **Enciende el GPS** (aunque estuviera apagado).
2. **Espera a que fije.** Sin tope de tiempo: si no hay cielo, sigue intentándolo.
   Un tope solo serviría para rendirse cuando quizá faltaban diez segundos.
3. **Deja que se asiente.** No guarda la posición en el instante del fix: espera
   **20 lecturas seguidas con fijación** (unos 20 segundos) y guarda **la última
   válida**. Motivo: justo después de fijar, el receptor da **saltos de decenas o
   cientos de metros** mientras resuelve ambigüedades. Guardar en ese momento sería
   guardar la peor posición de todas.
4. **Guarda la posición** en la configuración (queda en memoria permanente).
5. **Vuelve a apagar el GPS**, para no gastar batería.

Mientras trabaja, la pantalla va diciendo lo que está pasando, para que sepas que
**no se ha colgado**:

- **En la Faketec (OLED)**: `Buscando GPS...` con los satélites a la vista (se
  actualiza cada segundo), luego `GPS 7/20 asentando` y al final
  `Guardado <latitud> <longitud>`.
- **En el T-Echo (tinta)**: la pantalla se pone entera para la ocasión —
  `Buscando GPS...` con los satélites, `Asentando...` con barra y `Muestra 7/20`, y
  `GUARDADO` con la latitud y la longitud.

**Y se puede cancelar**: mientras dice `Buscando GPS...` o `Asentando...`, **un toque
de botón cancela** la operación (en la tinta lo pone: `Toca un botón para cancelar`).
Cancela sin guardar nada y **apaga el GPS** si lo había encendido ella. Como la
fijación puede tardar lo que necesite, no te quedas atrapado esperando.

**Tiempo esperable**: menos de un minuto si el GPS trae datos recientes; **2-3
minutos** si arranca en frío, y **puede ser más** con el cielo tapado. Que tarde no es
un fallo: mira los satélites a la vista. Si no quieres esperar, cancela.

> **El encendido del GPS es temporal y NO se guarda.** Si el nodo se reinicia a
> mitad del proceso, el GPS vuelve **apagado**: no se queda chupando batería sin
> que nadie lo haya pedido. Es una sesión de un solo uso.
>
> **Si el GPS ya estaba encendido** (por ejemplo con «GPS en repetidor» activado),
> la opción lo usa pero **no lo apaga al terminar**: lo deja como estaba.

**Funciona en cualquier modo de trabajo.** No hace falta activar nada antes:
**la propia operación enciende el GPS y lo apaga al terminar**, también en modo
Repetidor y con «GPS en repetidor» apagado. (Antes sí hacía falta activarlo, y por eso
en algunas versiones de este manual lo pone: ya no.)

**Flujo recomendado en el monte:**
1. Deja **«GPS en repetidor» apagado** (es lo de fábrica).
2. Pulsa **`Fijar coords actuales`** y espera a que diga que ha guardado.
3. Listo: el repetidor ya anuncia esa posición **y el GPS sigue apagado**
   ahorrando batería.

> Si tienes «GPS en repetidor» **activado**, la posición que acabas de guardar no
> se usa para balizar (se usa la del GPS en vivo). Para usar la guardada, deja esa
> opción apagada.

---

# 6. El botón

El nodo se maneja entero con **un solo botón**:

| Gesto | Qué hace |
|---|---|
| **Toque corto** | Enciende la pantalla si está apagada; si no, pasa a la escena siguiente; dentro del menú, baja de línea |
| **Toque largo** (800 ms, sin soltar) | Abre el menú; dentro del menú: entra, edita, ejecuta o confirma |
| **Doble toque** | **Manda una baliza de posición a mano** (ver regla 3) |

Dentro del menú, un doble toque **no** manda baliza: allí significa dos toques
cortos, y mientras editas un valor **cancela la edición** (deja el valor anterior).

---

# 7. La pantalla

La OLED va mostrando escenas que cambian solas: posición, GPS, potencia y batería,
sensores, radio, mensajes y registro. Un toque corto las va pasando y **pausa el
avance automático** unos segundos.

El **menú** está organizado por categorías: modo de trabajo, GPS, balizas,
mensajes, rastreador, radio, repetidor, APRS, sensores, pantalla, energía, remoto
y ajustes. Arriba y abajo de cada categoría hay filas para **salir** y **volver**.

## El menú se cierra solo a los 20 segundos

Si dejas el menú abierto y **no tocas nada durante 20 segundos**, se cierra solo y
la pantalla **vuelve al carrusel de escenas** de siempre. Así un nodo que te has
dejado olvidado no se queda plantado en el menú toda la tarde.

**Con aviso**: en los **3 últimos segundos** aparece una cuenta atrás en la esquina
(`3s`, `2s`, `1s`), para que no se cierre por sorpresa. Cualquier toque del botón
reinicia la cuenta: navegar, entrar en una categoría o cambiar un valor cuenta como
actividad, así que **nunca se cierra mientras estás usándolo**.

> **Un detalle práctico**: si estás escribiendo un valor (por ejemplo el
> indicativo) y te quedas parado, el menú se cierra y **lo escrito se descarta**:
> se queda el valor que había. No se guarda a medias.
>
> Mientras escribes **no** sale la cuenta atrás, para no meter ruido en la línea
> que estás editando.

---

# 8. Qué manda el nodo por su cuenta

Además de las balizas de posición:

- **Aviso de arranque**: al empezar, un paquete de estado para que se sepa que está
  vivo. En modo rastreador, si aún no hay GPS, dice que está buscando satélites.
- **`GPS OK`**: una sola vez, en cuanto el GPS fija. Sirve para que el mapa deje de
  mostrar «buscando satélites» cuando ya hay posición.
- **Telemetría** cada 10 minutos: tensión, corriente, temperatura, humedad, presión.
- **Meteorología** cada 15 minutos, en el formato estándar de APRS.
- **Estado** al arrancar y una vez al día.
- **Avisos** de batería baja y de que se va a dormir.

---

# 9. Mensajes, boletines y objetos

- **Mensajes**: se manda texto a otra estación, como un SMS por radio. Si el otro
  contesta con acuse, el nodo lo avisa; si no, **reintenta** cada 30 segundos
  (3 veces de fábrica) y luego se rinde.
- **Boletines** (`BLN0`-`BLN9`): un mensaje para todos, útil en una quedada.
- **Objetos**: publica un punto con nombre en el mapa de los demás (un puesto de
  control, un campamento) y lo puedes borrar cuando quieras.

Se pueden mandar desde el configurador web, por comandos o desde la pantalla.

---

# 10. Conectar el nodo a un programa (TNC)

El nodo puede hacer de **TNC**: un aparato que conecta un programa de APRS con la
radio. Hay un selector de tres posiciones:

| Ajuste | Qué hace |
|---|---|
| **Apagado** | El nodo funciona por su cuenta |
| **TNC2** | Manda las tramas en **texto** (para programas antiguos) |
| **KISS** | Manda las tramas en **binario**, que es lo que piden casi todas las apps modernas |

**Importante en modo KISS**: el nodo **se calla**. Deja de mandar sus balizas,
telemetría y meteorología, porque en ese modo **manda el programa** que tengas
conectado. El repetidor sigue igual, y lo que pidas a mano (botón, menú, web) sigue
funcionando. **El cambio necesita reiniciar el nodo.**

---

# 11. Ahorro y sueño

- **Dormir entre balizas** (modo rastreador): el nodo se apaga entre una baliza y
  la siguiente para durar más en excursiones largas. **En modo ambos no se duerme**,
  porque el repetidor no puede estar sordo.
- **Sueño de protección**: si la batería baja de un umbral, el nodo se apaga solo
  para no dañarla. Vuelve a arrancar cuando la tensión sube (por ejemplo, cuando
  el panel solar carga).
- **Nunca se duerme si está enchufado al USB.**
- **Apagado de pantalla**: la OLED se apaga sola tras los segundos que pongas en
  «Apagar pantalla (seg)». **De fábrica viene a 0, o sea que NO se apaga nunca**:
  así el nodo recién montado siempre enseña algo y no parece muerto. Si lo pones
  en un sitio donde no quieras luz, pon 30 o 60 segundos. Cualquier toque la
  enciende otra vez, y si estaba abierto el menú **vuelve donde lo dejaste**.

> **Orden de los tiempos**: con el ajuste de fábrica (0 = nunca) el único tiempo
> que actúa es el del menú, que se cierra a los 20 s y devuelve el carrusel. Si tú
> pones el apagado de pantalla, manda tu ajuste: con 30 s verás cerrarse el menú y
> unos segundos de carrusel antes de que la pantalla se apague; con menos de 20 s
> la pantalla se apaga antes y, al despertarla, el menú reaparece donde estaba.

---

# 12. El registro de viaje

El nodo guarda en su memoria interna **todo lo que pasa**: balizas enviadas,
tramas repetidas, recepciones y eventos, con la hora y fecha del GPS.

- **Sobrevive a apagones**: si se corta la corriente mientras escribe, el registro
  no se corrompe.
- Se puede **descargar** y convertir a **GPX/KML/CSV**, listo para Wikiloc, Google
  Earth, Garmin o Strava.
- **Se activa en modo rastreador**, para que el nodo fijo de casa no gaste memoria.

En el registro, cada baliza de posición anota **por qué salió**: `F` primera
fijación, `R` por tiempo, `C` por un giro, `D` por distancia recorrida, `M` a mano,
y **`L` cuando es la última posición conocida**. Así, al exportar la ruta, **un
punto histórico no se confunde con un punto nuevo**.

---

# 13. Cómo se actualiza el firmware

**A mano (lo que puedes hacer tú en cualquier momento):**

1. **Doble toque al botón de reset** → aparece una unidad llamada **NICENANO**.
2. Se copia el fichero `.uf2` a esa unidad.
3. Se espera a que desaparezca la unidad y el nodo arranca solo.

**Automático (lo que se puede hacer desde el ordenador, sin tocar el nodo):** el
nodo admite **DFU por el puerto serie**. Desde el PC se manda el paquete
`.pio/build/<entorno>/firmware.zip` al puerto del bootloader y el nodo se graba y
reinicia solo, en unos 40 segundos. Es la vía que se usa ahora para actualizar
durante el desarrollo.

> **El «toque» de los 1200 baudios** (abrir el puerto a esa velocidad hace que la placa entre
> sola en modo grabación) **está disponible como opción** en la página del flasher, y merece
> la pena probarlo: en muchos equipos funciona y ahorra el doble toque. **En el PC de
> desarrollo de este proyecto dio error de dispositivo** al abrirlo a 1200 (ver el histórico),
> pero eso es de ese Windows y de su controlador, no de la placa. **Si no entra, el doble toque
> funciona siempre.**

**Si algo sale mal y el nodo no arranca**: doble toque al reset y copiar un
firmware bueno conocido. En este proyecto hay uno guardado en
`data/rescue/firmware_bueno_sin_bluetooth.uf2`, y también una copia de lo que
llevaba grabado el nodo antes de cada actualización.

---

# 14. El configurador web (desde el ordenador)

Se abre el fichero **`web/index.html`** con **Chrome o Edge** (Firefox y Safari no
tienen WebSerial, que es lo que hace falta para hablar con el nodo). Se pulsa
**Conectar** y se elige el puerto del nodo. Sirve para todo:

- **Estado en vivo** y **consola** para ver qué está pasando.
- **Formulario** con todos los ajustes, agrupados por secciones: estación, rutas,
  balizas, repetidor, radio, sensores, rastreador, pantalla, energía, control
  remoto y Bluetooth.
- **Acciones**: mandar baliza, telemetría, formato de telemetría, leer estado y
  silenciar.
- **Mensajes, boletines y objetos**, para mandarlos sin escribir comandos.
- **Registro de viaje**: se descarga lo que el nodo guardó, **se ve el recorrido en
  un mapa** y se puede exportar a **GPX, KML o CSV** para Google Earth, Wikiloc o
  cualquier app de rutas.

## El mapa del recorrido

Al leer el registro, debajo sale **el mapa con la ruta dibujada**. Se maneja como
cualquier mapa: **se arrastra con el ratón** para moverse, **la rueda acerca y
aleja**, hay botones **+ / −**, **doble clic acerca** donde pinchas, y el botón
**«Ajustar a la ruta»** la encuadra entera. El principio sale marcado con **A**
(verde) y el final con **B** (rojo), y debajo del mapa hay un resumen: puntos,
distancia, velocidad máxima, altitud mínima y máxima, y duración.

> **El mapa es propio, sin librerías de internet.** Eso es a propósito: este nodo
> vive en el monte y en casas sin cobertura, y no tiene sentido que el programa
> dependa de que hoy haya conexión para poder enseñarte tu ruta.
>
> **Internet solo hace falta para las fotos del terreno.** Si no hay, la ruta se
> ve igual sobre un fondo con rejilla, y el mapa te lo dice. Con el botón **«Sin
> fotos (ahorra datos)»** puedes apagarlas tú: útil con datos móviles o para que
> vaya más rápido.
>
> **Si las fotos no cargan y el aviso habla de la política de OpenStreetMap**:
> es porque has abierto la página haciendo doble clic en el fichero. El servidor
> de mapas de OpenStreetMap **exige** que las peticiones lleven una cabecera
> (`referer`) que solo se envía cuando la página viene de una dirección web, y
> **prohíbe expresamente el uso sin conexión**. No es un fallo del programa y no
> se debe burlar. La solución es servir la página en local, que sí cumple:
>
> ```
> powershell -File tools\sirve_web.ps1
> ```
>
> y abrir **http://localhost:8000/web/** en Chrome o Edge. La ruta se sigue
> viendo igual mientras tanto.

> **Si el mapa sale sin ruta**: mira lo que dice el texto debajo del mapa. El
> registro **solo se guarda en modo rastreador o ambos** (en repetidor puro está
> apagado a propósito, para no gastar memoria), y hace falta que el GPS haya dado
> hora. **Estando parado también se guarda una posición cada 15 minutos** (la
> «baliza lenta de aparcado», para que los mapas no se queden viejos), así que un
> nodo con GPS un rato debería tener puntos. Si no los tiene, o si el texto dice
> que hay **líneas que no se han podido leer**, avísame: eso sí sería un fallo.
- **Zona delicada**: reiniciar, volver a valores de origen (borra la
  configuración, no reinicia), borrado total y modo grabación.

## El aviso que sale al rellenar el formulario

Cuando cambias cualquier cosa, el configurador **avisa** de las cosas que no se
hacen bien: un nodo sin indicativo, coordenadas a 0,0, una frecuencia que no es la
de la red, una ruta con demasiados saltos para un nodo fijo, una baliza demasiado
frecuente... También avisa de **ajustes que no van a hacer nada** en el modo que
tienes puesto (por ejemplo, el ahorro de GPS en modo rastreador).

**Los avisos no te impiden guardar.** Son consejos: si sabes lo que haces, puedes
guardar igual. Lo que sí bloquea el guardado es un valor fuera de rango, que lo
rechaza el propio nodo.

**El botón «Recuperar valores recomendados»** rellena el formulario con valores
sensatos (frecuencia y velocidad de la red, rutas correctas para cada modo...).
**No toca el indicativo ni las coordenadas**: eso es de cada uno y hay que
ponerlo a mano. Mientras no pongas indicativo, el aviso te lo recordará.

> **El indicativo y las coordenadas son lo primero que hay que rellenar.** Un
> nodo sin indicativo no es nadie en el aire, y con las coordenadas a cero
> aparece en el mapa en medio del Atlántico.

## Si el nodo no entiende un valor, te lo dice

Cuando algo que le mandas **no encaja en el tipo** que ese ajuste espera (por
ejemplo `beaconInterval` con el texto «cada media hora», una latitud escrita como
«42N», o un decimal donde el ajuste quiere un número entero), el nodo **no cambia
ese ajuste** y **te lo dice**:

- en la respuesta sale **`ignorado (tipo incorrecto):`** seguido de la lista de
  claves que no ha podido leer;
- y queda anotado en el **registro de viaje** (`log dump`), para poder mirarlo
  después.

El resto de la configuración **sí se guarda**: lo que no encaja se queda como
estaba, pero ya no se calla. (Antes sí se callaba: contestaba que todo había ido
bien y el ajuste no se había cambiado. Eso hacía perder el tiempo buscando el
fallo donde no estaba.)

> **Ojo con la diferencia, que es lo importante**: *ignorado* no es lo mismo que
> *no aplicado*. Si el valor no encaja, ese campo **conserva el valor que ya
> tenía**; no se queda a cero ni se inventa nada.

---

# 15. Preguntas frecuentes

**No aparece en el mapa.**
Comprueba, por este orden: (1) que el GPS tiene fijación; (2) que hay un iGate
oyéndote; (3) que no estás en modo rastreador sin fijación, porque entonces el nodo
**a propósito** no manda posición, solo el aviso.

**El GPS no coge satélites.**
Míralo al aire libre y con la antena bien puesta. Dentro de casa puede tardar
minutos o no fijar. Si en el informe salen satélites «a la vista» pero no fija,
está en ello: dale tiempo y cielo.

**¿Por qué a veces tarda tanto en aparecer la primera posición?**
Porque el rastreador no publica nada hasta que el GPS fija de verdad. Es
deliberado. Si tienes prisa, el **doble toque** manda la última posición conocida.

**He cambiado de modo y el GPS sigue encendido.**
Eso ya no pasa: el estado del GPS depende **siempre del modo actual**. Al cambiar
de modo, el GPS se enciende o se apaga según corresponda al modo nuevo, sin
heredar nada del anterior.

**¿Se puede cambiar el modo sin reiniciar?**
Sí, el cambio se aplica en marcha.

**El nodo no contesta por USB.**
Puede estar el puerto ocupado por otro programa (solo lo puede abrir uno a la vez),
o el nodo acaba de arrancar y el puerto aún no está listo. Cierra el otro programa
y vuelve a intentarlo.

**¿Se puede apagar el Bluetooth?**
No hay Bluetooth en el firmware actual. Está fuera del programa por un problema del
chip con el arranque del SoftDevice que está en investigación. Ver el cerebro del
proyecto para el estado.
