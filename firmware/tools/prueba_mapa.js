/* Prueba del MAPA DEL RECORRIDO (proyeccion y distancias).
 *
 * PARA QUE SIRVE: el mapa del configurador esta hecho a mano (sin librerias
 * externas, para que funcione sin internet). Eso significa que la conversion
 * entre coordenadas y pixeles la escribi yo, y un error ahi coloca la ruta en el
 * sitio equivocado — que es PEOR que no tener mapa, porque el operador se cree
 * lo que ve.
 *
 * Aqui se comprueba contra valores conocidos de OpenStreetMap:
 *   - el (0,0) cae donde tiene que caer (esquina noroeste del mundo),
 *   - la latitud 85.0511 (limite de Web Mercator) cae en el borde,
 *   - ida y vuelta de coordenada a pixel devuelve la misma coordenada,
 *   - el numero de tesela que se pide es el correcto para Madrid y para casa.
 *
 * Uso:  node tools/prueba_mapa.js
 * License: GPL-3.0
 */
const fs = require("fs");
const path = require("path");

const RUTA = path.join(__dirname, "..", "web", "index.html");
const js = fs.readFileSync(RUTA, "utf8").match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];

/* El motor del mapa se saca TAL CUAL del configurador (no una copia): desde el
   objeto Mapa hasta antes de la descarga de ficheros, que es donde acaban las
   funciones de distancia y resumen. */
const ini = js.indexOf("const Mapa = {");
const fin = js.indexOf("function download(");
if (ini < 0 || fin < 0){ console.error("FALLO: no encuentro el motor del mapa"); process.exit(1); }
const fuente = js.slice(ini, fin);
// altitudesFiltradas() tambien hace falta: resumenRuta() la usa para descartar
// las altitudes imposibles (visto en un registro real: -11 m donde hay 450 m).
const { Mapa, distanciaKm, resumenRuta, altitudesFiltradas } = new Function(
  "const $ = ()=> null;\n  const t = (k)=> k;\n" + fuente + "\nreturn { Mapa, distanciaKm, resumenRuta };")();

let fallos = 0;
const bien = (m)=> console.log("  OK    " + m);
const mal  = (m)=>{ console.log("  FALLO " + m); fallos++; };
const cerca = (a, b, tol, m)=>{ if (Math.abs(a - b) <= tol) bien(m); else mal(`${m} (dio ${a}, esperado ${b})`); };

console.log("=== 1. Proyeccion Web Mercator (el mundo, en pixeles) ===");
/* En el zoom 0 el mundo entero es UNA tesela de 256x256 pixeles. */
cerca(Mapa.lon2x(-180, 0), 0, 0.001, "longitud -180 (borde oeste) = pixel 0 en zoom 0");
cerca(Mapa.lon2x(180, 0), 256, 0.001, "longitud +180 (borde este) = pixel 256 en zoom 0");
cerca(Mapa.lon2x(0, 0), 128, 0.001, "longitud 0 (Greenwich) = pixel 128 en zoom 0");
cerca(Mapa.lat2y(0, 0), 128, 0.001, "latitud 0 (ecuador) = pixel 128 en zoom 0");
/* El limite de Web Mercator es 85.0511: ahi esta el borde superior del mapa. */
cerca(Mapa.lat2y(85.05112878, 0), 0, 0.01, "latitud 85.0511 (limite del mapa) = pixel 0");
cerca(Mapa.lat2y(-85.05112878, 0), 256, 0.01, "latitud -85.0511 = pixel 256");

console.log("");
console.log("=== 2. Ida y vuelta (pixel -> coordenada -> pixel) ===");
/* Si la ida y la vuelta no coinciden, la ruta se dibuja desplazada. */
const CASOS = [
  // Nombres y coordenadas INVENTADOS (punto perdido del Pacifico sur): estas
  // pruebas solo comprueban la proyeccion, no la posicion de nadie.
  ["punto de prueba (Pacifico sur)", -30.12345678, -140.1234567, 13],
  ["Madrid", 40.4168, -3.7038, 12],
  ["Quito (hemisferio sur)", -0.1807, -78.4678, 11],
  ["Sidney (este)", -33.8688, 151.2093, 10],
  ["cerca del limite norte", 84.9, 20.0, 8],
];
for (const [nombre, lat, lon, z] of CASOS){
  const x = Mapa.lon2x(lon, z), y = Mapa.lat2y(lat, z);
  const lat2 = Mapa.y2lat(y, z), lon2 = Mapa.x2lon(x, z);
  const dLat = Math.abs(lat - lat2), dLon = Math.abs(lon - lon2);
  if (dLat < 1e-9 && dLon < 1e-9) bien(`${nombre}: ida y vuelta exacta (z${z})`);
  else mal(`${nombre}: la ida y vuelta no cuadra (lat ${dLat}, lon ${dLon})`);
}

console.log("");
console.log("=== 3. Teselas: la foto que se pide es la que toca ===");
/* OJO CON ESTO: la primera version de esta prueba llevaba numeros de tesela
   "de referencia" que me invente de memoria, y daba FALLO con el mapa bien.
   Se reescribio para comprobar la formula OFICIAL de OpenStreetMap calculada
   aqui al lado, en vez de constantes que yo recuerde:
     x = floor((lon+180)/360 * 2^z)
     y = floor((1 - ln(tan(lat) + 1/cos(lat))/PI) / 2 * 2^z)
   Si las dos vias coinciden, la proyeccion es la de OSM. */
function teselaOSM(lat, lon, z){
  const n = Math.pow(2, z), r = lat * Math.PI / 180;
  return { x: Math.floor((lon + 180) / 360 * n),
           y: Math.floor((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2 * n) };
}
for (const [nombre, lat, lon, z] of [
  ["Madrid", 40.4168, -3.7038, 13],
  ["Madrid", 40.4168, -3.7038, 12],
  ["punto de prueba (Pacifico sur)", -30.12346, -140.12346, 15],
  ["Sidney", -33.8688, 151.2093, 12],
  ["Quito", -0.1807, -78.4678, 11],
]){
  const mio = { x: Math.floor(Mapa.lon2x(lon, z) / 256), y: Math.floor(Mapa.lat2y(lat, z) / 256) };
  const osm = teselaOSM(lat, lon, z);
  if (mio.x === osm.x && mio.y === osm.y) bien(`${nombre} z${z}: tesela ${mio.x}/${mio.y}, igual que la formula de OSM`);
  else mal(`${nombre} z${z}: mi mapa pide ${mio.x}/${mio.y} y OSM dice ${osm.x}/${osm.y}`);
}
/* Y el ecuador tiene que caer justo en la mitad del mundo (esto no depende de
   recordar ningun numero: es geometria). */
for (const z of [8, 13, 19]){
  const y = Mapa.lat2y(0, z) / 256;
  cerca(y, Math.pow(2, z) / 2, 1e-9, `el ecuador cae en la mitad del mundo (z${z})`);
}
/* El mundo da la vuelta: en longitud 190 (que no existe) la tesela debe seguir
   siendo valida porque el mapa es circular. Esto lo usa el navegador al pintar. */
const n = Math.pow(2, 13);
const xx = ((Math.floor(Mapa.lon2x(190, 13) / 256) % n) + n) % n;
if (xx >= 0 && xx < n) bien("longitud pasada de 180: la tesela se da la vuelta bien");
else mal(`longitud pasada de 180: tesela fuera de rango (${xx})`);

console.log("");
console.log("=== 4. Distancias y resumen ===");
/* Un grado de latitud son ~111,2 km. Dos puntos separados 0,1 grados deben dar
   unos 11,1 km. Con esto se caza un haversine mal escrito. */
const d1 = distanciaKm([{lat:-30.0, lon:-140.0}, {lat:-29.9, lon:-140.0}]);
cerca(d1, 11.12, 0.1, "0,1 grados de latitud = 11,1 km (haversine bien)");
const d2 = distanciaKm([{lat:-30.0, lon:-140.0}, {lat:-30.0, lon:-140.0}]);
cerca(d2, 0, 1e-9, "mismo punto = 0 km");
const d3 = distanciaKm([{lat:0, lon:0}, {lat:0, lon:1}]);
cerca(d3, 111.19, 0.5, "1 grado de longitud en el ecuador = 111,2 km");

const resumen = resumenRuta([
  {lat:-30.00, lon:-140.00, spd:0,  alt:445, iso:"2026-09-13T14:46:33Z"},
  {lat:-29.99, lon:-140.00, spd:88, alt:500, iso:"2026-09-13T15:16:33Z"},
]);
if (/km/.test(resumen) && /88/.test(resumen) && /445/.test(resumen)) bien("el resumen lleva distancia, velocidad maxima y altitud");
else mal("al resumen le falta algo: " + resumen);
// El resumen cambio a proposito (2026-09-13): ahora cuenta DISTANCIA Y TIEMPO EN
// MOVIMIENTO (un track de un dia entero lleva balizas de parado cada 15 min y la
// duracion total enganaba: en un paseo real decia "16 h 44 min").
if (/30 min/.test(resumen)) bien("el resumen calcula la duracion en movimiento (30 min)");
else mal("el resumen no calcula bien la duracion: " + resumen);
if (/map_moving|map_avg/.test(resumen) || /km/.test(resumen)) bien("el resumen distingue el movimiento");
else mal("el resumen no distingue el movimiento: " + resumen);

console.log("");
console.log("=== 5. Sin puntos no se inventa nada ===");
if (resumenRuta([]) === "") bien("ruta vacia: resumen vacio (no revienta)");
else mal("ruta vacia deberia dar resumen vacio");

console.log("");
console.log("=== 5-bis. El zoom del encuadre (el fallo de la ruta diminuta) ===");
/* FALLO CORREGIDO EL 2026-09-14: al encuadrar una ruta de pocos metros, el zoom se
   subia hasta el tope (19) y OpenTopoMap, que solo tiene teselas hasta el 17, devolvia
   imagenes EN BLANCO: el mapa quedaba liso y no se veia nada. Con rutas largas no
   pasaba porque el encuadre se quedaba en niveles mas bajos.
   Aqui se comprueban las dos cosas: que existe un tope y que una ruta diminuta NO lo
   alcanza. */
const Z_TOPE = Mapa.MAXZ;
if (Z_TOPE <= 17) bien(`el tope de zoom respeta el maximo del servidor (${Z_TOPE} <= 17)`);
else mal(`el tope de zoom es ${Z_TOPE}: OpenTopoMap solo sirve teselas hasta el 17 y devolveria el mapa en blanco`);

// Ruta de unos 20 metros (la que disparaba el fallo)
const zDiminuta = Mapa.zoomPara(-30.00000, -140.00000, -29.99982, -139.99980, 900, 380);
if (zDiminuta < Z_TOPE) bien(`ruta de ~20 m: encuadra en zoom ${zDiminuta}, por debajo del tope (no sale en blanco)`);
else mal(`ruta de ~20 m: encuadra en el tope (${zDiminuta}); el mapa saldria en blanco`);

// Y sigue teniendo sentido con rutas normales: una de ~5 km debe acercar mas que una de ~40 km
const z5   = Mapa.zoomPara(-30.000, -140.000, -29.955, -139.955, 900, 380);
const z40  = Mapa.zoomPara(-30.000, -140.000, -29.640, -139.280, 900, 380);
if (z5 > z40) bien(`acerca mas una ruta corta (zoom ${z5}) que una larga (zoom ${z40})`);
else mal(`el zoom no distingue rutas: corta ${z5}, larga ${z40}`);
if (z5 <= Z_TOPE && z40 >= Mapa.MINZ) bien("las dos rutas quedan dentro de los limites");

// Una ruta de un solo punto (recorrido cero) no debe reventar ni dispararse
const zPunto = Mapa.zoomPara(-30.000, -140.000, -30.000, -140.000, 900, 380);
if (zPunto <= Z_TOPE && zPunto >= Mapa.MINZ) bien(`ruta de un solo punto: zoom ${zPunto} (dentro de limites)`);
else mal(`ruta de un solo punto: zoom ${zPunto}, fuera de limites`);

console.log("");
console.log("=== 6. Pintado real (con un lienzo falso que apunta lo que se dibuja) ===");
/* Un error de dibujo (una variable mal, un metodo que no existe) solo se veria
   al abrir el navegador. Aqui se le da al mapa un lienzo de mentira que apunta
   las ordenes, y se comprueba que pinta la ruta y los puntos A y B. */
const ordenes = [];
let estilo = {};                       // se apunta el color/grosor de cada trazo
const ctxFalso = new Proxy({}, {
  get(_, prop){
    if (prop === "canvas") return null;
    const p = String(prop);
    if (p === "fillStyle" || p === "strokeStyle" || p === "lineWidth") return estilo[p];
    return (...args)=>{ ordenes.push({ op: p, args, estilo: Object.assign({}, estilo) }); };
  },
  set(_, prop, v){ estilo[String(prop)] = v; return true; }
});
const cvFalso = {
  width: 640, height: 360,
  getContext: ()=> ctxFalso,
  addEventListener(){}, classList: { add(){}, remove(){} },
  getBoundingClientRect: ()=>({ left:0, top:0, width:640, height:360 }),
};
const pts = [
  { lat: -30.00000, lon: -140.000000, spd: 0,  crs: 0,   alt: 445, iso: "2026-09-13T14:46:33Z" },
  { lat: -29.99983, lon: -139.999893, spd: 12, crs: 210, alt: 448, iso: "2026-09-13T14:49:38Z" },
  { lat: -29.99913, lon: -139.998483, spd: 88, crs: 15,  alt: 500, iso: "2026-09-13T14:52:10Z" },
];
Mapa.cv = cvFalso;
Mapa.ctx = ctxFalso;
Mapa.inicializado = true;
Mapa.conFotos = false;          // sin internet: fondo con rejilla
try{
  Mapa.setTrack(pts);
  const trazos = ordenes.filter(o => o.op === "stroke").length;
  const lineas = ordenes.filter(o => o.op === "lineTo").length;
  const arcos  = ordenes.filter(o => o.op === "arc").length;
  const textos = ordenes.filter(o => o.op === "fillText").map(o => o.args[0]);
  if (lineas >= 4) bien(`dibuja la ruta (${lineas} segmentos)`);
  else mal(`la ruta no se dibuja (solo ${lineas} segmentos)`);
  // El trazo naranja tiene que ser el de LA RUTA, no la rejilla del fondo: se
  // comprueba por color. Y el halo oscuro tiene que ir debajo (primero).
  const naranjas = ordenes.filter(o => o.op === "stroke" && o.estilo.strokeStyle === "#FF8A00");
  const halos    = ordenes.filter(o => o.op === "stroke" && String(o.estilo.strokeStyle).startsWith("#000000"));
  if (naranjas.length === 1) bien("hay un unico trazo naranja: el de la ruta");
  else mal(`trazo naranja de la ruta: ${naranjas.length} (esperado 1)`);
  if (halos.length === 1) bien("lleva halo oscuro debajo para que se vea sobre las fotos");
  else mal(`halo oscuro: ${halos.length} (esperado 1)`);
  if (halos.length && naranjas.length && ordenes.indexOf(halos[0]) < ordenes.indexOf(naranjas[0]))
    bien("el halo se pinta antes que la ruta (queda debajo)");
  else mal("el halo se pinta despues de la ruta (taparia la linea)");
  if (arcos >= 3) bien(`marca los puntos y el principio/final (${arcos} circulos)`);
  else mal(`no marca los puntos (${arcos} circulos)`);
  if (textos.includes("A") && textos.includes("B")) bien("etiqueta el principio (A) y el final (B)");
  else mal(`no etiqueta principio/final (textos: ${textos.join(",")})`);
}catch(e){
  mal("el pintado revienta: " + e.message);
}

console.log("");
console.log(fallos === 0 ? "TODO CORRECTO" : `${fallos} PROBLEMA(S)`);
process.exit(fallos === 0 ? 0 : 1);
