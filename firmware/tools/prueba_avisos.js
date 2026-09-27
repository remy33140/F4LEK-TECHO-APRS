/* Prueba de los AVISOS del configurador web (updateNotes).
 *
 * PARA QUE SIRVE: el comprobador de la web (web_check.js) comprueba sintaxis y
 * textos, pero NO prueba que los avisos salgan cuando deben. Esto si: coge el
 * codigo de avisos TAL CUAL esta en index.html, lo ejecuta con varias
 * configuraciones y comprueba que avisa de lo que tiene que avisar... y que NO
 * avisa cuando la configuracion esta bien (un aviso de mas es tan malo como uno
 * de menos: el usuario deja de creerselos).
 *
 * Uso:  node tools/prueba_avisos.js
 * License: GPL-3.0
 */
const fs = require("fs");
const path = require("path");

const html = fs.readFileSync(path.join(__dirname, "..", "web", "index.html"), "utf8");
const js = html.match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];

/* Se saca del fichero real el trozo que decide CUANDO se avisa: el INTERIOR de
   updateNotes (sin la linea "function updateNotes(){" ni su llave final). Asi la
   prueba usa el codigo de verdad y no puede quedarse vieja.
   OJO, que esto me costo un rato: si se deja la linea "function ...", la prueba
   crea una funcion DENTRO de otra y no se ejecuta nada (todo sale vacio y parece
   que la logica esta mal cuando lo que esta mal es el banco de pruebas). */
const ini = js.indexOf("function updateNotes(){");
if (ini < 0) { console.error("FALLO: no encuentro updateNotes() en index.html"); process.exit(1); }
const abre = js.indexOf("{", ini);
let prof = 0, cierra = -1;
for (let i = abre; i < js.length; i++) {
  if (js[i] === "{") prof++;
  else if (js[i] === "}") { prof--; if (prof === 0) { cierra = i; break; } }
}
if (cierra < 0) { console.error("FALLO: no encuentro el final de updateNotes()"); process.exit(1); }
const cuerpo = js.slice(abre + 1, cierra)
  // El cuerpo real empieza con "if (!box || !list) return;": en la pagina sirve
  // para no reventar si aun no existe el panel, pero aqui haria que la funcion
  // saliera sin decir nada. Se quita SOLO esa guarda (nada mas).
  .replace("if (!box || !list) return;", "");

/* La funcion real escribe en la pagina y traduce. Aqui se le dan versiones
   tontas: "t" devuelve la CLAVE del texto (asi comprobamos QUE aviso sale) y el
   "document" falso apunta en CAP cada aviso que se anade a la lista.
   DETALLE QUE ME COSTO UN RATO: el nombre del parametro NO puede ser "list",
   porque el propio cuerpo declara "const list" y el motor se queja de que ya
   existe ("Identifier 'list' has already been declared"). Por eso la captura se
   hace en CAP y la funcion devuelve CAP. */
const arranque = `
  const CAP = [];
  const $ = ()=> ({ style: {}, textContent: "", appendChild: ()=>{} });
  const t = (k)=> k;
  const document = { createElement: ()=>({ set textContent(v){ CAP.push(v); } }) };
`;
const updateNotes = new Function("collectConfig",
  arranque + "\n" + cuerpo + "\nreturn CAP;");

/* --- configuracion base: un nodo bien puesto (no debe avisar de nada) --- */
const BIEN = {
  callsign: "EA2ABC-7", mode: 0, latitude: 42.8, longitude: -1.6,
  frequency: 433775000, spreadingFactor: 12, signalBandwidth: 125,
  pathDigi: "WIDE1-1", pathTracker: "WIDE1-1,WIDE2-1", pathBoth: "WIDE1-1,WIDE2-1",
  posAmbiguity: 0, beaconInterval: 15, trackerSleep: false,
  gpsEco: false, gpsInDigi: false, bleEnabled: false,
  queriesEnabled: false, remoteEnabled: false,
};
const cfg = (extra) => Object.assign({}, BIEN, extra);
const corre = (c) => updateNotes(() => c);

const CASOS = [
  ["nodo bien puesto (no debe avisar de nada)", cfg({}), []],
  ["sin indicativo", cfg({ callsign: "" }), ["note_no_call"]],
  ["indicativo de fabrica", cfg({ callsign: "NOCALL-11" }), ["note_default_call"]],
  ["coordenadas a cero en repetidor", cfg({ latitude: 0 }), ["note_no_coords"]],
  ["coordenadas a cero pero con GPS puesto", cfg({ latitude: 0, gpsInDigi: true }), []],
  ["frecuencia que no es la de la red", cfg({ frequency: 433900000 }), ["note_freq"]],
  ["SF distinto de 12", cfg({ spreadingFactor: 9 }), ["note_sf"]],
  ["ancho de banda distinto de 125", cfg({ signalBandwidth: 250 }), ["note_sf"]],
  ["fijo pidiendo WIDE2-2", cfg({ mode: 0, pathDigi: "WIDE2-2" }), ["note_hops_fixed"]],
  ["movil sin WIDE2-1", cfg({ mode: 1, pathTracker: "WIDE1-1" }), ["note_hops_mobile"]],
  ["ambos sin WIDE2-1", cfg({ mode: 2, pathBoth: "WIDE1-1" }), ["note_hops_mobile"]],
  ["posicion difuminada en movil", cfg({ mode: 1, posAmbiguity: 2 }), ["note_ambiguity"]],
  ["baliza cada 5 min", cfg({ beaconInterval: 5 }), ["note_interval"]],
  ["dormir en modo ambos", cfg({ mode: 2, trackerSleep: true }), ["note_sleep_both"]],
  ["ajustes de GPS fuera de repetidor", cfg({ mode: 1, gpsEco: true }), ["note_gps_not_digi"]],
  ["bluetooth encendido (esta muerto)", cfg({ bleEnabled: true }), ["note_ble_off"]],
  ["consultas sin control remoto", cfg({ queriesEnabled: true }), ["note_queries"]],
  ["comentario de mas de 64 (el nodo lo corta)", cfg({ comment: "x".repeat(70) }), ["note_too_long_comment"]],
  ["comentario de 64 justos (cabe, no debe avisar)", cfg({ comment: "x".repeat(64) }), []],
  ["mensaje de estado de mas de 64", cfg({ status: "x".repeat(70) }), ["note_too_long_status"]],
  ["mensaje rapido de mas de 40", cfg({ msgText: "x".repeat(45) }), ["note_too_long_msg"]],
  ["varios problemas a la vez",
    cfg({ callsign: "", frequency: 433900000, bleEnabled: true }),
    ["note_no_call", "note_freq", "note_ble_off"]],
];

let fallos = 0;
for (const [nombre, c, esperados] of CASOS) {
  const salida = corre(c) || [];
  const faltan = esperados.filter((e) => !salida.includes(e));
  const sobran = salida.filter((s) => !esperados.includes(s));
  const ok = faltan.length === 0 && sobran.length === 0;
  if (!ok) fallos++;
  console.log(`  ${ok ? "OK   " : "FALLO"} ${nombre}`);
  if (!ok) {
    if (faltan.length) console.log(`         no avisa de: ${faltan.join(", ")}`);
    if (sobran.length) console.log(`         avisa de mas: ${sobran.join(", ")}`);
  }
}

/* Cada aviso tiene que tener texto en los DOS idiomas: si se anade una regla y
   se olvida la traduccion, el usuario veria "undefined" en pantalla. */
const textos = js.match(/^\s*note_[a-z_]+:/gm) || [];
const cuenta = {};
for (const s of textos) { const k = s.trim().slice(0, -1); cuenta[k] = (cuenta[k] || 0) + 1; }
const unicos = Object.keys(cuenta);
const sinPar = unicos.filter((k) => cuenta[k] < 2);
console.log("");
console.log(`  textos de aviso definidos: ${unicos.length}`);
if (sinPar.length) { console.log(`  FALLO: sin texto en los dos idiomas: ${sinPar.join(", ")}`); fallos++; }
else console.log("  OK    todos tienen texto en español y en inglés");

/* Y todos los avisos que la funcion PUEDE dar tienen que tener texto: se sacan
   las claves realmente usadas en el codigo y se comparan con las definidas. */
const usadas = [...new Set((cuerpo.match(/t\("(note_[a-z_]+)"\)/g) || [])
  .map((s) => s.slice(3, -2)))];
const huerfanas = usadas.filter((k) => !unicos.includes(k));
console.log(`  avisos que puede dar el codigo: ${usadas.length}`);
if (huerfanas.length) { console.log(`  FALLO: avisos sin texto: ${huerfanas.join(", ")}`); fallos++; }
else console.log("  OK    todos los avisos del codigo tienen su texto");

console.log("");
console.log(fallos === 0 ? "TODO CORRECTO" : `${fallos} PROBLEMA(S)`);
process.exit(fallos === 0 ? 0 : 1);
