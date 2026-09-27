#!/usr/bin/env node
// log2gpx.js — convierte el registro del nodo (tools\log_dump.ps1) en una ruta.
//
// Uso:  node tools\log2gpx.js paseo.txt [nombre]
// Salida: paseo.gpx (Wikiloc, Garmin, Strava, Google Earth...), paseo.kml
//         (Google Earth) y paseo.csv (hoja de calculo).
//
// Lineas reconocidas (las escribe src/flog.cpp). Las coordenadas de los ejemplos
// son INVENTADAS (punto perdido del Pacifico sur), no la posicion de nadie:
//   2026-09-11 14:23:45 TX TRK -29.99587,-140.023517 88.0km/h 210deg 1200m 111b ok D
//   14:23:45 TX TRK ...                       (sin fecha: GPS sin dia)
//   2026-09-11 14:23:50 RX EA2OY-7 Bcn rssi-11 snr-7.8
//   2026-09-11 14:23:52 DG EA2KW-9 -> EA2OY-10* rssi-95 snr-3.2
// License: GPL-3.0

const fs = require("fs");
const path = require("path");

// Speed may carry decimals (%.1f since 2026-09-11 (19)) and the altitude can be
// negative with a poor fix, so both accept sign and decimals. The trailing
// reason letter (F/R/C/D/M/P/L) is ignored on purpose.
//
// LA HORA SE ESCRIBE DE TRES FORMAS (comprobado en un registro real del nodo,
// 2026-09-13): fecha completa, hora suelta, o TIEMPO ENCENDIDO ("1s") cuando el
// GPS todavia no sabe la fecha. Y el GPS puede dar una fecha IMPOSIBLE
// ("2000-00-00") mientras no tiene almanaque. Los tres casos estan tratados:
// si no hay fecha buena, el punto va SIN hora (nunca inventada).
const TRK = /^(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}) TX TRK (-?\d+\.\d+),(-?\d+\.\d+) (-?\d+(?:\.\d+)?)km\/h (-?\d+(?:\.\d+)?)deg (-?\d+(?:\.\d+)?)m/;
const TRK_CUERPO = /^TX TRK (-?\d+\.\d+),(-?\d+\.\d+) (-?\d+(?:\.\d+)?)km\/h (-?\d+(?:\.\d+)?)deg (-?\d+(?:\.\d+)?)m/;
const PREFIJO = /^(?:(\d{4}-\d{2}-\d{2})|(\d{2}:\d{2}:\d{2})|\d+s)\s+/;
const EVENT = /^(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2}) (\S+)(.*)$/;
// Un mes 00 o un dia 00 no son una fecha.
function fechaValida(mes, dia) { return mes >= 1 && mes <= 12 && dia >= 1 && dia <= 31; }

function parse(lines) {
  const pts = [], events = [];
  for (const ln of lines) {
    let m = TRK.exec(ln);
    if (m) {
      const buena = fechaValida(+m[2], +m[3]);
      pts.push({ iso: buena ? `${m[1]}-${m[2]}-${m[3]}T${m[4]}:${m[5]}:${m[6]}Z` : null,
                 lat: +m[7], lon: +m[8], spd: +m[9], crs: +m[10], alt: +m[11],
                 tail: ln });
      continue;
    }
    // La hora no sirve (tiempo encendido o fecha imposible): se quita el prefijo
    // y se mira el RESTO de la linea, para no perder la posicion.
    const resto = ln.replace(PREFIJO, "");
    if (resto !== ln && (m = TRK_CUERPO.exec(resto))) {
      pts.push({ iso: null, lat: +m[1], lon: +m[2], spd: +m[3], crs: +m[4],
                 alt: +m[5], tail: ln });
      continue;
    }
    events.push(ln);
  }
  return { pts, events };
}

function esc(s) {
  return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function gpx(pts, name) {
  let out = '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<gpx version="1.1" creator="Faketec_APRS_Igate_EA2OY" xmlns="http://www.topografix.com/GPX/1/1">\n' +
    ` <trk>\n  <name>${esc(name)}</name>\n  <trkseg>\n`;
  for (const p of pts) {
    out += `   <trkpt lat="${p.lat}" lon="${p.lon}">`;
    if (Number.isFinite(p.alt)) out += `<ele>${p.alt}</ele>`;
    if (p.iso) out += `<time>${p.iso}</time>`;
    if (Number.isFinite(p.spd)) out += `<speed>${(p.spd / 3.6).toFixed(2)}</speed>`;
    if (Number.isFinite(p.crs)) out += `<course>${p.crs}</course>`;
    out += "</trkpt>\n";
  }
  return out + "  </trkseg>\n </trk>\n</gpx>\n";
}

function kml(pts, name) {
  const coords = pts.map(p => `${p.lon},${p.lat},${Number.isFinite(p.alt) ? p.alt : 0}`).join(" ");
  let marks = "";
  for (const p of pts) {
    marks += `<Placemark><name>${p.iso || ""}</name><Point><coordinates>` +
             `${p.lon},${p.lat},${Number.isFinite(p.alt) ? p.alt : 0}</coordinates></Point></Placemark>\n`;
  }
  return '<?xml version="1.0" encoding="UTF-8"?>\n' +
    `<kml xmlns="http://www.opengis.net/kml/2.2"><Document><name>${esc(name)}</name>\n` +
    '<Style id="line"><LineStyle><width>3</width><color>ff00a5ff</color></LineStyle></Style>\n' +
    `<Placemark><name>Ruta</name><styleUrl>#line</styleUrl><LineString><altitudeMode>absolute</altitudeMode>` +
    `<coordinates>${coords}</coordinates></LineString></Placemark>\n${marks}</Document></kml>\n`;
}

function csv(lines) {
  let out = "fecha,hora,tipo,lat,lon,vel_kmh,rumbo,alt_m,detalle\n";
  for (const ln of lines) {
    const m = TRK.exec(ln);
    if (m) {
      out += `${m[1]},${m[4]}:${m[5]}:${m[6]},TRK,${m[7]},${m[8]},${m[9]},${m[10]},${m[11]},tx ok\n`;
      continue;
    }
    const e = EVENT.exec(ln);
    if (e) out += `${e[1]},${e[2]},${e[3]},,,,,,"${e[4].trim().replace(/"/g, '""')}"\n`;
    else out += `,,,,,,,,"${ln.replace(/"/g, '""')}"\n`;
  }
  return out;
}

function distanceM(a, b) {
  const R = 6371000, rad = Math.PI / 180;
  const dp = (b.lat - a.lat) * rad, dl = (b.lon - a.lon) * rad;
  const x = Math.sin(dp / 2) ** 2 +
            Math.cos(a.lat * rad) * Math.cos(b.lat * rad) * Math.sin(dl / 2) ** 2;
  return 2 * R * Math.atan2(Math.sqrt(x), Math.sqrt(1 - x));
}

function main() {
  const file = process.argv[2];
  if (!file) {
    console.error("Uso: node tools/log2gpx.js <fichero.txt> [nombre]");
    process.exit(1);
  }
  const raw = fs.readFileSync(file, "utf8").split(/\r?\n/).filter(l => l.trim().length);
  const lines = raw.map(l => l.replace(/^LOG\s+/, ""));
  const { pts, events } = parse(lines);
  const base = file.replace(/\.[^.]+$/, "");
  const name = process.argv[3] || path.basename(base);

  fs.writeFileSync(base + ".gpx", gpx(pts, name));
  fs.writeFileSync(base + ".kml", kml(pts, name));
  fs.writeFileSync(base + ".csv", csv(lines));

  let dist = 0;
  for (let i = 1; i < pts.length; i++) dist += distanceM(pts[i - 1], pts[i]);
  // Si el punto no tiene hora buena, se dice "sin hora" y NO se enseña el trozo
  // de la linea: antes salia "2000-00-" en el resumen, que es una fecha que no
  // existe y solo confunde.
  const cuando = (p)=> p.iso ? p.iso : "(sin hora)";
  const t0 = pts.length ? cuando(pts[0]) : "-";
  const t1 = pts.length ? cuando(pts[pts.length - 1]) : "-";

  console.log(`Lineas leidas      : ${lines.length}`);
  console.log(`Puntos de ruta     : ${pts.length}   (${t0} -> ${t1})`);
  console.log(`Distancia total    : ${(dist / 1000).toFixed(2)} km`);
  console.log(`Eventos (RX/DG/EVT): ${events.length}`);
  const rx = events.filter(e => /\bRX\b/.test(e)).length;
  const dg = events.filter(e => /\bDG\b/.test(e)).length;
  console.log(`  repetidos (DG)   : ${dg}`);
  console.log(`  recibidas (RX)   : ${rx}`);
  console.log(`Generados          : ${base}.gpx , ${base}.kml , ${base}.csv`);
}

main();
