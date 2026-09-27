#!/usr/bin/env node
/*
 * aprsis_fake_server.js - Servidor APRS-IS de mentira, para ver EXACTAMENTE
 * que manda un iGate cuando cree que esta subiendo un paquete.
 *
 * Para que sirve: cuando en el aire algo no aparece en aprs.fi, hay que saber
 * si el problema es del iGate (manda algo mal formado) o del servidor (lo
 * recibe bien y lo tira). Con esto se lee la trama tal cual sale del iGate.
 *
 * Como se usa (el iGate debe apuntar a esta IP y a este puerto):
 *   node tools/aprsis_fake_server.js --secs 180
 *
 * Opciones:
 *   --port N     puerto de escucha (por defecto 14580, el de APRS-IS)
 *   --secs N     segundos que permanece escuchando (por defecto 120)
 *   --call C     indicativo que se hace creer al iGate (por defecto L0RA-10)
 *   --out FILE   guardar todo en un fichero
 *
 * Detalle importante: ademas de escuchar, contesta con un logresp "verified",
 * porque el firmware de CA2RXU no sube NADA hasta que el servidor le confirma
 * que su passcode es bueno (bandera passcodeValid). Sin esa respuesta, el
 * iGate se quedaria callado y no veriamos nada.
 */

'use strict';

const net = require('net');
const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);
function opt(name, def) {
  const i = argv.indexOf('--' + name);
  if (i < 0) return def;
  const v = argv[i + 1];
  if (v === undefined || v.startsWith('--')) return true;
  return v;
}

const port = parseInt(opt('port', '14580'), 10) || 14580;
const secs = parseInt(opt('secs', '120'), 10) || 120;
const fakeCall = String(opt('call', 'L0RA-10')).toUpperCase();
const stamp = new Date().toISOString().replace(/[-:T]/g, '').slice(0, 15);
const outFile = String(opt('out', path.join('logs', `fakeserver_${stamp}.log`)));

try {
  fs.mkdirSync(path.dirname(outFile), { recursive: true });
} catch (e) {
  /* sin fichero de log, seguimos igual */
}
const out = fs.createWriteStream(outFile, { flags: 'a' });

const t0 = Date.now();
function elapsed() {
  const s = (Date.now() - t0) / 1000;
  return `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
}

let nLines = 0;
let nPackets = 0;

const server = net.createServer((sock) => {
  const peer = `${sock.remoteAddress}:${sock.remotePort}`;
  console.log(`[${elapsed()}] iGate conectado desde ${peer}`);
  out.write(`=== conexion desde ${peer} ===\n`);

  // Saludo + respuesta de login: sin esto el iGate no sube nada.
  sock.write('# aprsc 2.1.21-fake (servidor de pruebas local)\r\n');
  sock.write(`# logresp ${fakeCall} verified, server FAKE\r\n`);

  let buf = '';
  sock.setEncoding('utf8');
  sock.on('data', (chunk) => {
    buf += chunk;
    let nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, nl).replace(/\r$/, '');
      buf = buf.slice(nl + 1);
      if (!line) continue;
      nLines++;
      const isCtrl = line.startsWith('#');
      if (!isCtrl) nPackets++;
      const tag = isCtrl ? 'control' : 'PAQUETE';
      console.log(`[${elapsed()}] ${tag}: ${line}`);
      out.write(`${tag}: ${line}\n`);
    }
  });
  sock.on('error', () => {});
  sock.on('close', () => {
    console.log(`[${elapsed()}] conexion cerrada (${peer})`);
    out.write(`=== cerrada ${peer} ===\n`);
  });
});

server.listen(port, () => {
  console.log(`Servidor de pruebas escuchando en el puerto ${port} durante ${secs} s`);
  console.log(`Guardando en ${outFile}`);
  console.log('(el iGate debe estar apuntando a la IP de este ordenador)\n');
});

setTimeout(() => {
  console.log('');
  console.log('---------------- RESUMEN ----------------');
  console.log(`Lineas recibidas : ${nLines}`);
  console.log(`Paquetes (no #)  : ${nPackets}`);
  console.log(`Guardado en      : ${outFile}`);
  console.log('-----------------------------------------');
  out.end(() => {
    server.close(() => process.exit(0));
  });
}, secs * 1000);
