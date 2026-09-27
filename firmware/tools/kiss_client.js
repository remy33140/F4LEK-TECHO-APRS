#!/usr/bin/env node
// kiss_client.js — KISS test client for the USB port of the node (no mobile needed).
//
// It speaks KISS over the USB serial port so the feature can be verified with a
// cable and a laptop: it decodes every AX.25 frame the node sends and prints it
// as a readable APRS line, and it can encode a text frame into AX.25 + KISS and
// hand it to the node to transmit over LoRa.
//
// Uso / Usage:
//   node tools/kiss_client.js --list
//   node tools/kiss_client.js --port COM36 --listen
//   node tools/kiss_client.js --port COM36 --send "EA2OY-7>APZFKT,WIDE1-1:>PRUEBA KISS DESDE EL PC"
//   node tools/kiss_client.js --port COM36 --send "<linea>" --repeat 3 --gap 8 --wait 6
//   node tools/kiss_client.js --send "<linea>" --dump tramas.kiss   (sin puerto)
//   node tools/kiss_client.js --selftest          (no port needed: codec check)
//
// Ports: no npm dependency here either. On Windows the bytes are written by a
// PowerShell helper that reads them FROM A FILE ([System.IO.File]::ReadAllBytes
// + SerialPort.Write); they are never streamed through a child's stdin, because
// a redirected stdin pipe cannot be polled in PowerShell (DataAvailable does not
// exist on it and Peek() blocks) and that made the old version drop every frame
// in silence. A failure to open the port or to write now stops the client with a
// clear message. On Linux/macOS the device is opened directly with a descriptor.
//
// Requirements on the node: `set tncProtocol 2` (KISS). While KISS is on the
// node does not send its own beacons: it only transmits what this client sends
// and keeps digipeating. `set tncProtocol 0` puts it back.
// License: GPL-3.0

"use strict";

const fs = require("fs");
const path = require("path");
const os = require("os");
const { spawn, spawnSync } = require("child_process");

const FEND = 0xc0;
const FESC = 0xdb;
const TFEND = 0xdc;
const TFESC = 0xdd;
const MAX_AX25 = 340;

/* ===================== AX.25 (inline decoder/encoder) ===================== */

// One 7-byte AX.25 address -> "CALL-SSID".
function decodeAddress(buf, off) {
  let call = "";
  for (let i = 0; i < 6; i++) {
    const c = buf[off + i] >> 1;
    if (c === 0x20) continue;
    if (c < 0x20 || c > 0x7e) return null;
    call += String.fromCharCode(c);
  }
  if (!call) return null;
  const ssid = (buf[off + 6] >> 1) & 0x0f;
  const last = (buf[off + 6] & 0x01) !== 0;  // extension bit: last address
  const h = (buf[off + 6] & 0x80) !== 0;     // H bit: already repeated
  return { text: ssid ? call + "-" + ssid : call, last, h };
}

// Binary AX.25 UI frame -> "SRC>DST[,PATH]:info" (null when not a UI frame).
function ax25ToText(buf) {
  if (!Buffer.isBuffer(buf) || buf.length < 16) return null;
  const dst = decodeAddress(buf, 0);
  if (!dst) return null;
  const src = decodeAddress(buf, 7);
  if (!src) return null;
  let off = 14;
  let last = src.last;
  const pathParts = [];
  while (!last) {
    if (off + 7 > buf.length) return null;
    const d = decodeAddress(buf, off);
    if (!d) return null;
    pathParts.push(d.text + (d.h ? "*" : ""));
    last = d.last;
    off += 7;
  }
  if (off + 2 > buf.length) return null;
  if (buf[off] !== 0x03 || buf[off + 1] !== 0xf0) return null;  // UI + no layer 3
  const info = buf.slice(off + 2).toString("latin1");
  const route = pathParts.length ? "," + pathParts.join(",") : "";
  return `${src.text}>${dst.text}${route}:${info}`;
}

// "CALL-SSID" -> {call, ssid}; returns an error string when it cannot be
// represented in AX.25 (more than 6 characters or SSID outside 0-15).
function parseAddress(text) {
  let t = String(text).trim();
  let repeated = false;
  if (t.endsWith("*")) {
    repeated = true;
    t = t.slice(0, -1).trim();
  }
  let call = t;
  let ssid = 0;
  const dash = t.lastIndexOf("-");
  if (dash > 0) {
    call = t.slice(0, dash);
    const s = t.slice(dash + 1);
    if (!/^\d+$/.test(s)) return { error: `SSID no numerico en "${text}"` };
    ssid = Number(s);
    if (ssid < 0 || ssid > 15) return { error: `SSID fuera de 0-15 en "${text}"` };
  }
  if (call.length === 0) return { error: `indicativo vacio en "${text}"` };
  if (call.length > 6) {
    return { error: `"${call}" tiene ${call.length} caracteres: AX.25 solo admite 6` };
  }
  if (!/^[A-Za-z0-9/-]+$/.test(call)) return { error: `caracteres no validos en "${call}"` };
  return { call: call.toUpperCase(), ssid, repeated };
}

// One 7-byte address out of {call, ssid, repeated}. last = extension bit.
// An SSID with a trailing "*" means that digipeater already repeated the frame,
// so only use it if the node really heard it from that digi.
function writeAddress(out, addr, last) {
  for (let i = 0; i < 6; i++) {
    const c = i < addr.call.length ? addr.call.charCodeAt(i) : 0x20;
    out.push(c << 1);
  }
  let ssidByte = 0x60 | ((addr.ssid & 0x0f) << 1) | (last ? 1 : 0);
  if (addr.repeated) ssidByte |= 0x80;  // H bit: already repeated
  out.push(ssidByte);
}

// "SRC>DST[,PATH]:info" -> binary AX.25 UI frame.
// Returns {frame} or {error}; never truncates silently.
function textToAx25(line) {
  const colon = line.indexOf(":");
  if (colon < 0) return { error: "falta ':' antes del texto (SRC>DST,PATH:info)" };
  const head = line.slice(0, colon);
  const info = Buffer.from(line.slice(colon + 1), "latin1");
  const gt = head.indexOf(">");
  if (gt <= 0) return { error: "falta '>' entre origen y destino" };
  const src = parseAddress(head.slice(0, gt));
  if (src.error) return { error: "origen: " + src.error };
  const rest = head.slice(gt + 1);
  const comma = rest.indexOf(",");
  const dstText = comma < 0 ? rest : rest.slice(0, comma);
  const pathText = comma < 0 ? "" : rest.slice(comma + 1);
  const dst = parseAddress(dstText);
  if (dst.error) return { error: "destino: " + dst.error };

  const pathAddrs = [];
  if (pathText.trim()) {
    for (const p of pathText.split(",")) {
      if (!p.trim()) continue;
      const a = parseAddress(p);
      if (a.error) return { error: "ruta: " + a.error };
      pathAddrs.push(a);
    }
  }
  if (pathAddrs.length > 8) return { error: "ruta con mas de 8 repetidores" };
  if (info.length > 256) return { error: `info de ${info.length} bytes: maximo 256` };

  const out = [];
  // The extension bit (0x01) marks the LAST address of the chain: the
  // destination is never last (source and/or digipeaters follow it).
  writeAddress(out, dst, false);
  writeAddress(out, src, pathAddrs.length === 0);
  pathAddrs.forEach((a, i) => writeAddress(out, a, i === pathAddrs.length - 1));
  out.push(0x03, 0xf0);
  return { frame: Buffer.concat([Buffer.from(out), info]) };
}

/* ============================== KISS framing ============================== */

// Complete AX.25 frame -> KISS bytes (FEND + command 0 + escaped body + FEND).
function kissEncode(ax25) {
  const out = [FEND, 0x00];
  for (const b of ax25) {
    if (b === FEND) out.push(FESC, TFEND);
    else if (b === FESC) out.push(FESC, TFESC);
    else out.push(b);
  }
  out.push(FEND);
  return Buffer.from(out);
}

// Byte-at-a-time KISS receiver: calls onFrame(ax25Buffer) per complete frame.
// Mirrors the node state machine in src/kiss.cpp (same framing rules), so
// --selftest exercises exactly the cases the firmware has to survive.
class KissDecoder {
  constructor(onFrame, onIgnored) {
    this.onFrame = onFrame;
    this.onIgnored = onIgnored || (() => {});
    this.reset();
  }
  reset() {
    this.inFrame = false;
    this.escaped = false;
    this.overrun = false;
    this.buf = [];
  }
  feed(b) {
    if (b === FEND) {
      if (this.inFrame) {
        if (!this.overrun && this.buf.length > 1) {
          const cmd = this.buf[0];
          if ((cmd & 0x0f) === 0 && (cmd >> 4) === 0) this.onFrame(Buffer.from(this.buf.slice(1)));
          else this.onIgnored(`comando KISS 0x${cmd.toString(16)} ignorado`);
        } else if (!this.overrun) {
          this.onIgnored("trama KISS vacia");
        } else {
          this.onIgnored("trama KISS demasiado larga: descartada");
        }
      }
      this.reset();
      this.inFrame = true;
      return;
    }
    if (!this.inFrame) return;
    if (this.escaped) {
      this.escaped = false;
      if (b === TFEND) b = FEND;
      else if (b === TFESC) b = FESC;
    } else if (b === FESC) {
      this.escaped = true;
      return;
    }
    if (this.buf.length >= MAX_AX25) {
      this.overrun = true;
      return;
    }
    this.buf.push(b);
  }
}

/* ============================== port listing ============================== */

// "pwsh" (PowerShell 7) when present, otherwise Windows PowerShell 5.1.
let PS = null;
function powershell() {
  if (PS) return PS;
  for (const exe of ["pwsh", "powershell.exe", "powershell"]) {
    const r = spawnSync(exe, ["-NoProfile", "-Command", "exit 0"], { stdio: "ignore" });
    if (!r.error) {
      PS = exe;
      return PS;
    }
  }
  PS = "pwsh";
  return PS;
}

function listPorts() {
  if (process.platform === "win32") {
    const ps =
      "[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object { Write-Output $_ }";
    const r = spawnSync(powershell(), ["-NoProfile", "-Command", ps], { encoding: "utf8" });
    if (r.error) {
      console.error("No puedo listar los puertos: " + r.error.message);
      return [];
    }
    return (r.stdout || "").split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
  }
  const out = [];
  for (const dir of ["/dev"]) {
    let names = [];
    try {
      names = fs.readdirSync(dir);
    } catch (e) {
      continue;
    }
    for (const n of names) {
      if (/^(tty(ACM|USB|S)\d+|cu\.\w+)$/.test(n)) out.push(path.join(dir, n));
    }
  }
  return out.sort();
}

/* ============================ serial transports =========================== */

// How bytes are written on Windows: NOT through the child's stdin. The old
// bridge read stdin with `$stdin.DataAvailable`, which does not exist on the
// redirected pipe stream (System.IO.__ConsoleStream): PowerShell 5.1 returns
// $null instead of throwing, so the branch was never taken and the written bytes
// were silently discarded — exactly the "client says TX, nothing arrives"
// failure seen on the bench 2026-09-13. `[Console]::In.Peek()` is not a fix
// either: it blocks on that stream (proved on the bench harness). So the bytes
// travel in a FILE and PowerShell writes them in one go; listening never touches
// stdin at all.
function psWriteScript() {
  return [
    "param([string]$Port,[int]$Baud,[string]$B64File,[int]$WaitMs,[int]$Cycles,[int]$GapMs)",
    "$ErrorActionPreference='Stop'",
    // Fail loudly and legibly: a wrong port name must be reported by the client.
    // The frame travels as base64 text: it survives any encoding of the temp file
    // and FromBase64String() exists in every PowerShell (Convert.FromHexString
    // does NOT exist in Windows PowerShell 5.1, where it silently left the buffer
    // null and wrote nothing - found on the bench 2026-09-13).
    "try { $bytes=[System.Convert]::FromBase64String([System.IO.File]::ReadAllText($B64File,[System.Text.Encoding]::ASCII)) }",
    "catch { [Console]::Error.WriteLine('No puedo leer ' + $B64File + ': ' + $_.Exception.Message); exit 3 }",
    "if ($bytes -eq $null -or $bytes.Length -eq 0) { [Console]::Error.WriteLine('Trama vacia en ' + $B64File); exit 3 }",
    "try { $sp=New-Object System.IO.Ports.SerialPort($Port,$Baud,'None',8,'One') }",
    "catch { [Console]::Error.WriteLine('Puerto ' + $Port + ' no valido: ' + $_.Exception.Message); exit 4 }",
    "$sp.DtrEnable=$true",
    "$sp.RtsEnable=$true",
    "$sp.ReadTimeout=200",
    "$sp.WriteTimeout=5000",
    "$sp.WriteBufferSize=4096",
    "try { $sp.Open() }",
    "catch { [Console]::Error.WriteLine('No puedo abrir ' + $Port + ': ' + $_.Exception.Message); exit 5 }",
    "$exitCode=0",
    "try {",
    "  $stdout=[Console]::OpenStandardOutput()",
    "  $buf=New-Object byte[] 1024",
    // The node may reboot when the port opens (DTR); give it time to come up.
    "  Start-Sleep -Milliseconds 1000",
    "  $sp.DiscardInBuffer()",
    "  for ($c=0; $c -lt $Cycles; $c++) {",
    "    $sp.Write($bytes,0,$bytes.Length)",
    "    $sp.BaseStream.Flush()",
    "    [Console]::Error.WriteLine('#KISS-SENT ' + $bytes.Length)",
    "    $deadline=[DateTime]::UtcNow.AddMilliseconds($WaitMs)",
    "    while ([DateTime]::UtcNow -lt $deadline) {",
    "      $av=$sp.BytesToRead",
    "      if ($av -gt 0) {",
    "        $n=$sp.Read($buf,0,[Math]::Min($av,$buf.Length))",
    "        if ($n -gt 0) { $stdout.Write($buf,0,$n); $stdout.Flush() }",
    "        continue",
    "      }",
    "      Start-Sleep -Milliseconds 5",
    "    }",
    "    if ($c + 1 -lt $Cycles) { Start-Sleep -Milliseconds $GapMs }",
    "  }",
    "} catch { [Console]::Error.WriteLine('Fallo escribiendo en ' + $Port + ': ' + $_.Exception.Message); $exitCode=6 }",
    "finally { try { $sp.Close() } catch {} }",
    "exit $exitCode",
  ].join("\n");
}

// Listen-only bridge: serial -> stdout, no stdin anywhere in the loop.
function psListenScript() {
  return [
    "param([string]$Port,[int]$Baud)",
    "$ErrorActionPreference='Stop'",
    "try { $sp=New-Object System.IO.Ports.SerialPort($Port,$Baud,'None',8,'One') }",
    "catch { [Console]::Error.WriteLine('Puerto ' + $Port + ' no valido: ' + $_.Exception.Message); exit 4 }",
    "$sp.DtrEnable=$true",
    "$sp.RtsEnable=$true",
    "$sp.ReadTimeout=200",
    "try { $sp.Open() }",
    "catch { [Console]::Error.WriteLine('No puedo abrir ' + $Port + ': ' + $_.Exception.Message); exit 5 }",
    "try {",
    "  $stdout=[Console]::OpenStandardOutput()",
    "  $buf=New-Object byte[] 1024",
    "  while ($true) {",
    "    $av=$sp.BytesToRead",
    "    if ($av -gt 0) {",
    "      $n=$sp.Read($buf,0,[Math]::Min($av,$buf.Length))",
    "      if ($n -gt 0) { $stdout.Write($buf,0,$n); $stdout.Flush() }",
    "      continue",
    "    }",
    "    Start-Sleep -Milliseconds 10",
    "  }",
    "} finally { try { $sp.Close() } catch {} }",
  ].join("\n");
}

let tmpSeq = 0;
// Windows PowerShell refuses -File unless the script ends in .ps1, so the
// extension is part of the name, not a detail.
function writeTempFile(prefix, content, { ps1 = false } = {}) {
  const file = path.join(os.tmpdir(), `${prefix}_${process.pid}_${++tmpSeq}${ps1 ? ".ps1" : ""}`);
  fs.writeFileSync(file, content, "utf8");
  return file;
}

function dropTemp(file) {
  try {
    fs.unlinkSync(file);
  } catch (e) {
    /* already gone */
  }
}

// Send KISS bytes through the port with one PowerShell run (file based).
// onBytes receives whatever the node sends back while waiting; stderr lines are
// forwarded as info. Returns {ok, sent, error}.
function psWritePort(port, baud, kissBytes, { waitMs, cycles, gapMs, onBytes }) {
  // base64, not hex: it survives the temp file being read back as text (see
  // psWriteScript for why hex decoding was a trap in PowerShell 5.1).
  const b64 = kissBytes.toString("base64");
  const b64File = writeTempFile("dsh_kiss_b64", b64);
  const scriptFile = writeTempFile("dsh_kiss_write", psWriteScript() + "\n", { ps1: true });
  const args = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptFile, port, String(baud),
                b64File, String(waitMs), String(cycles), String(gapMs)];
  const r = spawnSync(powershell(), args, { encoding: "buffer", maxBuffer: 8 * 1024 * 1024 });
  dropTemp(b64File);
  dropTemp(scriptFile);
  if (r.error) return { ok: false, sent: 0, error: "no puedo ejecutar PowerShell: " + r.error.message };
  const stderr = (r.stderr || Buffer.alloc(0)).toString("utf8");
  for (const line of stderr.split(/\r?\n/)) {
    if (!line.trim()) continue;
    if (line.startsWith("#KISS-SENT ")) {
      if (onBytes) onBytes(null, line.trim());  // progress line
      continue;
    }
    console.error("[puerto] " + line);
  }
  const sent = (stderr.match(/#KISS-SENT /g) || []).length;
  const stdout = r.stdout || Buffer.alloc(0);
  if (stdout.length && onBytes) onBytes(stdout, null);
  if (r.status !== 0) {
    return { ok: false, sent, error: `PowerShell termino con codigo ${r.status} (ver [puerto] arriba)` };
  }
  return { ok: true, sent, error: null };
}

// Listen-only stream (Windows: PowerShell without stdin; Linux: the fd).
function openListener(port, baud) {
  if (process.platform === "win32") {
    const scriptFile = writeTempFile("dsh_kiss_listen", psListenScript() + "\n", { ps1: true });
    const child = spawn(powershell(), ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptFile, port, String(baud)], {
      stdio: ["ignore", "pipe", "pipe"],  // no stdin at all: nothing can block on it
    });
    child.on("exit", () => dropTemp(scriptFile));
    return child;
  }
  const fdInfo = openFd(port, baud);
  if (!fdInfo) return null;
  const stream = fs.createReadStream(null, { fd: fdInfo.fd, autoClose: false });
  stream.on("error", (e) => {
    console.error("Error leyendo " + port + ": " + e.message);
    process.exit(2);
  });
  return stream;
}

// Linux/macOS: raw termios + file descriptor.
function openFd(port, baud) {
  const stty = spawnSync("stty", ["-F", port, String(baud), "raw", "-echo", "-crtscts"], { encoding: "utf8" });
  if (stty.error || stty.status !== 0) {
    console.error("stty fallo: " + (stty.error ? stty.error.message : stty.stderr));
    return null;
  }
  const fd = fs.openSync(port, "r+");
  return { fd, port };
}

// Linux/macOS: write straight to the descriptor (no child process involved).
function fdSend(port, baud, kissBytes, { onBytes }) {
  const fdInfo = openFd(port, baud);
  if (!fdInfo) return { ok: false, sent: 0, error: "no puedo abrir " + port };
  try {
    const n = fs.writeSync(fdInfo.fd, kissBytes);
    if (n !== kissBytes.length) {
      return { ok: false, sent: 0, error: `escritos ${n}/${kissBytes.length} bytes` };
    }
    // Read whatever comes back for a moment, then let the caller listen on.
    const until = Date.now() + 1500;
    while (Date.now() < until) {
      const buf = Buffer.alloc(1024);
      try {
        const got = fs.readSync(fdInfo.fd, buf, 0, buf.length);
        if (got > 0 && onBytes) onBytes(buf.slice(0, got));
      } catch (e) {
        /* nothing to read right now */
      }
      sleepMs(20);
    }
  } finally {
    fs.closeSync(fdInfo.fd);
  }
  return { ok: true, sent: 1, error: null };
}

function sleepMs(ms) {
  const until = Date.now() + ms;
  while (Date.now() < until) {
    try {
      Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, Math.min(50, until - Date.now()));
    } catch (e) {
      break;  // no SharedArrayBuffer (very old node): busy-wait below
    }
  }
}

/* ================================== main ================================== */

const HELP = `kiss_client.js — cliente KISS de prueba por USB

  node tools/kiss_client.js --list
  node tools/kiss_client.js --port COM36 --listen
  node tools/kiss_client.js --port COM36 --send "EA2OY-7>APZFKT,WIDE1-1:>PRUEBA KISS DESDE EL PC"
  node tools/kiss_client.js --port COM36 --send "<linea>" --repeat 4 --gap 10 --wait 6
  node tools/kiss_client.js --send "<linea>" --dump tramas.kiss     (sin puerto, para inspeccionar)
  node tools/kiss_client.js --selftest

Opciones:
  --list            lista los puertos serie disponibles y sale
  --port <nombre>   puerto a usar (COM36, /dev/ttyACM0...)
  --baud <n>        velocidad (por defecto 115200)
  --listen          escucha y traduce cada trama AX.25 que manda el nodo
  --send "<linea>"  manda una trama "SRC>DST[,RUTA]:info" (se puede repetir)
                    y despues lee lo que el nodo conteste durante --wait segundos
  --repeat <n>      numero de envios (por defecto 1)
  --gap <seg>       segundos entre envios (por defecto 10)
  --wait <seg>      segundos de lectura despues de cada envio (por defecto 4)
  --raw             muestra tambien los bytes KISS en hexadecimal
  --dump <fichero>  escribe los bytes KISS en un fichero y sale (no abre el puerto)
  --selftest        comprueba el codificador/decodificador sin abrir el puerto

El envio va por fichero temporal: el cliente escribe los bytes KISS y PowerShell los
manda al puerto de una vez. Si el puerto no se puede abrir o los bytes no salen, el
cliente lo dice y termina con error (nada de fallos silenciosos).

Antes de usarlo, en el nodo: set tncProtocol 2   (y set tncProtocol 0 para volver)
`;

function parseArgs(argv) {
  const o = { baud: 115200, repeat: 1, gap: 10, wait: 4, listen: false, raw: false, sends: [] };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--list") o.list = true;
    else if (a === "--port") o.port = argv[++i];
    else if (a === "--baud") o.baud = Number(argv[++i]) || 115200;
    else if (a === "--listen") o.listen = true;
    else if (a === "--raw") o.raw = true;
    else if (a === "--selftest") o.selftest = true;
    else if (a === "--dump") o.dump = argv[++i];
    else if (a === "--send") o.sends.push(argv[++i] || "");
    else if (a === "--repeat") o.repeat = Math.max(1, Number(argv[++i]) || 1);
    else if (a === "--gap") o.gap = Math.max(1, Number(argv[++i]) || 10);
    else if (a === "--wait") o.wait = Math.max(1, Number(argv[++i]) || 4);
    else if (a === "--help" || a === "-h") o.help = true;
    else if (a.startsWith("--")) {
      console.error("Opcion desconocida: " + a);
      o.help = true;
    } else {
      o.sends.push(a);  // "SRC>DST:info" suelto equivale a --send
    }
  }
  return o;
}

function fmtFrame(line) {
  const ts = new Date().toISOString().slice(11, 19);
  return `[${ts}] ${line}`;
}

// Callback-free encoder, for the framing cases the round trip cannot build.
function kissEncodeRaw(body, commandByte) {
  const out = [FEND, commandByte];
  for (const b of body) {
    if (b === FEND) out.push(FESC, TFEND);
    else if (b === FESC) out.push(FESC, TFESC);
    else out.push(b);
  }
  out.push(FEND);
  return Buffer.from(out);
}

function runSelfTest() {
  const cases = [
    "EA2OY-7>APZFKT,WIDE1-1:!3000.00S/14000.00W>prueba",
    "EA2OY-7>APZFKT:>sin ruta",
    "EA2OY-7>APZFKT,WIDE1-1*,WIDE2-1:@092345z3000.00S/14000.00W_",
    // SSID with two digits, which is exactly what the node failed to parse on
    // the bench (2026-09-13): the callsign scan ate the "-" and rejected the
    // address, so the whole RF -> KISS direction stayed silent. Keep this case.
    "EA2OY-10>APLRG1,WIDE1-1:!3000.00S/14000.00W# H37% 26.1C B3.87V",
    "N0CALL-15>APZFKT,WIDE1-1,WIDE2-2:>dos digitos y ruta larga",
  ];
  let fails = 0;
  for (const line of cases) {
    const enc = textToAx25(line);
    if (enc.error) {
      console.log("FALLO codificando " + line + ": " + enc.error);
      fails++;
      continue;
    }
    const kiss = kissEncode(enc.frame);
    let decoded = null;
    const dec = new KissDecoder((f) => {
      decoded = ax25ToText(f);
    });
    for (const b of kiss) dec.feed(b);
    if (decoded !== line) {
      console.log(`FALLO ida y vuelta:\n  entra  ${line}\n  sale   ${decoded}`);
      fails++;
    } else {
      console.log("OK  " + decoded);
    }
  }
  // The limits must be reported, never truncated.
  const bad = textToAx25("EA2OYLARGO-7>APZFKT:>x");
  if (!bad.error) {
    console.log("FALLO: un indicativo de 9 caracteres deberia dar error");
    fails++;
  } else {
    console.log("OK  limite de indicativo: " + bad.error);
  }
  const badSsid = textToAx25("EA2OY-16>APZFKT:>x");
  if (!badSsid.error) {
    console.log("FALLO: un SSID 16 deberia dar error");
    fails++;
  } else {
    console.log("OK  limite de SSID: " + badSsid.error);
  }

  // Framing cases, one by one (same walk-through the firmware state machine
  // needs): they are about the KISS layer, not about AX.25. Every byte of the
  // sequence is fed exactly once, so the count check also proves the state
  // machine never swallows or re-reads a byte.
  const okFrame = textToAx25(cases[0]).frame;
  const framing = [
    ["trama normal", kissEncodeRaw(okFrame, 0x00), 1],
    ["FEND escapado dentro", kissEncodeRaw(Buffer.concat([okFrame, Buffer.from([FEND])]), 0x00), 1, okFrame.length + 1],
    ["FESC escapado dentro", kissEncodeRaw(Buffer.concat([okFrame, Buffer.from([FESC])]), 0x00), 1, okFrame.length + 1],
    ["trama truncada (nunca cierra)", Buffer.from([FEND, 0x00, 0x01, 0x02, 0x03]), 0],
    ["comando 0x01 (no es dato)", kissEncodeRaw(okFrame, 0x01), 0],
    ["trama vacia (1 byte)", Buffer.from([FEND, 0x00, FEND]), 0],
    [
      "dos tramas seguidas sin hueco",
      Buffer.concat([kissEncodeRaw(okFrame, 0x00), kissEncodeRaw(okFrame, 0x00)]),
      2,
    ],
  ];
  for (const [name, bytes, wantFrames, wantLen] of framing) {
    let got = 0;
    let fed = 0;
    let len = -1;
    const d = new KissDecoder((f) => {
      got++;
      len = f.length;
    });
    for (const b of bytes) {
      fed++;
      d.feed(b);
    }
    const ok = got === wantFrames && fed === bytes.length &&
               (wantLen === undefined || len === wantLen);
    if (!ok) fails++;
    console.log(`${ok ? "OK  " : "FALLO "}${name}: ${got}/${wantFrames} tramas, ${fed}/${bytes.length} bytes` +
                (wantLen === undefined ? "" : `, len ${len} (esperado ${wantLen})`));
  }

  console.log(fails ? `${fails} fallo(s)` : "Todo correcto");
  return fails ? 1 : 0;
}

// One decoder for the whole run: what the node sends is printed as APRS lines.
function makeDecoder(o) {
  return new KissDecoder(
    (frame) => {
      const text = ax25ToText(frame);
      if (text) console.log(fmtFrame(text));
      else console.log(fmtFrame(`(AX.25 no-UI, ${frame.length} bytes) ${frame.toString("hex")}`));
      if (o.raw) console.log("        kiss " + kissEncode(frame).toString("hex"));
    },
    (why) => console.log(fmtFrame("(ignorado) " + why))
  );
}

function feedDecoder(decoder, o, buf) {
  // --raw also shows the bytes exactly as they arrive, which is what tells a KISS
  // frame (0xC0 ... 0xC0) apart from text lines the node may answer with.
  if (o.raw) console.log(`        raw ${buf.toString("hex")}`);
  for (const b of buf) decoder.feed(b);
}

// Send every payload, then wait and print whatever the node echoes back. The
// bytes go to the port through a FILE, never through a child's stdin: that is
// the failure this replaces (see psWriteScript).
function runSendCycle(o, payloads, decoder) {
  const isWin = process.platform === "win32";
  const first = payloads[0];
  console.log(`Puerto ${o.port} @ ${o.baud} 8N1 · modo KISS (el nodo debe tener tncProtocol = 2)`);
  console.log(`TX ${first.kiss.length} bytes KISS: ${first.kiss.toString("hex")}`);
  const onBytes = (buf, info) => {
    if (info) console.log("        " + info);
    if (buf) feedDecoder(decoder, o, buf);
  };

  let total = 0;
  let lastErr = null;
  for (let cycle = 0; cycle < o.repeat; cycle++) {
    const r = isWin
      ? psWritePort(o.port, o.baud, first.kiss, {
          waitMs: o.wait * 1000,
          cycles: 1,
          gapMs: 0,
          onBytes,
        })
      : fdSend(o.port, o.baud, first.kiss, { onBytes });
    if (!r.ok) {
      lastErr = r.error;
      break;
    }
    total += r.sent;
    console.log(fmtFrame(`TX -> ${first.line}`));
    if (cycle + 1 < o.repeat) sleepMs(o.gap * 1000);
  }

  if (lastErr !== null || total === 0) {
    console.error(`FALLO: no se pudo entregar la trama (${total} envio(s) confirmado(s)).`);
    console.error("       " + (lastErr || "el puerto no acepto los bytes"));
    console.error("       Comprueba el puerto (--list), que ningun otro programa lo tenga abierto");
    console.error("       y que el nodo tenga tncProtocol = 2. Con --dump puedes ver los bytes.");
    return false;
  }
  console.log(`Entregados ${total} envio(s) KISS (${total * first.kiss.length} bytes) al puerto ${o.port}.`);
  return true;
}

function runListen(o, decoder) {
  const listener = openListener(o.port, o.baud);
  if (!listener) return false;
  const onData = (buf) => feedDecoder(decoder, o, buf);
  if (process.platform === "win32") {
    listener.stdout.on("data", onData);
    listener.stderr.on("data", (d) => process.stderr.write("[puerto] " + d.toString()));
    listener.on("error", (e) => {
      console.error("FALLO: no puedo abrir el puerto: " + e.message);
      process.exit(2);
    });
    listener.on("exit", (code) => {
      console.log(`\nLectura terminada (codigo ${code}).`);
      process.exit(code === 0 ? 0 : 2);
    });
  } else {
    listener.on("data", onData);
  }
  console.log("Escuchando tramas del nodo... (Ctrl+C para salir)");
  return true;
}

function main() {
  const o = parseArgs(process.argv.slice(2));
  if (o.help || (o.list !== true && !o.selftest && !o.dump && !o.port)) {
    console.log(HELP);
    return o.help ? 0 : 1;
  }
  if (o.list) {
    const ports = listPorts();
    console.log(ports.length ? "Puertos serie disponibles:\n  " + ports.join("\n  ")
                            : "No encuentro ningun puerto serie.");
    return 0;
  }
  if (o.selftest && !o.dump) return runSelfTest();

  // Encode everything BEFORE opening the port: a bad callsign must be reported
  // without touching the node (and without transmitting anything).
  const payloads = [];
  for (const line of o.sends) {
    const enc = textToAx25(line);
    if (enc.error) {
      console.error(`ERROR en "${line}": ${enc.error}`);
      console.error("AX.25 no puede representar ese indicativo/SSID: corrigelo (no se envia nada).");
      return 2;
    }
    payloads.push({ line, kiss: kissEncode(enc.frame) });
  }

  if (o.dump) {
    // Inspection mode: no port touched, so the exact bytes can be checked (and
    // the encode path proved) with nothing connected.
    const lines = o.sends.length ? o.sends : ["EA2OY-7>APZFKT,WIDE1-1:>PRUEBA KISS DESDE EL PC"];
    const out = [];
    for (const line of lines) {
      const enc = textToAx25(line);
      if (enc.error) {
        console.error(`ERROR en "${line}": ${enc.error}`);
        return 2;
      }
      const kiss = kissEncode(enc.frame);
      out.push(kiss);
      console.log(`${line}\n  AX.25 ${enc.frame.length} bytes: ${enc.frame.toString("hex")}\n  KISS  ${kiss.length} bytes: ${kiss.toString("hex")}`);
    }
    const joined = Buffer.concat(out);
    fs.writeFileSync(o.dump, joined);
    console.log(`Escritos ${joined.length} bytes KISS en ${o.dump}`);
    return o.selftest ? runSelfTest() : 0;
  }

  if (payloads.length === 0 && !o.listen) {
    console.log(HELP);
    return 1;
  }

  const decoder = makeDecoder(o);
  if (payloads.length) {
    if (payloads.length > 1) {
      console.error("AVISO: --send admite una trama por ejecucion (una linea = una trama); se envia la primera.");
    }
    // Send and read in the same run: one command both transmits and shows what
    // the node answers (bug reports need no second window).
    if (!runSendCycle(o, payloads, decoder)) return 2;
    if (!o.listen) return 0;
    console.log("Sigo escuchando... (Ctrl+C para salir)");
  }
  if (!runListen(o, decoder)) return 2;
  process.on("SIGINT", () => process.exit(0));
  process.on("SIGTERM", () => process.exit(0));
  return null;  // keep running
}

const rc = main();
if (typeof rc === "number") process.exit(rc);
