#!/usr/bin/env node
/*
 * aprsis_watch.js - Escucha el flujo de APRS-IS (solo lectura) para comprobar
 * si las tramas que emite el nodo llegan a Internet.
 *
 * Sirve para verificar en el aire sin depender de aprs.fi ni de findu:
 * nos conectamos al servidor de APRS-IS como oyente (pass -1 = solo recibe,
 * no puede inyectar nada) con un filtro de radio alrededor del nodo y
 * mostramos todo lo que pasa por delante.
 *
 * LIMITACION IMPORTANTE (comprobada a golpes el 2026-09-12):
 *   Los servidores de APRS-IS NO te devuelven los paquetes de tu propio
 *   indicativo base. Si te conectas como EA2OY-7 (o EA2OY-14, da igual el
 *   SSID) y buscas paquetes de EA2OY, el servidor te los oculta y esta
 *   herramienta dice "no ha llegado nada" aunque las tramas SI esten en la
 *   red y se vean en aprs.fi. Estuvimos una hora persiguiendo ese fantasma.
 *   Por eso: esta herramienta NUNCA sirve para decidir si tu propia estacion
 *   esta saliendo al aire. Para eso, el navegador (aprs.fi o el mapa) manda.
 *   Solo es fiable para ver trafico de indicativos de OTRO base.
 *
 * REGLA DE ESTA HERRAMIENTA: solo se escucha, nunca se identifico uno como otra
 * estacion. No acepta passcode: siempre entra con "pass -1", que el servidor
 * marca como conexion sin validar y que por tanto NO PUEDE inyectar nada.
 *
 * Uso:
 *   node tools/aprsis_watch.js
 *   node tools/aprsis_watch.js --secs 300 --grep EA2OY
 *   node tools/aprsis_watch.js --filter "r/42.83/-1.64/80"
 *
 * Opciones:
 *   --secs N      segundos a escuchar (por defecto 180)
 *   --server H    servidor APRS-IS (por defecto euro.aprs2.net)
 *   --port N      puerto (por defecto 14580 = filtros definidos por el usuario)
 *   --call C      indicativo de login, solo para identificarse (EA2OY-7)
 *   --filter F    filtro APRS-IS (por defecto r/42.83/-1.64/60 -> 60 km)
 *   --grep S      mostrar solo lineas que contengan S (se puede repetir)
 *   --out FILE    guardar todo en un fichero (por defecto logs/aprsis_....log)
 *   --raw         mostrar tambien las lineas de control del servidor (#)
 *   --quiet       no imprimir las tramas, solo el resumen final
 *
 * Ejemplos tipicos:
 *   - Ver si nuestras balizas salen:  node tools/aprsis_watch.js --grep EA2OY
 *   - Ver todo el trafico del valle:  node tools/aprsis_watch.js --secs 600
 */

'use strict';

const net = require('net');
const fs = require('fs');
const path = require('path');

// ---------------------------------------------------------------- argumentos
const argv = process.argv.slice(2);
function opt(name, def) {
  const i = argv.indexOf('--' + name);
  if (i < 0) return def;
  const v = argv[i + 1];
  if (v === undefined || v.startsWith('--')) return true; // bandera sin valor
  return v;
}
function optAll(name) {
  const out = [];
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--' + name && argv[i + 1] && !argv[i + 1].startsWith('--')) out.push(argv[i + 1]);
  }
  return out;
}

const secs = parseInt(opt('secs', '180'), 10) || 180;
const server = String(opt('server', 'euro.aprs2.net'));
const call = String(opt('call', 'EA2OY-7')).toUpperCase();
// REGLA DE ESTA HERRAMIENTA: solo se escucha, nunca se identifico uno como otra
// estacion. Por eso NO acepta passcode: siempre entra con "pass -1", que el
// servidor marca como conexion sin validar y que por tanto NO PUEDE inyectar
// nada en la red. Es una cuestion de legalidad y de respeto a los
// radioaficionados: no se suplanta a nadie ni se usa el passcode de otro.
//
// Si hace falta una conexion validada para que el servidor aplique un filtro
// por indicativo, la via correcta NO es usar un passcode ajeno: es --anonimo,
// que se conecta al puerto de lectura publica (10152) SIN identificarse.
if (argv.includes('--pass')) {
  console.log('AVISO: la opcion --pass ya no existe. Esta herramienta solo escucha');
  console.log('       con "pass -1" (conexion sin validar, incapaz de transmitir).');
  console.log('       Si necesitas una conexion validada, usa --anonimo.');
}
const anonimo = argv.includes('--anonimo') || argv.includes('--anonymous');
// Modo anonimo: puerto de lectura publica, SIN identificarse y SIN filtro del
// servidor (se filtra aqui, en local). Es la forma limpia de ver el flujo
// entero sin usar el indicativo ni el passcode de nadie.
const port = parseInt(opt('port', anonimo ? '10152' : '14580'), 10) ||
             (anonimo ? 10152 : 14580);
const filter = String(opt('filter', 'r/42.83/-1.64/60'));
const greps = optAll('grep').map((s) => s.toUpperCase());
const showRaw = argv.includes('--raw');
const quiet = argv.includes('--quiet');

const stamp = new Date().toISOString().replace(/[-:T]/g, '').slice(0, 15);
const outFile = String(opt('out', path.join('logs', `aprsis_${stamp}.log`)));

try {
  fs.mkdirSync(path.dirname(outFile), { recursive: true });
} catch (e) {
  /* sin fichero de log, seguimos igual */
}
const out = fs.createWriteStream(outFile, { flags: 'a' });

// ------------------------------------------------------------------ utilidad
const t0 = Date.now();
function elapsed() {
  const s = (Date.now() - t0) / 1000;
  const m = Math.floor(s / 60);
  return `${String(m).padStart(2, '0')}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
}
function say(msg) {
  process.stdout.write(msg + '\n');
}

// Estadisticas
let nCtrl = 0;
let nData = 0;
let nMatch = 0;
const bySource = new Map();
let loggedIn = false;
let filterOk = null;

// ------------------------------------------------------------------ conexion
say(`APRS-IS  ${server}:${port}   login ${call} (solo lectura)`);
say(`Filtro   ${filter}`);
say(`Escucho  ${secs} s   ->  ${outFile}`);
say('');

const sock = net.createConnection({ host: server, port });
sock.setEncoding('utf8');
let buf = '';

// Aviso si estas buscando tu propio indicativo base: el servidor te lo ocultara
// y parece que "no llega nada" cuando en realidad si llega. Ver la limitacion
// explicada en la cabecera de este fichero.
const baseDe = (s) => String(s).split('-')[0].toUpperCase();
if (greps.some((g) => baseDe(g).includes(baseDe(call)) || baseDe(call).includes(baseDe(g)))) {
  say('  OJO: estas escuchando con un indicativo del mismo base que buscas.');
  say('  El servidor NO te devolvera esos paquetes aunque esten en la red.');
  say('  Para saber si TU estacion sale al aire, mira aprs.fi en el navegador.\n');
}

sock.on('connect', () => {
  // Modo anonimo: el puerto de lectura publica no pide identificarse, asi que
  // no se manda ninguna linea de login. Nada que suplantar.
  if (anonimo) return;
  // pass -1: el servidor marca la conexion como NO validada, de modo que esta
  // herramienta es incapaz de inyectar nada en la red.
  sock.write(`user ${call} pass -1 vers FaketecWatch 1.0 filter ${filter}\r\n`);
});

sock.on('data', (chunk) => {
  buf += chunk;
  let nl;
  while ((nl = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, nl).replace(/\r$/, '');
    buf = buf.slice(nl + 1);
    if (line.trim()) handleLine(line);
  }
});

sock.on('error', (err) => {
  say(`ERROR de conexion: ${err.code || ''} ${err.message}`);
  if (err.code === 'ENOTFOUND') say('  -> no se pudo resolver el nombre del servidor (DNS).');
  if (err.code === 'ECONNREFUSED') say('  -> el servidor rechazo la conexion (puerto cerrado o bloqueado).');
  if (err.code === 'ETIMEDOUT') say('  -> no hubo respuesta; puede ser el cortafuegos de la red.');
  finish(2);
});

sock.on('close', () => finish(0));

function handleLine(line) {
  out.write(line + '\n');

  if (line.startsWith('#')) {
    nCtrl++;
    const low = line.toLowerCase();
    if (low.includes('logresp')) {
      loggedIn = true;
      say(`[${elapsed()}] servidor: ${line.replace(/^#\s*/, '')}`);
      if (low.includes('unverified') || low.includes('pass -1')) {
        filterOk = false;
        say('  AVISO: entrada sin verificar (pass -1). Solo escuchamos, no se puede transmitir.');
      }
    } else if (low.includes('filter')) {
      if (low.includes('not') || low.includes('error') || low.includes('reject')) filterOk = false;
      else filterOk = true;
      say(`[${elapsed()}] filtro: ${line.replace(/^#\s*/, '')}`);
    } else if (showRaw) {
      say(`[${elapsed()}] # ${line.replace(/^#\s*/, '')}`);
    }
    return;
  }

  nData++;
  const src = (line.split('>')[0] || '?').toUpperCase();
  bySource.set(src, (bySource.get(src) || 0) + 1);

  const hit = greps.length === 0 || greps.some((g) => line.toUpperCase().includes(g));
  if (hit) {
    nMatch++;
    if (!quiet) say(`[${elapsed()}] ${line}`);
  }
}

function finish(code) {
  try {
    sock.destroy();
  } catch (e) {
    /* ya cerrado */
  }
  const total = secs;
  say('');
  say('-------------------- RESUMEN --------------------');
  say(`Escuchado        ${total} s en ${server}`);
  say(`Lineas de datos  ${nData}   (control: ${nCtrl})`);
  if (loggedIn) say('Login            aceptado por el servidor');
  else say('Login            SIN confirmacion del servidor');
  if (filterOk === false) say('Filtro           el servidor no lo aplico (posible falta de passcode)');
  if (greps.length) say(`Coincidencias    ${nMatch} de ${greps.join(', ')}`);
  if (bySource.size) {
    const top = [...bySource.entries()].sort((a, b) => b[1] - a[1]).slice(0, 12);
    say('Estaciones oidas:');
    for (const [s, n] of top) say(`   ${s.padEnd(12)} ${n}`);
  } else {
    say('Estaciones oidas: ninguna');
  }
  say(`Guardado en      ${outFile}`);
  say('------------------------------------------------');
  out.end(() => process.exit(code));
}

// Si no llega nada en 30 s, avisamos (suele ser red o filtro)
setTimeout(() => {
  if (nData === 0) say(`[${elapsed()}] aviso: todavia no ha llegado ninguna trama.`);
}, 30000).unref();

setTimeout(() => finish(0), secs * 1000);
