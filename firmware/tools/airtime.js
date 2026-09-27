#!/usr/bin/env node
// airtime.js — Informe de USO DEL AIRE a partir del volcado del registro del nodo.
//
// Uso:
//   node tools/airtime.js .pio/log_completo.txt
//
// Lee las lineas del "log dump" (con marca de tiempo del GPS o de uptime) y saca:
//   - Cuantas tramas emite el nodo, de que tipo y por hora.
//   - Cuanto tiempo de aire ocupa cada una (formula LoRa, SF/BW/CR del nodo).
//   - El ciclo de trabajo (duty cycle) por hora y por dia.
//   - Cuantas tramas y de que estaciones se OYEN (trafico de la zona).
// License: GPL-3.0

const fs = require("fs");

const SF = 12, BW = 125000, CR = 1;      // CR 4/5 (CR = 4/5 -> valor 1)
const PREAMBLE = 8, CRC = 1, IH = 0;    // cabecera explicita, CRC on
const LDRO = 1;                         // SF12 a 125 kHz: optimizacion de baja velocidad
const PREFIX = 3;                       // 0x3C 0xFF 0x01 del ecosistema LoRa APRS

function airTimeMs(bytes) {
  const pl = bytes + PREFIX;
  const tsym = Math.pow(2, SF) / BW;                       // segundos
  const tPre = (PREAMBLE + 4.25) * tsym;
  const num = 8 * pl - 4 * SF + 28 + 16 * CRC - 20 * IH;
  const den = 4 * (SF - 2 * LDRO);
  const nPay = 8 + Math.max(Math.ceil(num / den) * (CR + 4), 0);
  return (tPre + nPay * tsym) * 1000;
}

// Tamano real de cada trama nuestra (el registro lo dice para BCN/TRK; el resto
// son estimaciones conservadoras a partir de lo que construye el firmware).
const SIZES = {
  BCN: (m) => parseInt(m, 10) || 85,
  TRK: (m) => parseInt(m, 10) || 101,
  WX: () => 40,
  TLM: () => 90,
  MSG: () => 40,
  BLN: () => 50,
  OBJ: () => 55,
  STATUS: () => 80,
};

function main() {
  const file = process.argv[2] || ".pio/log_completo.txt";
  const lines = fs.readFileSync(file, "utf8").split(/\r?\n/).filter(Boolean);

  const perHour = new Map();     // clave "YYYY-MM-DD HH" -> datos
  const rxStations = new Map();  // indicativo -> nº de tramas oidas
  const txByType = new Map();    // tipo -> nº de tramas emitidas
  let txTotal = 0, rxTotal = 0, txMs = 0;
  let first = null, last = null;
  const uptimeOnly = [];

  const RXRE = /RX\s+(\S+)\s+(Bcn|Msg|Pkt)\s+rssi/i;
  const TXRE = /TX\s+(BCN|TRK|WX|TLM|MSG|BLN|OBJ)\b(.*)$/;

  for (const ln of lines) {
    // El volcado del nodo (log_dump.ps1) puede llegar de dos maneras: con el
    // prefijo "LOG " de una captura en bruto, o ya limpio (solo la entrada,
    // empezando por la hora). Antes solo se aceptaba la primera, y con el
    // volcado normal el informe salia a cero. Se aceptan las dos.
    let body;
    if (ln.startsWith("LOG ")) {
      body = ln.slice(4).trim();          // ojo: el volcado lleva un espacio tras LOG
    } else if (/^\d{4}-\d{2}-\d{2} \d{2}:/.test(ln) || /^\d+s /.test(ln) ||
               /^\d{2}:\d{2}:\d{2} /.test(ln)) {
      body = ln.trim();
    } else {
      continue;
    }
    // marca de tiempo: "YYYY-MM-DD HH:MM:SS" o "NNNs" (sin GPS todavia)
    let key = null;
    const dt = body.match(/^(\d{4}-\d{2}-\d{2}) (\d{2}):/);
    if (dt) {
      key = `${dt[1]} ${dt[2]}`;
      if (!first) first = body.slice(0, 19);
      last = body.slice(0, 19);
    } else if (/^\d+s /.test(body)) {
      uptimeOnly.push(body);
      continue;
    } else if (/^\d{2}:\d{2}:\d{2} /.test(body)) {
      key = "sin-fecha " + body.slice(0, 2);   // hay hora pero no dia
    } else {
      continue;
    }
    if (!perHour.has(key)) perHour.set(key, { tx: 0, txMs: 0, rx: 0 });
    const h = perHour.get(key);

    const tx = body.match(TXRE);
    if (tx) {
      const type = tx[1];
      const sizeM = (type === "BCN" || type === "TRK")
        ? (tx[2].match(/(\d+)b\b/) || [null, null])[1]
        : null;
      const bytes = SIZES[type](sizeM);
      const ms = airTimeMs(bytes);
      h.tx++; h.txMs += ms;
      txTotal++; txMs += ms;
      txByType.set(type, (txByType.get(type) || 0) + 1);
      continue;
    }
    const rx = body.match(RXRE);
    if (rx && !/ACUSE|ACK|MSG|CMD|REJ/.test(body)) {
      h.rx++; rxTotal++;
      const st = rx[1];
      rxStations.set(st, (rxStations.get(st) || 0) + 1);
    }
  }

  console.log("=== TIEMPO DE AIRE POR TRAMA (SF12 / BW125 / CR4-5, +3 B de prefijo) ===");
  for (const b of [40, 85, 101, 130, 178, 255]) {
    console.log(`  ${String(b).padStart(3)} bytes -> ${(airTimeMs(b) / 1000).toFixed(2)} s`);
  }

  console.log("\n=== LO QUE EMITE NUESTRO NODO ===");
  for (const [t, n] of [...txByType.entries()].sort((a, b) => b[1] - a[1])) {
    console.log(`  ${t.padEnd(5)} ${String(n).padStart(4)} tramas`);
  }
  console.log(`  TOTAL ${txTotal} tramas, ${(txMs / 1000).toFixed(0)} s de aire`);

  console.log("\n=== POR HORA (horas con actividad) ===");
  console.log("  hora              TX   aire(s)  %canal   RX");
  for (const [k, v] of [...perHour.entries()].sort()) {
    if (v.tx === 0 && v.rx === 0) continue;
    const pct = (v.txMs / 3600000) * 100;
    console.log(`  ${k}  ${String(v.tx).padStart(4)}  ${(v.txMs / 1000).toFixed(1).padStart(7)}  ${pct.toFixed(2).padStart(6)}  ${String(v.rx).padStart(4)}`);
  }

  console.log("\n=== TRAFICO OIDO (estaciones de la zona) ===");
  for (const [st, n] of [...rxStations.entries()].sort((a, b) => b[1] - a[1])) {
    console.log(`  ${st.padEnd(12)} ${String(n).padStart(4)} tramas`);
  }
  console.log(`  TOTAL ${rxTotal} tramas oidas`);
  if (first) console.log(`\n  Ventana del registro: ${first} -> ${last}`);
  console.log(`  Lineas sin fecha (arranques): ${uptimeOnly.length}`);
}

main();
