#!/usr/bin/env node
/*
 * igate_monitor.js - Vigila el iGate por su web y apunta lo que ve.
 *
 * PARA QUE SIRVE: hubo un hueco de unos 12 minutos en el que el nodo
 * transmitia y no aparecia en el mapa, y NO sabemos por que. La unica forma de
 * averiguarlo es tener datos del momento exacto en que vuelva a pasar. Esto
 * interroga al iGate cada pocos segundos POR SU WEB (nunca por el puerto
 * serie: abrir el puerto serie reinicia la placa y falsearia la prueba) y
 * apunta en un fichero si deja de contestar, si su lista de paquetes oidos se
 * queda parada, o si el propio iGate se reinicia.
 *
 * Uso:
 *   node tools/igate_monitor.js                        (vigila 8 horas)
 *   node tools/igate_monitor.js --horas 1 --cada 20
 *
 * Opciones:
 *   --host H     direccion del iGate (por defecto 192.168.3.236)
 *   --cada N     segundos entre consultas (por defecto 30)
 *   --horas N    horas de vigilancia (por defecto 8)
 *   --parado N   minutos sin oir nada para avisar (por defecto 20)
 *   --out FILE   fichero de registro (por defecto logs/igate_monitor.log)
 */

'use strict';

const fs = require('fs');
const path = require('path');
const http = require('http');

const argv = process.argv.slice(2);
function opt(name, def) {
  const i = argv.indexOf('--' + name);
  if (i < 0) return def;
  return argv[i + 1] === undefined ? true : argv[i + 1];
}

const host = String(opt('host', '192.168.3.236'));
const cada = Math.max(5, parseInt(opt('cada', '30'), 10) || 30);
const horas = parseFloat(opt('horas', '8')) || 8;
const paradoMin = parseInt(opt('parado', '20'), 10) || 20;
const stamp = new Date().toISOString().replace(/[-:T]/g, '').slice(0, 15);
const outFile = String(opt('out', path.join('logs', `igate_monitor_${stamp}.log`)));

try { fs.mkdirSync(path.dirname(outFile), { recursive: true }); } catch (e) {}
const out = fs.createWriteStream(outFile, { flags: 'a' });

function apunta(texto) {
  const linea = `[${new Date().toISOString().slice(11, 19)}] ${texto}`;
  console.log(linea);
  out.write(linea + '\n');
}

function pide(ruta, ms) {
  return new Promise((resolve, reject) => {
    const req = http.get({ host, port: 80, path: ruta, timeout: ms }, (res) => {
      let cuerpo = '';
      res.setEncoding('utf8');
      res.on('data', (d) => { cuerpo += d; });
      res.on('end', () => resolve({ status: res.statusCode, cuerpo }));
    });
    req.on('timeout', () => { req.destroy(new Error('sin respuesta')); });
    req.on('error', reject);
  });
}

let caidas = 0;
let ultimoVisto = null;      // "HH:MM:SS" del ultimo paquete que oyo
let ultimoCambio = Date.now();
let sinOir = 0;

async function vuelta() {
  // 1) contesta la web?
  let estado;
  try {
    estado = await pide('/status', 8000);
  } catch (e) {
    caidas++;
    apunta(`AVISO: el iGate NO contesta en la web (${e.message}). Caida numero ${caidas}.`);
    return;
  }
  if (!estado || estado.status !== 200) {
    caidas++;
    apunta(`AVISO: el iGate contesto HTTP ${estado && estado.status}. Caida numero ${caidas}.`);
    return;
  }
  if (caidas > 0) {
    apunta(`El iGate vuelve a contestar (tras ${caidas} fallo(s)).`);
    caidas = 0;
  }

  // 2) su lista de paquetes oidos: ha cambiado?
  try {
    const r = await pide('/received-packets.json', 8000);
    const lista = JSON.parse(r.cuerpo);
    if (Array.isArray(lista) && lista.length) {
      const ultimo = lista[lista.length - 1];
      const marca = `${ultimo.rxTime}`;
      if (marca !== ultimoVisto) {
        ultimoVisto = marca;
        ultimoCambio = Date.now();
        sinOir = 0;
        apunta(`oido: ${marca}  ${String(ultimo.packet).slice(0, 80)}  RSSI=${ultimo.RSSI}`);
      } else {
        sinOir = Math.round((Date.now() - ultimoCambio) / 60000);
        if (sinOir >= paradoMin) {
          apunta(`AVISO: el iGate lleva ${sinOir} min sin oir nada (ultimo: ${marca}).`);
          ultimoCambio = Date.now();   // no repetir el aviso cada vuelta
        }
      }
    }
  } catch (e) {
    apunta(`AVISO: no he podido leer su lista de paquetes (${e.message}).`);
  }
}

apunta(`Vigilando el iGate ${host} cada ${cada} s durante ${horas} h -> ${outFile}`);
apunta('Se apunta: caidas de su web, paquetes oidos y silencios largos. El puerto serie NO se toca.');

const fin = Date.now() + horas * 3600 * 1000;
(async () => {
  while (Date.now() < fin) {
    await vuelta();
    await new Promise((r) => setTimeout(r, cada * 1000));
  }
  apunta('Fin de la vigilancia.');
  out.end(() => process.exit(0));
})();
