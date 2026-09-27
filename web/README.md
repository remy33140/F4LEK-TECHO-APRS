# EA2OY APRS — Web configurator / Configurador web

Single-file WebSerial configurator for the EA2OY APRS firmware (nRF52840 + SX1262).
Configurador WebSerial de un solo archivo para el firmware EA2OY APRS (nRF52840 + SX1262).

## Usage / Uso
1. Open `index.html` with **desktop Chrome or Edge** (WebSerial is not available in Firefox/Safari).
   Abre `index.html` con **Chrome o Edge de escritorio** (WebSerial no está en Firefox/Safari).
2. Click **Conectar / Connect** and pick the node's USB serial port (115200).
   Pulsa **Conectar** y elige el puerto USB del nodo (115200).
3. The page reads the real configuration from the node (`get`) and fills the form.
   La página lee la configuración real del nodo (`get`) y rellena el formulario.
4. Change what you need and press **Guardar en el nodo / Save to node**; the reply shows the
   values the node accepted (read-back, clamped values included).
   Cambia lo que necesites y pulsa **Guardar**; la respuesta muestra los valores aceptados
   (lectura de vuelta, incluidos los recortes del nodo).
5. **Actions**: beacon, telemetry, status, mute, reboot, factory reset, full wipe and DFU.
   **Factory reset** sends `{"cmd":"factory_reset"}`: it **erases the configuration** and
   returns the node to factory values; it is **not** a reboot (the reboot button sends
   `reboot confirm`). Destructive actions ask for typed confirmation (`CONFIRMAR` / `CONFIRM`).
   **Acciones**: baliza, telemetría, estado, mute, reinicio, reset de fabrica, borrado total
   y DFU. El **reset de fabrica** manda `{"cmd":"factory_reset"}`: **borra la configuracion**
   y devuelve el nodo a valores de origen; **no** es un reinicio (el boton de reiniciar manda
   `reboot confirm`).
   Las destructivas piden confirmación escrita.

Notes / Notas:
- Radio changes (frequency, SF, BW, power) are applied **on reboot**.
  Los cambios de radio (frecuencia, SF, BW, potencia) se aplican **al reiniciar**.
- **Serve it locally, and not only for WebSerial: the MAP needs it too.**
  `powershell -File tools\sirve_web.ps1` (or any static server) and open
  `http://localhost:8000/web/`.
  Si al abrir el archivo directo no aparece WebSerial, sírvelo en local:
  `python -m http.server` (o cualquier servidor estático) y abre `http://localhost:8000/web/`.

  **Why the map needs a local server** (OpenStreetMap tile usage policy, checked
  2026-09-13): OSM requires web requests to carry a valid `Referer` header and
  states that "offline use is not permitted". A page opened as `file:///...` sends
  no referer, so OSM blocks the tiles on purpose and the map shows no images. Over
  `http://localhost` the referer is sent and the use is the permitted one (a human
  looking at the map, only the tiles on screen, cached by the browser). The
  configurator says this on screen when it happens, and the track is always drawn
  even without map images.

  **Por qué el mapa necesita el servidor local** (política de uso de teselas de
  OpenStreetMap, consultada el 2026-09-13): OSM exige que las peticiones web lleven
  cabecera `Referer` y dice que «el uso sin conexión no está permitido». Una página
  abierta como `file:///...` no manda referer, así que OSM bloquea las teselas a
  propósito y el mapa se queda sin fotos. Por `http://localhost` sí se manda
  referer y el uso es el permitido (una persona mirando el mapa, solo las teselas
  de lo que ve, y el navegador las guarda en caché). El configurador lo avisa en
  pantalla cuando pasa, y la ruta se dibuja igual aunque no haya fotos.
- Protocol reference: `docs/protocol_config_v1.md`.
  Referencia del protocolo: `docs/protocol_config_v1.md`.

## Avisos del configurador / Configurator notes

The form **warns** as you type about things that are wrong or that would do nothing,
and it never blocks saving (only an out-of-range value is rejected, and that is the
node itself rejecting it). Examples: no callsign, coordinates at 0,0, a frequency
that is not the LoRa APRS one, too many hops for a fixed node, or GPS settings in a
mode where they do nothing. It also tells you when a section is **inert** (not used
by the current firmware, like Bluetooth).

El formulario **avisa** mientras escribes de las cosas que están mal o que no harían
nada, y **nunca impide guardar** (solo rechaza un valor fuera de rango, y eso lo
rechaza el propio nodo). Ejemplos: sin indicativo, coordenadas a 0,0, una frecuencia
que no es la de LoRa APRS, demasiados saltos para un nodo fijo, o ajustes de GPS en
un modo donde no hacen nada. También avisa cuando una sección es **inerte** (que el
firmware actual no usa, como el Bluetooth).

## Comprobaciones / Checks

Two checkers, both run from the project root:
Dos comprobadores, los dos desde la raíz del proyecto:

```
node tools/web_check.js        # sintaxis, campos con ayuda y traducciones es/en
node tools/prueba_avisos.js    # los avisos: 18 casos, usa el codigo real del formulario
node tools/prueba_registro.js  # registro de viaje: parseo y GPX/KML/CSV
node tools/prueba_mapa.js      # mapa: proyeccion contra la formula de OSM y pintado
```

`web_check.js` catches syntax errors, fields without help text and missing
translations. `prueba_registro.js` checks the trip-log parser and the GPX/KML/CSV exports against
real firmware log lines: positions are recognised (including negative altitude),
events are not mistaken for positions, and the GPX/KML come out well formed with
the units each format expects (GPX speed in m/s, KML coordinates lon,lat,alt).
`prueba_mapa.js` checks the hand-made map against the **official OpenStreetMap tile
formula** (it caught nothing in the map but it caught made-up reference numbers in
the test itself, which is exactly why it exists) and paints the track on a fake
canvas to make sure the route, the points and the A/B markers are really drawn.

`prueba_registro.js` comprueba el parser del registro de viaje y las exportaciones
GPX/KML/CSV con líneas reales del firmware: se reconocen las posiciones (incluida
la altitud negativa), los eventos no se confunden con posiciones, y el GPX y el KML
salen bien formados y con las unidades que pide cada formato (velocidad en m/s en
GPX, coordenadas lon,lat,alt en KML). `prueba_mapa.js` comprueba el mapa hecho a
mano contra la **fórmula oficial de teselas de OpenStreetMap** (no encontró ningún
fallo en el mapa, pero sí números de referencia inventados en la propia prueba, que
es justo para lo que sirve) y pinta la ruta en un lienzo falso para asegurar que se
dibujan la ruta, los puntos y las marcas A/B.

`prueba_avisos.js` takes the advisory code **straight out of
`index.html`** (so it cannot go stale), runs it against 18 configurations and checks
that each warning appears when it should — and, just as important, that it does
**not** appear when the configuration is fine. It also verifies that every warning
has text in both languages.

`web_check.js` caza errores de sintaxis, campos sin ayuda y traducciones que faltan.
`prueba_avisos.js` coge el código de avisos **tal cual está en `index.html`** (así no
se queda viejo), lo ejecuta con 18 configuraciones y comprueba que cada aviso sale
cuando toca y —igual de importante— que **no** sale cuando la configuración está
bien. También comprueba que cada aviso tiene texto en los dos idiomas.
