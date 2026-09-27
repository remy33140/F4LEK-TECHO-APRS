#!/usr/bin/env node
/*
 * ble_kiss_server.js — Servidor local de pruebas para el KISS por Bluetooth (BLE).
 *
 * PARA QUE SIRVE
 *   El nodo va a llevar un TNC KISS por Bluetooth LE (servicio NUS). Desde este
 *   PC no hay forma de hablar BLE a mano: PowerShell no puede suscribirse a los
 *   eventos de WinRT, no hay SDK de .NET para compilar un ayudante y Node no trae
 *   Bluetooth. Lo unico que funciona es un navegador: Web Bluetooth solo esta
 *   disponible en contexto seguro, y "http://localhost" cuenta como seguro. Asi
 *   que este servidor sirve la pagina y ademas recoge lo que la pagina ve.
 *
 * COMO SE USA
 *   node tools/ble_kiss_server.js
 *   (opciones: --port N, --host IP, --open, --quiet, --help)
 *
 *   Luego, en Chrome o Edge, abrir la URL que imprime al arrancar y pulsar
 *   "Conectar" (Web Bluetooth obliga a que lo pulse una persona: no se puede
 *   automatizar). Todo lo que llegue por Bluetooth queda escrito en:
 *     logs/ble_kiss.log    -> lineas de registro de la pagina
 *     logs/ble_frames.log  -> tramas AX.25 decodificadas (y las enviadas)
 *
 * POR QUE NO SIRVE ABRIR EL HTML CON DOBLE CLIC
 *   Con file:// el navegador desactiva Web Bluetooth. La pagina tiene que venir
 *   de este servidor (http://localhost) para poder conectar.
 *
 * License: GPL-3.0
 */

"use strict";

const http = require("http");
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");                      // raiz del proyecto
const PAGE = path.join(__dirname, "ble_kiss.html");           // la pagina que se sirve
const LOG_DIR = path.join(ROOT, "logs");
const LOG_FILE = path.join(LOG_DIR, "ble_kiss.log");
const FRAMES_FILE = path.join(LOG_DIR, "ble_frames.log");

const MAX_BODY = 256 * 1024;   // 256 KB: ninguna peticion legitima pasa de aqui
const FALLBACK_PORTS = [8099, 8100, 8101, 8102, 8103, 8110, 8120, 8200];
const CHUNK = 8192;            // para no partir una linea al escribir en consola

/* ============================== opciones ================================== */

const argv = process.argv.slice(2);
function opt(name, def) {
  const i = argv.indexOf("--" + name);
  if (i < 0) return def;
  const v = argv[i + 1];
  if (v === undefined || v.startsWith("--")) return true;
  return v;
}

const requestedPort = parseInt(opt("port", "8099"), 10) || 8099;
const host = String(opt("host", "127.0.0.1"));
const quiet = argv.includes("--quiet") || argv.includes("-q");
const wantOpen = argv.includes("--open");

if (argv.includes("--help") || argv.includes("-h")) {
  console.log(`ble_kiss_server.js — servidor local para probar el KISS por Bluetooth

  node tools/ble_kiss_server.js [opciones]

  --port N    puerto preferido (por defecto 8099; si esta ocupado prueba otros)
  --host IP   interfaz donde escuchar (por defecto 127.0.0.1, solo este PC)
  --open      intenta abrir la pagina en el navegador al arrancar
  --quiet     no imprime cada peticion, solo los avisos
  --help      esta ayuda

  Deja el servidor abierto y abre la URL en Chrome o Edge. Hay que pulsar
  "Conectar" a mano: el navegador exige un clic de una persona.`);
  process.exit(0);
}

/* ============================== ficheros de log =========================== */

try {
  fs.mkdirSync(LOG_DIR, { recursive: true });
} catch (e) {
  console.error("No puedo crear " + LOG_DIR + ": " + e.message);
}

const kissOut = fs.createWriteStream(LOG_FILE, { flags: "a" });
const framesOut = fs.createWriteStream(FRAMES_FILE, { flags: "a" });
// Un error de escritura (disco lleno, fichero bloqueado) no debe tumbar el
// servidor: se avisa una vez y se sigue sirviendo la pagina.
let writeWarned = false;
function streamError(which) {
  return (e) => {
    if (writeWarned) return;
    writeWarned = true;
    console.error("AVISO: no puedo escribir en el log de " + which + ": " + e.message);
  };
}
kissOut.on("error", streamError("lineas"));
framesOut.on("error", streamError("tramas"));

function stamp() {
  const d = new Date();
  const p = (n, w) => String(n).padStart(w || 2, "0");
  return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

// Corta por longitud para que una linea enorme (o un salto de linea metido en
// el JSON) no rompa el formato del fichero: una linea de log, una linea.
function oneLine(s, max) {
  let t = String(s === undefined || s === null ? "" : s)
    .replace(/[\r\n\t]+/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  const lim = max || 4000;
  if (t.length > lim) t = t.slice(0, lim) + " [...]";
  return t;
}

// Campos que ya se pintan aparte, para no repetirlos en el "extra".
const KNOWN = ["ts", "time", "hora", "level", "nivel", "text", "msg", "message",
               "line", "linea", "dir", "direction", "raw", "hex", "rssi", "kind",
               "client", "tab", "count", "n", "len", "src", "dst", "path", "info"];

function extraFields(obj) {
  const out = {};
  for (const k of Object.keys(obj)) {
    if (!KNOWN.includes(k)) out[k] = obj[k];
  }
  return out;
}

function extraText(obj) {
  const extra = extraFields(obj);
  const keys = Object.keys(extra);
  if (!keys.length) return "";
  try {
    return " " + JSON.stringify(extra);
  } catch (e) {
    return " (campos extra no imprimibles)";
  }
}

/* ---------------------------- lineas de registro -------------------------- */

function appendLog(obj, rawFallback) {
  const isObj = obj && typeof obj === "object" && !Array.isArray(obj);
  const level = isObj ? oneLine(obj.level || obj.nivel || "", 16) : "";
  const text = isObj
    ? oneLine(obj.text || obj.msg || obj.message || obj.line || obj.linea || JSON.stringify(obj))
    : oneLine(rawFallback);
  const hora = isObj ? oneLine(obj.ts || obj.time || obj.hora || "", 24) : "";
  const extra = isObj ? extraText(obj) : "";
  const line = `[${stamp()}]${hora ? " (" + hora + ")" : ""}` +
               `${level ? " [" + level + "]" : ""} ${text}${extra}`;
  kissOut.write(line + "\n");
  return line;
}

/* -------------------------------- tramas ---------------------------------- */

// Los bytes pueden llegar como cadena hex ("C0 00 82 ...") o como lista de
// numeros; se normalizan a hex limpio en mayusculas.
function toHex(v) {
  if (Array.isArray(v)) {
    return v.map((b) => {
      const n = Number(b);
      return Number.isFinite(n) ? (n & 0xff).toString(16).toUpperCase().padStart(2, "0") : "??";
    }).join(" ");
  }
  if (typeof v === "string") {
    if (/^[0-9a-fA-F\s]+$/.test(v) && v.trim()) {
      return v.trim().split(/\s+/).map((t) => {
        if (!/^[0-9a-fA-F]+$/.test(t)) return t;
        return t.length <= 2 ? t.toUpperCase().padStart(2, "0") : t.toUpperCase();
      }).join(" ");
    }
    return oneLine(v, 1200);
  }
  return "";
}

function appendFrame(obj, dirFromQuery, rawFallback) {
  const isObj = obj && typeof obj === "object" && !Array.isArray(obj);
  const dir = oneLine(
    (isObj && (obj.dir || obj.direction)) || dirFromQuery || "?",
    8
  ).toUpperCase();
  const rssi = isObj ? (obj.rssi === undefined || obj.rssi === null ? "" : oneLine(obj.rssi, 16)) : "";
  const len = isObj && Number.isFinite(Number(obj.len)) ? Number(obj.len) : null;
  const summary = isObj
    ? oneLine(obj.summary || obj.text || obj.msg || obj.message || "")
    : oneLine(rawFallback);
  const hex = isObj ? toHex(obj.raw || obj.hex || obj.bytes || "") : "";

  const head = `[${stamp()}] ${dir === "TX" ? "TX ->" : dir === "RX" ? "RX <-" : dir}` +
               `${rssi !== "" ? " rssi=" + rssi + "dBm" : ""}` +
               `${len !== null ? " " + len + "B" : ""}`;
  const line = `${head} ${summary || "(sin resumen)"}${hex ? "  hex=" + hex : ""}`;
  framesOut.write(line + "\n");
  return line;
}

/* ================================ HTTP ==================================== */

function send(res, code, type, body, extraHeaders) {
  const h = Object.assign({
    "Content-Type": type,
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
  }, extraHeaders || {});
  try {
    res.writeHead(code, h);
    res.end(body);
  } catch (e) {
    /* el cliente se fue: da igual */
  }
}

function sendJson(res, code, obj) {
  send(res, code, "application/json; charset=utf-8", JSON.stringify(obj) + "\n");
}

function readBody(req, cb) {
  let size = 0;
  const parts = [];
  let done = false;
  const finish = (err, text) => {
    if (done) return;
    done = true;
    cb(err, text);
  };
  req.on("data", (c) => {
    size += c.length;
    if (size > MAX_BODY) {
      finish(new Error("cuerpo demasiado grande (" + size + " bytes)"));
      return;
    }
    parts.push(c);
  });
  req.on("end", () => finish(null, Buffer.concat(parts).toString("utf8")));
  req.on("error", (e) => finish(e));
  req.on("aborted", () => finish(new Error("peticion cortada")));
}

function servePage(res) {
  fs.readFile(PAGE, (err, buf) => {
    if (err) {
      console.error("No puedo leer " + PAGE + ": " + err.message);
      send(res, 500, "text/plain; charset=utf-8",
        "No encuentro tools/ble_kiss.html junto a este servidor.\n");
      return;
    }
    send(res, 200, "text/html; charset=utf-8", buf);
  });
}

function parseJson(text) {
  const t = String(text || "").trim();
  if (!t) return { ok: true, value: {} };
  try {
    return { ok: true, value: JSON.parse(t) };
  } catch (e) {
    return { ok: false, error: e.message };
  }
}

const server = http.createServer((req, res) => {
  let pathname = "/";
  let query = "";
  try {
    const u = new URL(req.url || "/", "http://localhost");
    pathname = u.pathname;
    query = u.search;
  } catch (e) {
    pathname = String(req.url || "/").split("?")[0];
  }

  // Pagina principal (y cualquier variante razonable de la raiz).
  if (req.method === "GET" && (pathname === "/" || pathname === "/index.html" || pathname === "/ble_kiss.html")) {
    servePage(res);
    return;
  }

  // Estado rapido, util para comprobar desde la linea de comandos que sigue vivo.
  if (req.method === "GET" && pathname === "/health") {
    sendJson(res, 200, { ok: true, page: PAGE, logs: { lineas: LOG_FILE, tramas: FRAMES_FILE } });
    return;
  }

  // El navegador pide el icono solo: se contesta vacio para no ensuciar la consola.
  if (req.method === "GET" && pathname === "/favicon.ico") {
    send(res, 204, "image/x-icon", "");
    return;
  }

  if (req.method === "POST" && (pathname === "/log" || pathname === "/frames")) {
    const isFrames = pathname === "/frames";
    let dirFromQuery = "";
    try {
      dirFromQuery = new URLSearchParams(query).get("dir") || "";
    } catch (e) {
      dirFromQuery = "";
    }
    readBody(req, (err, text) => {
      if (err) {
        console.log(`[${stamp()}] AVISO: ${pathname} descartado: ${err.message}`);
        sendJson(res, 413, { ok: false, error: err.message });
        return;
      }
      const parsed = parseJson(text);
      if (!parsed.ok) {
        // Entrada mal formada: se apunta tal cual y se sigue. Nunca se cae.
        const line = isFrames
          ? appendFrame(null, dirFromQuery, "(JSON roto) " + text)
          : appendLog(null, "(JSON roto) " + text);
        console.log(`[${stamp()}] AVISO: ${pathname} con JSON invalido (${parsed.error}); guardado igualmente`);
        sendJson(res, 200, { ok: true, warning: "json invalido: " + parsed.error, line });
        return;
      }
      const rawText = typeof parsed.value === "string" ? parsed.value : JSON.stringify(parsed.value);
      const line = isFrames
        ? appendFrame(parsed.value, dirFromQuery, rawText)
        : appendLog(parsed.value, rawText);
      if (!quiet) console.log(line.length > CHUNK ? line.slice(0, CHUNK) + " [...]" : line);
      sendJson(res, 200, { ok: true, line });
    });
    return;
  }

  if (req.method === "OPTIONS") {
    // La pagina viene del mismo origen, pero por si se abre de otra forma.
    send(res, 204, "text/plain", "", {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type",
    });
    return;
  }

  sendJson(res, 404, { ok: false, error: "ruta desconocida: " + pathname });
});

// Cualquier fallo raro de una peticion se queda en un aviso: el servidor sigue.
server.on("clientError", (err, socket) => {
  try {
    socket.end("HTTP/1.1 400 Bad Request\r\n\r\n");
  } catch (e) {
    /* nada que hacer */
  }
});
server.on("connection", (socket) => {
  // Un cliente que corta a lo bruto (cerrar la pestana) no puede tumbar nada.
  socket.on("error", () => {});
});
// OJO: aqui NO se pone un manejador general de "error". Los errores de
// escucha los trata tryListen() para poder probar el siguiente puerto; si
// hubiera uno general, el puerto ocupado saldria como error feo en pantalla
// justo antes de cambiar de puerto, que es un caso perfectamente normal.

/* ============================== arranque ================================== */

let announced = false;
const ports = [...new Set([requestedPort, ...FALLBACK_PORTS])];
let attempt = 0;

let intentoActivo = false;   // hay un intento de escucha en curso

function tryListen() {
  const port = ports[attempt];
  intentoActivo = true;
  server.removeListener("error", onListenError);
  server.on("error", onListenError);
  server.listen(port, host, () => {
    // Si este puerto ya habia fallado, este aviso llega tarde y no vale.
    if (!intentoActivo) return;
    intentoActivo = false;
    server.removeListener("error", onListenError);
    // El puerto de verdad lo dice el propio servidor: no se supone.
    const dir = server.address();
    announce(dir && dir.port ? dir.port : port);
  });
}

function onListenError(e) {
  if (!intentoActivo) return;
  intentoActivo = false;
  server.removeListener("error", onListenError);
  if (e && e.code === "EADDRINUSE" && attempt < ports.length - 1) {
    console.log(`AVISO: el puerto ${ports[attempt]} esta ocupado; pruebo el ${ports[attempt + 1]}.`);
    attempt++;
    setImmediate(tryListen);
    return;
  }
  console.error("No puedo escuchar en " + host + ":" + ports[attempt] + " -> " + (e && e.message));
  if (e && e.code === "EADDRINUSE") {
    console.error("Cierra el otro programa o arranca con: node tools/ble_kiss_server.js --port 8123");
  }
  process.exit(1);
}

function announce(port) {
  if (announced) return;
  announced = true;
  const url = `http://localhost:${port}/`;
  if (port !== requestedPort) {
    console.log("");
    console.log(`AVISO: el puerto ${requestedPort} estaba ocupado, asi que escucho en el ${port}.`);
    console.log(`       La direccion buena es esta (no la del 8099): ${url}`);
  }
  console.log("");
  console.log("================================================================");
  console.log("  Prueba de KISS por Bluetooth (BLE) - servidor local listo");
  console.log("----------------------------------------------------------------");
  console.log("  1. Abre esta direccion en Chrome o Edge:");
  console.log("");
  console.log("         " + url);
  console.log("");
  console.log("  2. En la pagina, pulsa el boton \"Conectar\" y elige el nodo en");
  console.log("     la lista. El navegador exige que lo pulse una persona:");
  console.log("     no se puede hacer automaticamente.");
  console.log("----------------------------------------------------------------");
  console.log("  Registro de la pagina : " + LOG_FILE);
  console.log("  Tramas decodificadas  : " + FRAMES_FILE);
  console.log("  (el log se escribe segun llega: puedes ir leyendo el fichero)");
  console.log("  Ctrl+C para parar.");
  console.log("================================================================");
  console.log("");
  if (wantOpen) openBrowser(url);
}

// Abrir el navegador es un extra: si falla, la URL ya esta en pantalla.
function openBrowser(url) {
  const { spawn } = require("child_process");
  const cmd = process.platform === "win32" ? "cmd"
            : process.platform === "darwin" ? "open" : "xdg-open";
  const args = process.platform === "win32" ? ["/c", "start", "", url] : [url];
  try {
    const child = spawn(cmd, args, { detached: true, stdio: "ignore" });
    child.on("error", () => console.log("(no he podido abrir el navegador solo; abre la URL a mano)"));
    child.unref();
  } catch (e) {
    console.log("(no he podido abrir el navegador solo; abre la URL a mano)");
  }
}

let closing = false;
function shutdown(code) {
  if (closing) return;
  closing = true;
  console.log("\nCerrando servidor...");
  try {
    server.close();
  } catch (e) {
    /* ya estaba cerrado */
  }
  let pending = 2;
  const done = () => {
    pending--;
    if (pending <= 0) process.exit(code || 0);
  };
  kissOut.end(done);
  framesOut.end(done);
  // Red de seguridad: si un stream no cierra, se sale igualmente.
  setTimeout(() => process.exit(code || 0), 1500).unref();
}

process.on("SIGINT", () => shutdown(0));
process.on("SIGTERM", () => shutdown(0));
process.on("uncaughtException", (e) => {
  console.error("AVISO: fallo inesperado (el servidor sigue): " + (e && e.stack ? e.stack : e));
});

tryListen();
