#!/usr/bin/env node
/*
 * kiss_frame_hex.js - Construye una trama AX.25 y su envoltorio KISS, y la saca
 * en hexadecimal (y opcionalmente a un fichero binario, para poder enviarla al
 * puerto serie sin depender de ninguna herramienta grafica).
 *
 * PARA QUE SIRVE: cuando algo no sale por el puerto, hay que separar "mi
 * programa no lo manda" de "el nodo no lo hace". Esto construye los bytes a
 * mano y los deja en un fichero, para mandarlos con cualquier cosa.
 *
 * Uso:
 *   node tools/kiss_frame_hex.js "EA2OY-7>APZFKT,WIDE1-1:>hola"
 *   node tools/kiss_frame_hex.js "EA2OY-7>APZFKT:>hola" --out frame.bin
 */

'use strict';

const fs = require('fs');

// --- AX.25 -------------------------------------------------------------------
// Direccion: 6 caracteres desplazados a la izquierda, relleno con espacios, y
// luego el byte de SSID: 0x60 | (ssid << 1) | (esLaUltima ? 1 : 0).
function direccion(call, ssid, ultima, repetida) {
  const out = Buffer.alloc(7);
  const c = (call || '').toUpperCase().padEnd(6, ' ').slice(0, 6);
  for (let i = 0; i < 6; i++) out[i] = c.charCodeAt(i) << 1;
  let b = 0x60 | ((ssid & 0x0f) << 1);
  if (repetida) b |= 0x80;
  if (ultima) b |= 0x01;
  out[6] = b;
  return out;
}

function partirDireccion(txt) {
  const m = /^([A-Za-z0-9]+)(?:-(\d+))?(\*)?$/.exec(txt.trim());
  if (!m) throw new Error('direccion rara: ' + txt);
  return { call: m[1], ssid: m[2] ? parseInt(m[2], 10) : 0, rep: !!m[3] };
}

function ax25(linea) {
  const i = linea.indexOf(':');
  if (i < 0) throw new Error('falta el ":" que separa cabecera y texto');
  const cabecera = linea.slice(0, i);
  const info = Buffer.from(linea.slice(i + 1), 'latin1');

  const gt = cabecera.indexOf('>');
  if (gt < 0) throw new Error('falta el ">" entre origen y destino');
  const srcTxt = cabecera.slice(0, gt);
  const resto = cabecera.slice(gt + 1).split(',');
  const dstTxt = resto.shift();
  const repetidores = resto.filter((s) => s.length > 0);

  const trozos = [];
  trozos.push(direccion(...Object.values(partirDireccion(dstTxt)).slice(0, 2), repetidores.length === 0, false));
  const s = partirDireccion(srcTxt);
  trozos.push(direccion(s.call, s.ssid, false, false));
  repetidores.forEach((r, idx) => {
    const p = partirDireccion(r);
    trozos.push(direccion(p.call, p.ssid, idx === repetidores.length - 1, p.rep));
  });
  trozos.push(Buffer.from([0x03, 0xf0]));   // control UI + PID sin protocolo
  trozos.push(info);
  return Buffer.concat(trozos);
}

// --- KISS --------------------------------------------------------------------
const FEND = 0xc0, FESC = 0xdb, TFEND = 0xdc, TFESC = 0xdd;

function kiss(ax) {
  const out = [FEND, 0x00];                 // puerto 0, comando 0 (datos)
  for (const b of ax) {
    if (b === FEND) out.push(FESC, TFEND);
    else if (b === FESC) out.push(FESC, TFESC);
    else out.push(b);
  }
  out.push(FEND);
  return Buffer.from(out);
}

// --- principal ---------------------------------------------------------------
const argv = process.argv.slice(2);
const linea = argv.find((a) => !a.startsWith('--'));
if (!linea) {
  console.log('Uso: node tools/kiss_frame_hex.js "EA2OY-7>APZFKT,WIDE1-1:>hola" [--out fichero.bin]');
  process.exit(1);
}

const ax = ax25(linea);
const k = kiss(ax);
const hex = (b) => b.toString('hex').match(/.{1,2}/g).join(' ');

console.log('linea    : ' + linea);
console.log('AX.25    : ' + ax.length + ' bytes');
console.log('  ' + hex(ax));
console.log('KISS     : ' + k.length + ' bytes');
console.log('  ' + hex(k));

const oi = argv.indexOf('--out');
if (oi >= 0 && argv[oi + 1]) {
  fs.writeFileSync(argv[oi + 1], k);
  console.log('escrito  : ' + argv[oi + 1]);
}
