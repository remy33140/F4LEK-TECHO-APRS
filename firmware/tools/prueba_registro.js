/* Prueba del REGISTRO DE VIAJE: parseo y exportaciones (GPX / KML / CSV).
 *
 * PARA QUE SIRVE: el operador descargo su registro y solo le dejo bajar CSV
 * (los botones de GPX y KML estaban desactivados). Esos botones se activan solo
 * si el parser reconoce puntos de ruta, asi que aqui se comprueba, con lineas
 * REALES de las que escribe el firmware, que:
 *   1. las lineas de posicion se reconocen (y que formato tienen que tener),
 *   2. las demas (eventos, RX, DG, TLM...) NO se confunden con posiciones,
 *   3. el GPX y el KML salen bien formados y con lo que esperan Google Earth,
 *      Wikiloc y compania.
 *
 * El codigo NO se copia: se saca tal cual de web/index.html, para que la prueba
 * no pueda quedarse vieja respecto al configurador.
 *
 * Uso:  node tools/prueba_registro.js
 * License: GPL-3.0
 */
const fs = require("fs");
const path = require("path");

const RUTA = path.join(__dirname, "..", "web", "index.html");
const js = fs.readFileSync(RUTA, "utf8").match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];

/* --- se sacan del configurador las piezas que se quieren probar --- */
function trozo(desde, hasta){
  const i = js.indexOf(desde);
  const f = js.indexOf(hasta, i);
  if (i < 0 || f < 0) throw new Error("no encuentro el trozo: " + desde);
  return js.slice(i, f);
}
const fuente = trozo("const TRK_RE", "function download(");
const { parseLog, buildGpx, buildKml, buildCsv } = new Function(
  // t() se define DENTRO del codigo generado: el parser la usa para los titulos
  // de sesion, y asi la prueba no depende de como se pase.
  "const t = (k)=> k;\n" + fuente + `
  return { parseLog, buildGpx, buildKml, buildCsv };
`)();

/* --- lineas REALES del firmware (src/aprs.cpp, flogLine) ---
   Coordenadas INVENTADAS (punto perdido del Pacifico sur): el formato es el
   real, pero no son la posicion de nadie. */
const LINEAS = [
  "2026-09-13 14:46:33 TX TRK -29.99587,-140.023517 0.0km/h 0deg 445m 111b R ok",
  "2026-09-13 14:49:38 TX TRK -29.99570,-140.023410 12.5km/h 210deg 448m 111b D ok",
  "2026-09-13 14:52:10 TX TRK -29.99500,-140.022000 88.0km/h 15deg 1200m 111b C ok",
  "2026-09-13 15:16:35 TX TRK -29.99610,-140.024000 0.0km/h 180deg -31m 111b L ok",
  "2026-09-13 15:20:00 TX BCN pos -29.99600,-140.023000 44b ok",
  "2026-09-13 15:20:05 RX EA2OY-7 Bcn rssi-51 snr9.2",
  "2026-09-13 15:20:10 DG N0CALL-10 -> EA2OY-7* rssi-95 snr-3.2",
  "2026-09-13 15:20:15 TX TLM seq=4 Vbat=4.18",
  "2026-09-13 15:20:20 RX EA2OY-10 MSG <- hola",
  "2026-09-13 15:20:25 EVT boot v1.0alpha mode=2",
  "2026-09-13 15:20:30 TX STS En marcha 44b ok",
];

/* Lineas sin año: el nodo las escribe cuando el GPS aun no ha dado hora. */
const LINEAS_SIN_ANIO = [
  "15:30:00 TX TRK -29.99000,-140.020000 5.0km/h 90deg 450m 111b R ok",
];

let fallos = 0;
const mal = (m)=>{ console.log("  FALLO " + m); fallos++; };
const bien = (m)=> console.log("  OK    " + m);

/* --- 1. parseo ---
   parseLog() lee la variable global logLines (es como funciona en la pagina),
   asi que aqui se le da el valor por parametro. */
/* El parser devuelve SESIONES (una por encendido del nodo). Para las pruebas que
   miran el registro entero hay que sumar todas: `pts` a secas son las de la
   sesion mas reciente, que es lo que enseña el programa. */
const parse = (lineas)=>{
    // sesiones=[] declarado para que el parser no dependa de estado global al
  // construir los titulos (en la pagina lo declara el propio configurador).
  const fn = new Function("logLines", "let sesiones=[]; const t=(k)=>k;\n" + fuente + "\nreturn parseLog(logLines);");
  const r = fn(lineas);
  const todos = (r.sesiones || []).flatMap((s)=> s.pts);
  const eventos = (r.sesiones || []).flatMap((s)=> s.events);
  const raras = (r.sesiones || []).reduce((a,s)=> a + s.raras, 0);
  return { sesiones: r.sesiones, pts: todos, events: eventos, raras };
};

console.log("=== 1. Parseo del registro ===");
const r1 = parse(LINEAS);
console.log(`  lineas leidas: ${LINEAS.length}`);
console.log(`  puntos de ruta reconocidos: ${r1.pts.length} (esperados: 4)`);
console.log(`  lineas tratadas como evento: ${r1.events.length} (las de la sesion mostrada)`);
if (r1.pts.length !== 4) mal("no reconoce las 4 lineas de posicion");
else bien("reconoce las 4 lineas de posicion (incluida la de altitud negativa)");

/* la altitud negativa y los decimales de velocidad son los casos que rompen un
   parser descuidado, asi que se miran uno a uno */
const p3 = r1.pts[2];
if (p3.spd === 88 && p3.crs === 15 && p3.alt === 1200) bien("velocidad, rumbo y altitud con decimales: bien");
else mal(`velocidad/rumbo/altitud mal leidos: ${JSON.stringify(p3)}`);
const p4 = r1.pts[3];
if (p4.alt === -31) bien("altitud negativa (fix malo): bien");
else mal(`altitud negativa mal leida: ${JSON.stringify(p4)}`);
if (r1.pts[0].iso === "2026-09-13T14:46:33Z") bien("fecha y hora en formato ISO: bien");
else mal(`fecha mal montada: ${r1.pts[0].iso}`);

/* NINGUNA linea que no sea de posicion puede acabar como punto de ruta.
   Y ojo con las cuentas: el parser separa SESIONES y DESCARTA las que no tienen
   ninguna posicion (a proposito: en un registro real hay cientos de restos de
   arranque y llenarian el desplegable). En esta lista de ejemplo la linea
   "EVT boot" corta la sesion, asi que las ultimas lineas se van con una sesion
   sin posiciones. Por eso se comprueba lo que SI tiene que cuadrar: que las
   posiciones son 4 y que no hay ninguna sesion vacia en la lista. */
const puntos = r1.pts.length;
if (puntos === 4) bien("reconoce las 4 posiciones de la lista de ejemplo");
else mal(`posiciones: ${puntos}, esperadas 4`);
if ((r1.sesiones || []).every((s)=> s.pts.length > 0))
  bien("no queda ninguna sesion vacia en la lista (las de arranque se descartan)");
else mal("hay sesiones sin posiciones en la lista del desplegable");
const coladas = r1.events.length;
if (coladas > 0 && coladas < LINEAS.length) bien("las lineas de estado no se confunden con posiciones (" + coladas + " eventos)");
else mal(`eventos: ${coladas}, esperados entre 1 y ${LINEAS.length - 1}`);
const r2 = parse(LINEAS_SIN_ANIO);
if (r2.pts.length === 1 && r2.pts[0].iso === null) bien("linea sin año (GPS sin hora): se acepta, sin fecha");
else mal(`linea sin año mal tratada: ${JSON.stringify(r2.pts)}`);

/* --- 2. GPX --- */
console.log("");
console.log("=== 2. GPX (para Wikiloc, Strava, Garmin...) ===");
const gpx = buildGpx(r1.pts);
const nTrkpt = (gpx.match(/<trkpt /g) || []).length;
if (nTrkpt === 4) bien(`lleva los 4 puntos (${nTrkpt} <trkpt>)`);
else mal(`el GPX lleva ${nTrkpt} puntos, esperados 4`);
if (gpx.includes("<ele>") && gpx.includes("<time>") && gpx.includes("<speed>")) bien("lleva altitud, hora y velocidad");
else mal("al GPX le falta altitud, hora o velocidad");
if (gpx.trimEnd().endsWith("</gpx>")) bien("cierra bien el XML");
else mal("el GPX no cierra bien");
/* la velocidad en GPX va en m/s, no en km/h: 88 km/h = 24.44 m/s */
if (gpx.includes("<speed>24.44</speed>")) bien("la velocidad va en m/s como manda el GPX (88 km/h = 24.44)");
else mal("la velocidad del GPX no esta en m/s");

/* --- 3. KML --- */
console.log("");
console.log("=== 3. KML (para Google Earth) ===");
const kml = buildKml(r1.pts);
if (kml.includes("<LineString>") && kml.includes("<coordinates>")) bien("lleva la linea de la ruta");
else mal("al KML le falta la linea de la ruta");
if (kml.trimEnd().endsWith("</kml>")) bien("cierra bien el XML");
else mal("el KML no cierra bien");
/* en KML las coordenadas van lon,lat,alt (al reves que el GPX) */
if (kml.includes("-140.023517,-29.99587")) bien("las coordenadas van lon,lat como manda el KML");
else mal("las coordenadas del KML no estan en orden lon,lat");

/* --- 4. CSV --- */
console.log("");
console.log("=== 4. CSV (para hoja de calculo) ===");
const csv = buildCsv(LINEAS);
const filas = csv.trim().split("\n").length;
if (filas === LINEAS.length + 1) bien(`una fila por linea + cabecera (${filas})`);
else mal(`el CSV tiene ${filas} filas, esperadas ${LINEAS.length + 1}`);
if (csv.split("\n")[0].startsWith("fecha,hora,tipo")) bien("lleva cabecera");
else mal("al CSV le falta la cabecera");

/* --- 5. caso real: registro SIN posiciones (nodo parado en modo repetidor) ---
   Es importante que esto NO se confunda con un fallo del parser: si el nodo no
   se ha movido, no hay puntos de ruta y los botones de GPX/KML deben quedar
   apagados A PROPOSITO, diciendolo. */
console.log("");
console.log("=== 5. Registro sin posiciones ===");
const soloEventos = LINEAS.filter((l)=> !l.includes("TX TRK"));
const r3 = parse(soloEventos);
if (r3.pts.length === 0) bien("sin lineas de posicion: 0 puntos (correcto, no es un fallo)");
else mal(`deberia haber 0 puntos y hay ${r3.pts.length}`);

/* --- 6. REGISTRO REAL DE UN NODO (el que mando el operador) ---
   Esta es la prueba que importa: un volcado de verdad, con 2242 lineas y tres
   formatos de hora distintos (fecha completa, tiempo encendido "1s", y la fecha
   imposible "2000-00-00" que pone el GPS cuando aun no sabe el dia). Con el
   parser viejo se perdian posiciones; con este tienen que salir las 17. */
console.log("");
console.log("=== 6. Registro REAL del nodo (tools/ejemplo_registro_crudo.txt) ===");
const rutaReal = path.join(__dirname, "ejemplo_registro_crudo.txt");
if (!fs.existsSync(rutaReal)){
  console.log("  (no esta el ejemplo; se salta)");
} else {
  const real = fs.readFileSync(rutaReal, "utf8").split("\n").filter((l)=> l.length);
  const rr = parse(real);
  console.log(`  lineas: ${real.length} · puntos: ${rr.pts.length} · eventos: ${rr.events.length} · raras: ${rr.raras}`);
  if (rr.pts.length === 17) bien("saca las 17 posiciones del registro real");
  else mal(`deberia sacar 17 posiciones y saca ${rr.pts.length}`);
  const conHora = rr.pts.filter((p)=> p.iso !== null).length;
  if (conHora === 15) bien(`15 puntos con hora valida y 2 sin hora (los del GPS sin fecha)`);
  else mal(`puntos con hora: ${conHora}, esperados 15`);
  if (rr.pts.every((p)=> !String(p.iso).startsWith("2000-00-00"))) bien("la fecha imposible 2000-00-00 NO se cuela en los datos");
  else mal("se ha colado la fecha imposible 2000-00-00");
  const g = buildGpx(rr.pts);
  if (!g.includes("2000-00-00")) bien("el GPX del registro real sale sin fechas imposibles");
  else mal("el GPX lleva la fecha 2000-00-00 (rompe el fichero en otras apps)");
  if ((g.match(/<trkpt /g) || []).length === rr.pts.length) bien("el GPX lleva los 17 puntos");
  else mal("el GPX no lleva todos los puntos");
  if (rr.raras === 0) bien("no avisa de lineas raras: todas las de posicion se han podido leer");
  else mal(`raras = ${rr.raras}, esperadas 0 (todas las posiciones del registro real se leen)`);
}

/* --- 7. Posicion con hora de ENCENDIDO: no se puede fechar, pero NO se pierde --- */
console.log("");
console.log("=== 7. Posicion con hora de encendido (GPS sin hora todavia) ===");
const rUp = parse(["1s TX TRK -29.99587,-140.023517 5.0km/h 90deg 450m 105b ok F"]);
if (rUp.pts.length === 1 && rUp.pts[0].iso === null && rUp.pts[0].lat === -29.99587)
  bien("se recoge la posicion, sin hora (antes se perdia entera)");
else mal(`la posicion con hora de encendido se pierde: ${JSON.stringify(rUp.pts)}`);

/* --- 8. LEER DOS VECES: el mapa no puede quedarse con la ruta vieja ---
   Caso real que se cuela siempre: lees un registro con ruta, luego otro sin
   posiciones, y el mapa se queda ensenando la ruta ANTERIOR. Eso es peor que no
   ensenar nada: el operador cree que esa es la ruta de la ultima lectura.
   Aqui se ejecuta finishLog() de verdad, con un DOM de mentira. */
console.log("");
console.log("=== 8. Leer dos veces (el mapa no puede quedarse viejo) ===");
// Se reutiliza `fuente`, que ya trae TODO (los patrones, parseLog, el objeto
// Mapa, distanciaKm, resumenRuta y las exportaciones). Antes esta seccion montaba
// su propia lista de trozos y se dejaba fuera los patrones y el Mapa: la prueba
// decia "no hay puntos" con el programa perfecto. Una sola fuente de verdad.
// --- DOM de mentira: apunta lo que se ensena y que botones quedan activos ---
const el = {};
["logInfo","mapWrap","mapStats","btnLogGpx","btnLogKml","btnLogCsv","btnLogRaw",
 "mapCanvas","mapZoomInfo","cfgNotes","cfgNotesList","cfgNotesTitle"].forEach((id)=>{
  el[id] = { id, textContent: "", style: {}, disabled: false,
             classList: { add(){}, remove(){}, contains(){ return false } },
             addEventListener(){}, appendChild(){}, setAttribute(){} };
});
// El canvas necesita getContext(): el Mapa de verdad lo usa para pintar. Sin
// esto, Mapa.setTrack() revienta a media faena y el mapa se queda a medias
// (display='none' con la ruta ya leida): me costo un buen rato verlo, porque la
// excepcion no se ve y parece que "no hay puntos".
el.mapCanvas.width = 640;
el.mapCanvas.height = 360;
el.mapCanvas.getContext = ()=>({
  canvas: el.mapCanvas, fillStyle: "", strokeStyle: "", lineWidth: 1,
  font: "", lineJoin: "", lineCap: "",
  clearRect(){}, fillRect(){}, strokeRect(){}, drawImage(){}, beginPath(){},
  moveTo(){}, lineTo(){}, stroke(){}, fill(){}, arc(){}, fillText(){},
});
el.mapCanvas.getBoundingClientRect = ()=>({ left:0, top:0, width:640, height:360 });
// El entorno que ve el codigo de la pagina: $() y t() son los unicos enganches
// que no existen en Node. El objeto Mapa es el DE VERDAD (viene en `fuente`).
const ENV = {
  $: (id)=> el[id] || null,
  t: (k)=> k,
  toast: ()=>{},
  // Doble de Image: nunca carga (como si no hubiera internet).
  Image: function(){ this.onload = null; this.onerror = null;
                     setTimeout(()=>{ if (this.onerror) this.onerror(); }, 0); },
  document: { getElementById: (id)=> el[id] || null,
              createElement: ()=>({ style:{}, classList:{add(){},remove(){}},
                                    set textContent(v){}, appendChild(){} }),
              body: { appendChild(){} } },
};
const src = [
  "function $(id){ return ENV.$(id); }",
  // Image no existe en Node: el mapa pide las fotos del terreno con new Image().
  // Con este doble no carga ninguna (onerror), que es justo el caso "sin
  // internet": la ruta se dibuja igual sobre el fondo con rejilla.
  "const Image = ENV.Image;",
  "function t(k){ return ENV.t(k); }",
  "function toast(m){ ENV.toast(m); }",
  "function log(){}",
  fuente,
  // finishLog() va DESPUES de download() en la pagina, asi que no entra en
  // `fuente` (que acaba en download) y hay que traerla aparte. Sin esto: el
  // clasico "finishLog is not defined".
  trozo("function finishLog", "/* ================= MAPA"),
  // Y mostrarMapa(), que va justo despues de finishLog.
  trozo("function mostrarMapa", "/* ================= replies"),
].join("\n");

// OJO: logLines, lastTrack y logTimeout se declaran DENTRO de la funcion de
// prueba (no fuera), porque finishLog() los reasigna y los lee. Con const daria
// "Assignment to constant variable", y sin declararlos, "is not defined".
const corre = new Function("ENV", "lineasIn", "el",
  "let logLines = lineasIn;\nlet lastTrack = null;\nlet logTimeout = null;\n" + src +
  "\nfinishLog();\nreturn { el, mapa: (Mapa.pts || []) };");

// 1) primera lectura: CON posiciones
const conPuntos = [
  "2026-09-13 13:30:44 TX TRK -29.99562,-140.02250 0.0km/h 0deg 1470m 105b ok P",
  "2026-09-13 13:45:41 TX TRK -29.99548,-140.02251 0.0km/h 170deg 480m 105b ok P",
];
const lec1 = corre(ENV, conPuntos, el);
if (lec1.mapa.length === 2 && lec1.el.mapWrap.style.display === "") bien("lectura con ruta: el mapa se ensena y guarda 2 puntos");
else mal(`lectura con ruta: mapa=${lec1.mapa.length} display='${lec1.el.mapWrap.style.display}'`);

// 2) segunda lectura: SIN posiciones
const sinPuntos = ["2026-09-13 13:46:33 TX WX .../...g...t082h37b09731"];
const lec2 = corre(ENV, sinPuntos, el);
if (lec2.el.mapWrap.style.display === "none") bien("lectura sin ruta: el mapa se ESCONDE");
else mal(`lectura sin ruta: el mapa sigue visible (display='${lec2.el.mapWrap.style.display}')`);
if (lec2.mapa.length === 0) bien("y se olvida la ruta anterior (no se queda la vieja)");
else mal(`el mapa se queda con ${lec2.mapa.length} puntos de la lectura anterior`);
if (lec2.el.mapStats.textContent === "") bien("el resumen de la ruta vieja tambien se borra");
else mal("el resumen sigue ensenando datos de la lectura anterior");

// 3) registro VACIO: los botones de descarga tienen que quedar disponibles
const lec3 = corre(ENV, [], el);
if (lec3.el.btnLogRaw.disabled === false) bien("registro vacio: 'guardar tal cual' queda DISPONIBLE (para poder mirarlo)");
else mal("registro vacio: 'guardar tal cual' se queda bloqueado, justo cuando hace falta");
if (/log_none/.test(lec3.el.logInfo.textContent)) bien("y se avisa de que el nodo no tiene registro");
else mal("no avisa de que no hay registro");
if (lec3.el.btnLogGpx.disabled === true) bien("y GPX/KML siguen bloqueados (no hay puntos que exportar)");
else mal("GPX deberia estar bloqueado sin puntos");

console.log("");
console.log(fallos === 0 ? "TODO CORRECTO" : `${fallos} PROBLEMA(S)`);
process.exit(fallos === 0 ? 0 : 1);
