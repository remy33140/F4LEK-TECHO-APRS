#!/usr/bin/env node
// web_check.js — Comprobaciones del configurador web (sin navegador).
//
// Uso:  node tools/web_check.js
//
// Verifica:
//   1. Que el JavaScript de web/index.html no tiene errores de sintaxis.
//   2. Que TODOS los campos del formulario tienen texto de ayuda (HELP).
//   3. Que todas las claves de idioma usadas (t("...")) existen en es y en.
//   4. Que los botones con data-i18n tienen traducción.
// License: GPL-3.0

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const file = path.join(__dirname, "..", "web", "index.html");
const html = fs.readFileSync(file, "utf8");
let fails = 0;
const bad = (m) => { console.log("  FALLO: " + m); fails++; };

// --- 1. sintaxis del bloque <script> ---
const m = html.match(/<script>([\s\S]*?)<\/script>/);
if (!m) { bad("no encuentro el bloque <script>"); process.exit(1); }
try {
  new vm.Script(m[1], { filename: "index.html:script" });
  console.log("1. Sintaxis del JavaScript: OK");
} catch (e) {
  bad("sintaxis: " + e.message);
}

// --- 2. ayuda de cada campo ---
const schemaBlock = html.slice(html.indexOf("const SCHEMA = ["), html.indexOf("/* ================= state"));
const helpBlock = html.slice(html.indexOf("const HELP = {"), html.indexOf("/* ================= schema"));
const schemaKeys = [...schemaBlock.matchAll(/\{key:"([A-Za-z0-9_]+)"/g)].map((x) => x[1]);
const helpKeys = [...helpBlock.matchAll(/^  ([A-Za-z0-9_]+):\{/gm)].map((x) => x[1]);
const noHelp = schemaKeys.filter((k) => !helpKeys.includes(k));
console.log(`2. Campos en el formulario: ${schemaKeys.length} · textos de ayuda: ${helpKeys.length}`);
if (noHelp.length) bad("campos sin ayuda: " + noHelp.join(", "));
else console.log("   Todos los campos tienen ayuda: OK");

// --- 3. claves de idioma usadas vs definidas ---
const usedT = [...html.matchAll(/\bt\("([A-Za-z0-9_]+)"\)/g)].map((x) => x[1]);
const dataI18n = [...html.matchAll(/data-i18n="([A-Za-z0-9_]+)"/g)].map((x) => x[1]);
const allUsed = [...new Set([...usedT, ...dataI18n])];
const esBlock = html.slice(html.indexOf("es:{"), html.indexOf("en:{"));
const enBlock = html.slice(html.indexOf("en:{"), html.indexOf("let lang ="));
const esKeys = new Set([...esBlock.matchAll(/([A-Za-z0-9_]+):"/g)].map((x) => x[1]));
const enKeys = new Set([...enBlock.matchAll(/([A-Za-z0-9_]+):"/g)].map((x) => x[1]));
const missEs = [], missEn = [];
for (const k of allUsed) {
  if (!esKeys.has(k)) missEs.push(k);
  if (!enKeys.has(k)) missEn.push(k);
}
console.log(`3. Claves usadas: ${allUsed.length} · definidas es: ${esKeys.size} · en: ${enKeys.size}`);
if (missEs.length) bad("sin traducción en español: " + missEs.join(", "));
if (missEn.length) bad("sin traducción en inglés: " + missEn.join(", "));
if (!missEs.length && !missEn.length) console.log("   Traducciones completas: OK");

// --- 4. TODOS los elementos que el JavaScript busca tienen que existir ---
// POR QUE ESTA COMPROBACION: el 2026-09-13, al mover el bloque del registro de
// viaje a su propia pestana, un script de edicion borro el HTML de ese bloque
// (botones, mapa, resumen) y dejo el JavaScript intacto. La pagina cargaba sin
// errores de sintaxis y el fallo solo se habria visto ABRIENDOLA y pulsando:
// "$(...)" devuelve null y el boton no hace nada. Esta comprobacion lo caza en
// un segundo, sin navegador.
const idsEnHtml = new Set([...html.matchAll(/id="([A-Za-z0-9_]+)"/g)].map((x) => x[1]));
const idsQueBusca = [...new Set([...html.matchAll(/\$\("([A-Za-z0-9_]+)"\)/g)].map((x) => x[1]))];
// Excepcion: los elementos que el PROPIO JavaScript crea al montar la pagina.
const creadosPorJs = new Set(["gpsState"]);   // lo crea buildForm() con el boton del GPS
const idsQueFaltan = idsQueBusca.filter((k) => !idsEnHtml.has(k) && !creadosPorJs.has(k));
console.log(`4. Elementos que busca el JavaScript: ${idsQueBusca.length} Â· definidos en el HTML: ${idsEnHtml.size}`);
if (idsQueFaltan.length) bad("elementos que el JavaScript busca y NO existen en la pÃ¡gina: " + idsQueFaltan.join(", "));
else console.log("   Todos los elementos existen: OK");

console.log(fails ? `\n${fails} problema(s) encontrados.` : "\nTodo correcto.");
process.exit(fails ? 1 : 0);
