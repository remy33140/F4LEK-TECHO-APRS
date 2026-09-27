# Funciones del firmware

> **AVISO (2026-09-13)**: este documento es la lista de lo que el nodo SABE hacer.
> Dos matices que conviene tener presentes, porque hubo una version anterior de
> este texto que los daba por hechos:
>
> - **El Bluetooth está APAGADO** en el firmware de ahora (fuera de la
>   compilación) porque rompía el nodo. Todo lo que se lea sobre Bluetooth,
>   hoy no funciona.
> - **El registro de viaje se guarda en modo rastreador Y en modo ambos** (solo
>   está apagado en repetidor puro).


Firmware APRS-LoRa para placas **Faketec (nRF52840)** con radio **HT-RA62** o **E22P-433M30S**.
Pensado para cualquier radioaficionado: funciona como repetidor, como rastreador o como las dos cosas a la vez.

---

## Radio y compatibilidad
- **APRS sobre LoRa en 433 MHz**, con los mismos parámetros que usa el ecosistema: **compatible 100 % con el resto de nodos y iGates APRS LoRa**
- **Escucha antes de emitir**: no pisa a otras estaciones y reintenta si el canal está ocupado
- **Potencia ajustable**, y **el mismo número no significa lo mismo en cada módulo**: en el **E22P** el módulo lleva su propio amplificador, así que el ajuste es la **excitación** y no la potencia que sale por la antena (**8 dBm**, el valor de fábrica, sacan **más de 500 mW**; **12 dBm**, el máximo, **más de 1 W**, y en algunos ejemplares 1,2-1,5 W según la electrónica; medido con vatímetro). En los **SX1262/SX1278** (HT-RA62) **no hay amplificador**: el ajuste **es** la potencia de salida, hasta **22 dBm**
- **Silencio total** a un toque: corta toda emisión y sigue escuchando

## Repetidor
- **Tres modos**: apagado, solo WIDE1-1, o WIDE1-1 + WIDE2
- **Reescribe la ruta con tu indicativo**, para que aprs.fi y los demás vean por dónde ha pasado
- **No repite duplicados**, ni sus propias tramas, ni las marcadas como "no pasar a Internet"
- **Lista negra** de indicativos, con comodines
- **Respeta el orden correcto de la ruta** (no repite tramas mal formadas)

## Balizas y meteorología
- **Baliza de posición** cada X minutos, con comentario y símbolo a elegir (repetidor, coche, persona, bici…)
- **Icono del mapa que va con el perfil de uso**: al cambiar de perfil (fijo/digi, peatón, bici, coche) cambia el SSID **y el icono** con el que sales en el mapa, cada perfil con el suyo (repetidor, persona, bici, coche). Se elige desde el menú del propio aparato; y si prefieres fijar un icono a mano para todo, **ese manda** sobre el del perfil
- **Símbolo con overlay**: además de la tabla normal se puede usar la alternativa con una letra o número encima (para distinguir varios nodos iguales)
- **Posición discreta**: se pueden ocultar de 1 a 4 dígitos de la posición (para un nodo de casa que no quiere publicar el sitio exacto)
- **Posición comprimida** (opcional): la baliza del rastreador ocupa menos aire, como hacen los equipos comerciales
- **Aviso de batería baja en el propio texto** de la baliza (añade `BAT BAJA` al bajar de **3,6 V en los módulos E22P** y de **3,5 V en los demás**: el amplificador del E22P gasta más corriente y por eso avisa 100 mV antes), además de en la telemetría
- **Aviso de que se va a dormir**: cuando la batería baja del umbral de apagado, la baliza añade `DORMIR` para que se vea en el mapa antes de que el nodo se apague; al dormirse y al volver manda además una baliza con el cartel correspondiente
- **Una sola configuración para LiPo y NiMH**: los umbrales de apagado y despertar (3,4 V / 3,7 V en las Faketec; **3,2 V / 3,4 V en el T-Echo**, que lleva celda de litio) valen para las dos químicas sin tocar nada
- **Temperatura, humedad y presión** en la baliza, en texto legible **y** en el formato meteorológico estándar que aprs.fi dibuja como estación del tiempo
- **Batería** en cada baliza
- **Telemetría estándar APRS** con canales y unidades, para gráficas históricas
- **Paquete de estado** al arrancar y una vez al día
- **Temperatura del chip** como referencia de la temperatura dentro de la caja

## Rastreo GPS
- **Tracking GPS exportable a formatos populares KML/GPX**, listo para Wikiloc, Google Earth, Garmin o Strava
- **Tres modos de trabajo**: solo repetidor, solo rastreador, o **los dos a la vez**
- **Cadencia inteligente (SmartBeaconing)**: emite más o menos según la velocidad, con perfiles de a pie, bici o coche
- **Baliza extra en cada giro o cruce**: las rotondas y cruces quedan marcados aunque no toque por tiempo
- **Emisión cada X metros**, con un tiempo mínimo de seguridad para no saturar el canal y **solo cuando de verdad te estás moviendo** (así el nodo no gasta aire parado por un salto tonto del GPS); **parado sigue publicando su posición cada 15 minutos**, para que el mapa no se quede con una posición vieja
- **Hora del GPS** en cada baliza, con rumbo, velocidad y altitud
- **Primera baliza en cuanto fija**, sin esperas
- **Nunca publica una posición que no sea real**: en modo rastreador y en modo ambos, si el GPS todavía no ha fijado, el nodo **no manda posición** — solo el aviso de que está en marcha y buscando satélites. Así en el mapa no aparece nunca una posición «inventada» por el aparato (el modo repetidor sí anuncia su posición fija, porque ese es su trabajo)
- **Última posición conocida a mano**: un **doble toque del botón** manda la última posición que el GPS fijó de verdad, **aunque sea de un paseo anterior** (el nodo la recuerda al apagarse). Viaja con **su hora real** — la de cuando se tomó — y con el aviso **«ULTIMA CONOCIDA»** en el comentario, para que quien lo vea sepa que es histórica y no de ahora. Si el nodo nunca ha fijado, no manda nada y lo avisa en pantalla
- **Ahorro de GPS** (solo en modo repetidor): lo apaga cuando no hace falta y lo despierta antes de cada baliza. En modo rastreador va siempre encendido, para no perder los satélites
- **Fijar la posición desde el propio aparato, sin teclear números**: una orden del menú enciende el GPS, espera a que fije **lo que haga falta** (sin tope de tiempo), deja que la posición se asiente (20 lecturas seguidas, porque justo después de fijar el receptor da saltos de decenas o cientos de metros) y guarda esa posición como posición fija del repetidor. **Funciona en cualquier modo de trabajo** y no hay que activar nada antes: la operación enciende el GPS y **lo vuelve a apagar al terminar**. Va contando el proceso en la pantalla (satélites a la vista, muestras, barra) y **se puede cancelar con el botón** si no quieres esperar
- **Dormir entre balizas** para excursiones largas sin batería

## Mensajes
- **Manda mensajes de texto a otra estación** por radio, como un SMS por APRS: desde el configurador web, por comandos o desde la propia pantalla del nodo
- **Mensaje guardado en el nodo**: se escribe una vez (por web o menú) y se envía al último escuchado con un toque
- **Reintenta solo si no le contestan**: repite el mensaje cada 30 segundos (las veces que le digas, 3 de fábrica) y avisa en pantalla cuando llega el acuse o cuando se rinde
- **Avisa de lo que llega**: acuses, mensajes de otras estaciones y boletines salen en pantalla y quedan en el registro
- **Boletines**: un mensaje para todos (BLN0-BLN9), útil para avisos en una quedada o un evento
- **Objetos**: publica un punto con nombre en el mapa de los demás (un puesto de control, un repetidor portátil, un campamento) y bórralo cuando quieras

## Registro de viaje
- **Guarda en la memoria interna** balizas, repetidos, recepciones y eventos, con hora y fecha del GPS
- **Sobrevive a apagones y reinicios**: si se corta la alimentación mientras escribe, no se corrompe
- **Se recicla solo**: al llenarse borra lo más antiguo y sigue grabando
- **Se activa en modo rastreador y en modo ambos** (en repetidor puro está apagado a propósito), así el nodo fijo de casa no gasta memoria
- **Descarga desde el configurador web** y **exporta a GPX, KML y CSV**
- **Resumen automático** del viaje: puntos, distancia total, repetidos y recepciones

## Sensores
- **Autodetección** de los sensores conectados al arrancar, con reintentos si alguno tarda en responder (y sigue buscando cada minuto si no encuentra ninguno)
- **Combinaciones habituales**: BMP280 o BME280 (temperatura y presión, el BME añade humedad), **BME680** (además, calidad del aire), AHT20 (humedad) e INA219 (tensión y consumo)
- **Funciona sin sensores**: la batería se mide con lo que la propia placa ya trae
- **Medición de batería filtrada**, para que un pico de transmisión no confunda al sistema
- **En la baliza solo sale lo que realmente mide**: si falta un sensor, ese dato no se inventa

## Pantalla
- **8 pantallas**: estado, últimas recibidas, últimas emitidas, radio, sensores, sistema, estaciones oídas y rastreo
- **Cambio automático** cada pocos segundos, o a toque
- **Menú por categorías** manejable **con un solo botón**: modo, GPS, balizas, rastreador, radio, repetidor, APRS, sensores, pantalla, energía, remoto y ajustes
- **Avisos en pantalla** de lo que hace el nodo: baliza emitida, trama recibida, repetido, silencio activado…
- **Indicadores** de emisión/recepción, batería, GPS y silencio
- **Pantalla de arranque** con nombre, versión, fecha y modo de trabajo
- **Distancia y rumbo** a las estaciones que oye (si hay GPS)

## Autonomía y nodo desatendido
- **Firmware totalmente resiliente**: el nodo se **duerme y despierta solo según umbrales de voltaje**, sin que nadie tenga que tocarlo
- **Se va a dormir con la batería baja** y **despierta cuando el sol recupera la tensión**; si no hay sol, sigue durmiendo
- **Nunca se duerme por un dato falso**: exige varias lecturas seguidas antes de decidir
- **Avisa por radio** antes de dormirse y al volver
- **Dormir entre balizas** en modo rastreador
- **Protección frente a cortes de alimentación**: no escribe en memoria con la batería baja, y si algo no responde bien, se recupera solo en lugar de quedarse bloqueado

## Configuración
- **Configurador web** desde Chrome o Edge por cable USB: todos los ajustes, con lectura y verificación
- **Ayuda en cada casilla**: un botoncito «?» al lado de cada opción explica para qué sirve, en lenguaje claro (en español y en inglés)
- **Botón «Recuperar valores recomendados»**: rellena el formulario con los ajustes habituales de la red para dejar un nodo listo en un minuto (el indicativo y las coordenadas los pones tú; nada se envía hasta que pulses Guardar)
- **Botón «Usar coordenadas del GPS»** en la latitud: copia la posición del GPS al formulario cuando el nodo la tiene fija (cómodo para un repetidor fijo)
- **Saltos configurables por separado** para repetidor, rastreador y modo ambos, cada uno con su valor recomendado
- **Avisa de lo que necesita reinicio** (frecuencia, velocidad, ancho de banda, potencia) y marca en rojo el dato que esté mal antes de guardar
- **Si el nodo no entiende un valor, lo dice**: cuando una clave conocida llega con un valor de un tipo que no le encaja, el nodo **no cambia ese ajuste** y lo avisa (respuesta `ignorado (tipo incorrecto): …` y anotación en el registro), en vez de contestar que todo ha ido bien y dejarlo como estaba
- **Comandos de texto por USB**, con ayuda integrada
- **Menú en la propia pantalla** del nodo (las rutas se eligen a toques, sin escribir)
- **Todo se guarda en memoria** y sobrevive a apagones y actualizaciones
- **Exportar e importar** la configuración en un archivo

## Control y comunicación
- **Control remoto por radio**: mandar comandos al nodo desde otro equipo APRS, con lista de operadores autorizados
- **Acuse de recibo** en las respuestas
- **Mensajes a otras estaciones**, con acuse automático cuando el destinatario lo pide
- **Contesta a las consultas APRS** más habituales sobre posición, estado, estaciones oídas y calidad de señal (incluidas las que preguntan qué estaciones ha oído en las últimas horas)
- **Modo módem por USB (TNC)**: convierte el nodo en un módem para programas de APRS del ordenador o del móvil, con cable (por ejemplo APRSdroid con cable OTG). Tres posiciones: **apagado**, **TNC2** (formato de texto clásico) y **KISS** (binario, el que piden APRSdroid, APRSIS32 y la LoRa APRS App). En KISS **manda la aplicación**: el nodo deja de enviar sus propias balizas, telemetría y meteorología (solo repite lo que oye y transmite lo que le manda la app); el modo TNC2 no cambia nada de eso. El configurador y la consola siguen funcionando en los tres modos, así que nunca te quedas sin poder cambiarlo
- **KISS por Bluetooth (BLE)** ⚠️ **NO DISPONIBLE EN ESTA VERSIÓN** (2026-09-13): el código de Bluetooth está **fuera de la compilación** porque al enlazarlo el nodo se quedaba **sin pantalla, sin radio y sin USB**. Los ajustes (interruptor y PIN) siguen guardándose, pero **no hacen nada**. Cuando vuelva, será como se describe aquí: el nodo hablará KISS **sin cable**, por el servicio NUS que usan la LoRa APRS App y las apps de APRS por Bluetooth, se anunciará con el nombre **«Faketec APRS» + tu indicativo**, será **solo otra forma de hablar con el nodo** (como el cable: no cambia lo que transmite) y para emparejar **enseñará el PIN en su pantalla** (6 cifras, de fábrica `123456`). **No cuentes con el Bluetooth hoy**: usa el cable.
- **Prueba del módem sin móvil**: `tools/kiss_client.js` (Node, sin instalar nada) lista los puertos, escucha y traduce las tramas que manda el nodo y envía una trama escrita a mano, para comprobar KISS con un cable y un ordenador
- **Prueba del Bluetooth desde el PC**: `node tools/ble_kiss_server.js` sirve `tools/ble_kiss.html` en `http://localhost:8099/`; esa página habla Web Bluetooth (Chrome/Edge) y deja en `logs/` todo lo que llega y lo que se envía
- **Recuerda**: los indicativos de AX.25 son de **6 caracteres** como máximo y los SSID van de **0 a 15**; el nodo avisa si la app pide algo que no cabe, en vez de recortarlo
- **Diagnóstico en vivo por USB**: estado completo una vez por segundo, cada trama con su hora exacta y volcado de las sentencias del GPS

## Instalación
- **Placas Faketec V1-V6** (nRF52840) con radio **HT-RA62** o **E22P-433M30S**
- **GPS** con encendido y apagado controlados por el firmware
- **Grabación por doble clic** (archivo UF2) o **por software**, sin abrir la caja ni pulsar botones
- **Licencia GPL-3.0**: software libre, con el código disponible
