#!/usr/bin/env node
// diag_report.js — analiza un log JSONL capturado con tools/monitor.ps1 y
// resume el funcionamiento del nodo: GPS (adquisicion, satelites, deriva),
// balizas (cadencias), tracker, radio (RSSI/SNR/CRC), potencia y sensores.
// Uso: node tools/diag_report.js diag_20260910_120000.jsonl
// License: GPL-3.0

const fs = require('fs');

const file = process.argv[2];
if (!file) {
  console.error('Uso: node diag_report.js <log.jsonl>');
  process.exit(1);
}

const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/).filter(Boolean);
const snaps = [];
const rx = [];
const tx = [];
const notes = [];
const other = [];
const nonJson = [];

for (const line of lines) {
  if (line[0] !== '{') { nonJson.push(line); continue; }
  let o;
  try { o = JSON.parse(line); } catch { nonJson.push(line); continue; }
  if (o.diag === 'snap') snaps.push(o);
  else if (o.diag === 'rx') rx.push(o);
  else if (o.diag === 'tx') tx.push(o);
  else if (o.diag === 'note') notes.push(o);
  else other.push(o);
}

const n = (v) => (typeof v === 'number' ? v : 0);
const stats = (arr) => {
  if (!arr.length) return null;
  const s = arr.slice().sort((a, b) => a - b);
  const sum = s.reduce((a, b) => a + b, 0);
  return { min: s[0], med: s[Math.floor(s.length / 2)], max: s[s.length - 1], avg: sum / s.length };
};
const fmt = (s, unit = '', dec = 1) => s ? `${s.min.toFixed(dec)}/${s.avg.toFixed(dec)}/${s.max.toFixed(dec)}${unit} (min/med/max)` : 'sin datos';
const haversine = (a, b) => {
  const R = 6371000, r = Math.PI / 180;
  const dp = (b.lat - a.lat) * r, dl = (b.lon - a.lon) * r;
  const x = Math.sin(dp / 2) ** 2 + Math.cos(a.lat * r) * Math.cos(b.lat * r) * Math.sin(dl / 2) ** 2;
  return 2 * R * Math.atan2(Math.sqrt(x), Math.sqrt(1 - x));
};

const out = [];
out.push('===== INFORME DE DIAGNOSTICO =====');
out.push(`Fichero: ${file}`);
out.push(`Lineas: ${lines.length} | snapshots: ${snaps.length} | rx: ${rx.length} | tx: ${tx.length} | no-JSON (NMEA): ${nonJson.length}`);

if (snaps.length) {
  const t0 = n(snaps[0].ms), t1 = n(snaps[snaps.length - 1].ms);
  out.push(`Duracion: ${((t1 - t0) / 1000).toFixed(1)} s | modo=${snaps[0].mode} | call=${snaps[0].call || '?'}`);

  // GPS
  const fixSnaps = snaps.filter(s => s.gps && s.gps.fix);
  const firstFix = fixSnaps.find(s => n(s.ms) > 0);
  const powered = snaps.filter(s => s.gps && s.gps.on).length;
  out.push('');
  out.push(`GPS: encendido ${((powered / snaps.length) * 100).toFixed(0)}% | fix ${((fixSnaps.length / snaps.length) * 100).toFixed(0)}%`);
  if (firstFix) out.push(`  primera fijacion: ${(n(firstFix.ms) / 1000).toFixed(1)} s desde arranque`);
  out.push(`  satelites: ${fmt(stats(fixSnaps.map(s => n(s.gps.sats))), '', 0)}`);
  out.push(`  hdop: ${fmt(stats(fixSnaps.map(s => n(s.gps.hdop))), '', 2)}`);
  let dist = 0, prev = null, maxSpd = 0;
  for (const s of fixSnaps) {
    if (prev) { const d = haversine(prev, s.gps); if (d > 5 && d < 5000) dist += d; }
    prev = s.gps;
    maxSpd = Math.max(maxSpd, n(s.gps.spd));
  }
  out.push(`  deriva acumulada: ${(dist / 1000).toFixed(3)} km | vel max: ${maxSpd.toFixed(1)} km/h`);

  // Tracker
  let moves = 0, lastMv = null;
  for (const s of snaps) { const mv = s.trk && s.trk.mv; if (lastMv !== null && mv !== lastMv) moves++; lastMv = mv; }
  out.push(`Tracker: cambios movimiento=${moves} | snapshots con primer fix pendiente=${snaps.filter(s => s.trk && s.trk.first).length}`);

  // Power
  const mv = stats(snaps.map(s => n(s.pwr && s.pwr.mv)).filter(v => v > 0));
  out.push(`Potencia: ${fmt(mv, ' mV', 0)} | USB ${((snaps.filter(s => s.pwr && s.pwr.usb).length / snaps.length) * 100).toFixed(0)}% | lecturas bajas>0: ${snaps.filter(s => s.pwr && n(s.pwr.low) > 0).length}`);

  // Radio (ultimo snapshot = contadores)
  const last = snaps[snaps.length - 1];
  if (last.rdo) out.push(`Radio: rx=${n(last.rdo.rx)} tx=${n(last.rdo.tx)} digi=${n(last.rdo.dg)} crc=${n(last.rdo.crc)} | rssi ultimo=${n(last.rdo.rssi).toFixed(0)} dBm snr=${n(last.rdo.snr).toFixed(1)} fErr=${n(last.rdo.fErr).toFixed(0)} Hz`);

  // Sensores
  const wx = snaps.filter(s => s.sens && s.sens.wx);
  if (wx.length) {
    out.push(`Sensores: T=${fmt(stats(wx.map(s => n(s.sens.temp))), ' C')} H=${fmt(stats(wx.map(s => n(s.sens.hum))), ' %', 0)} P=${fmt(stats(wx.map(s => n(s.sens.hpa))), ' hPa', 1)}`);
    const ima = stats(wx.filter(s => s.sens.ima !== 0).map(s => n(s.sens.ima)));
    if (ima) out.push(`  INA: ${fmt(ima, ' mA', 0)}`);
  }
}

// Balizas: agrupa TX por tipo y calcula cadencia de las de posicion
const kind = (f) => {
  const i = f.indexOf(':'); if (i < 0) return '?';
  const body = f.slice(i + 1);
  if (body.startsWith('!') || body.startsWith('@') || body.startsWith('=')) return 'pos';
  if (body.startsWith('T#')) return 'telem';
  if (body.startsWith('>')) return 'status';
  if (body.startsWith(':')) return 'msg';
  return 'other';
};
const okTx = tx.filter(t => n(t.code) === 0);
const byKind = {};
for (const t of okTx) { const k = kind(t.frame || ''); byKind[k] = (byKind[k] || 0) + 1; }
out.push('');
out.push(`TX correctas: ${okTx.length} (${Object.entries(byKind).map(([k, v]) => k + '=' + v).join(' ') || 'ninguna'})`);
const failed = tx.filter(t => n(t.code) !== 0);
if (failed.length) {
  const codes = {};
  for (const t of failed) codes[t.code] = (codes[t.code] || 0) + 1;
  out.push(`TX fallidas: ${failed.length} (${Object.entries(codes).map(([c, v]) => 'code=' + c + ':' + v).join(', ')})  [-102 mute, -101 canal ocupado]`);
}
const posTx = okTx.filter(t => kind(t.frame || '') === 'pos');
if (posTx.length >= 2) {
  const gaps = [];
  for (let i = 1; i < posTx.length; i++) {
    const a = n(posTx[i - 1].ms), b = n(posTx[i].ms);
    if (b > a) gaps.push((b - a) / 1000);
  }
  if (gaps.length) {
    const g = stats(gaps);
    out.push(`Cadencia balizas posicion: ${g.min.toFixed(0)}/${g.med.toFixed(0)}/${g.max.toFixed(0)} s (min/med/max, n=${gaps.length})`);
  }
}
if (posTx.length) {
  out.push('Balizas de posicion (frames):');
  for (const t of posTx.slice(-8)) out.push('  ' + (t.frame || '').slice(0, 110));
}

// Radiobalizas oidas
if (rx.length) {
  const senders = {};
  for (const r of rx) senders[r.from] = (senders[r.from] || 0) + 1;
  const rssi = stats(rx.map(r => n(r.rssi)));
  out.push('');
  out.push(`RX validadas: ${rx.length} de ${Object.keys(senders).length} emisores | rssi ${fmt(rssi, ' dBm', 0)}`);
  out.push('  Top emisores: ' + Object.entries(senders).sort((a, b) => b[1] - a[1]).slice(0, 8).map(([k, v]) => `${k}(${v})`).join(' '));
}

if (notes.length) out.push(`Notas diag: ${notes.length}`);
const sleepEv = other.filter(o => o.power === 'sleep').length;
const bootEv = other.filter(o => o.power === 'boot').length;
if (sleepEv || bootEv) out.push(`Eventos de energia: sleep=${sleepEv} boot=${bootEv}`);
if (other.length) {
  const keys = {};
  for (const o of other) { const k = Object.keys(o).join(','); keys[k] = (keys[k] || 0) + 1; }
  out.push(`Eventos JSON: ${Object.entries(keys).map(([k, v]) => k + '=' + v).join(' ')}`);
}
if (nonJson.length) out.push(`AVISO: ${nonJson.length} lineas no-JSON (NMEA u otros)`);

console.log(out.join('\n'));
