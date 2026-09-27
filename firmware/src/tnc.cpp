// tnc.cpp — USB TNC bridge: TNC2 text (protocol 1) and KISS (protocol 2).
// See tnc.h. The KISS framing core (kiss.cpp) is transport independent; this
// file is the USB byte sink/source for it and holds the two shared helpers
// (tncBuildKissFrame, tncSendHostFrame), so the text <-> AX.25 <-> KISS
// conversion exists exactly once.
// License: GPL-3.0

#include "tnc.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "aprs.h"
#include "ax25.h"
#include "ble_kiss.h"   // bleOutputFrameText(): el Bluetooth es el segundo huesped
#include "diag.h"
#include "flog.h"
#include "kiss.h"

namespace {

DigiConfig *gCfg = nullptr;

// AX25_ERR_* of the last frame that could not be represented in AX.25
// (callsign > 6 characters, SSID outside 0-15, a truncated frame...). 0 = the
// last one was fine. Covers both directions: what the host sent and what we
// failed to replay to it.
int gLastFrameErr = 0;

// Last description of a dropped frame (trip log + diag, never the USB stream).
char gLastFrameNote[64] = "";

// KISS counters (reported by the diag snapshot): frames replayed to the host and
// bytes that really left through USB, so "the listener saw nothing" can be
// traced from the node's own side.
uint32_t gKissFramesOut = 0;
uint32_t gKissUsbTxBytes = 0;

// Set when a KISS frame has been received: from then on the node must not inject
// its own unsolicited text lines into the same USB stream (a KISS app would see
// them as garbage between frames). Replies to JSON/CLI commands are still sent
// on purpose: they are answers the operator asked for, and they are what keeps
// the node configurable while KISS is enabled.
bool gKissSession = false;

// Record why a frame could not be replayed to the KISS host. Before this existed
// the RF -> KISS direction failed in complete silence: a bug in the AX.25
// address parser rejected every frame whose callsign+SSID did not fit in six
// characters (EA2OY-10) and the listener simply saw nothing at all.
void kissFail(int err, const char *where) {
  gLastFrameErr = err;
  snprintf(gLastFrameNote, sizeof(gLastFrameNote), "TNC drop %s err %d", where, err);
  flogLine("%s", gLastFrameNote);
  diagNote(gLastFrameNote);
}

// One complete AX.25 frame arrived from the host (USB via KISS, or Bluetooth).
void kissFrameReceived(const uint8_t *frame, size_t len) {
  gKissSession = true;
  if (!tncKissActive()) return;
  tncSendHostFrame(frame, len);
}

}  // namespace

void tncBindConfig(DigiConfig *cfg) {
  gCfg = cfg;
  kissSetFrameHandler(kissFrameReceived);
}

bool tncActive() { return gCfg != nullptr && gCfg->tncProtocol != CFG_TNC_OFF; }

bool tncTnc2Active() {
  return gCfg != nullptr && gCfg->tncProtocol == CFG_TNC_TNC2;
}

bool tncKissActive() {
  return gCfg != nullptr && gCfg->tncProtocol == CFG_TNC_KISS;
}

// Alcanzado que el puerto, aunque este en KISS, no puede dejar a un operador sin
// poder hablar con SU nodo: un comando `kissoff` por USB pausa la regla "KISS manda"
// (en memoria, no persistente) y el nodo vuelve a aceptar todas sus ordenes normales
// (baliza manual, WX, telemetria...). `kisson` o un reinicio lo rearman.
static bool gKissPaused = false;
void tncKissPause() { gKissPaused = true; }
void tncKissResume() { gKissPaused = false; }
bool tncKissPaused() { return gKissPaused; }

uint8_t tncProtocol() { return gCfg != nullptr ? gCfg->tncProtocol : CFG_TNC_OFF; }

// ★ VIA TNC2: LA TRAMA TIENE QUE SER NUESTRA (arreglado 2026-09-15).
// Antes esto metia en el aire CUALQUIER linea que llevara '>' y ':' y midiera
// menos de 240 bytes, con el unico filtro del indicativo DEL NODO (que lo aplica
// aprsSendTextFrame()). O sea: un programa host podia hacer que el nodo
// transmitiera una posicion o un mensaje A NOMBRE DE OTRO INDICATIVO. En APRS eso
// es suplantacion, y con razon esta mal visto.
// Ahora el campo de origen (lo que hay antes del '>') tiene que ser:
//   1) nuestro indicativo (comparacion sin distinguir mayusculas, como en APRS), y
//   2) un indicativo representable en AX.25 (ax25ValidAddress(): <= 6 caracteres,
//      SSID 0..15, sin espacios), porque uno ilegible es aire tirado.
// Si no cumple, la linea se DESCARTA y se deja constancia en el registro y en el
// diagnostico: el operador tiene que poder ver por que su programa no transmite.
bool tncHandleLine(const String &line) {
  if (!tncTnc2Active()) return false;  // KISS is binary: no text line is a frame

  // TNC2 frame: "SRC>DST[,PATH]:info". Anything else (JSON/CLI) passes.
  int gt = line.indexOf('>');
  int colon = line.indexOf(':');
  if (gt <= 0 || colon <= gt + 1) return false;

  String frame = line;
  frame.trim();
  if (frame.length() == 0 || frame.length() > 240) return true;  // swallowed

  if (gCfg == nullptr) return true;  // no config bound: never transmit

  // 1) source field = OUR callsign? (APRS compares callsigns case-insensitively)
  String src = frame.substring(0, gt);
  src.trim();
  if (strcasecmp(src.c_str(), gCfg->callsign) != 0) {
    snprintf(gLastFrameNote, sizeof(gLastFrameNote),
             "TNC drop TNC2 origen '%s' distinto del nuestro", src.c_str());
    gLastFrameErr = AX25_ERR_CALLSIGN;
    flogLine("%s", gLastFrameNote);
    diagNote(gLastFrameNote);
    return true;  // consumed: it is a frame, just not one we may transmit
  }
  // 2) ...and one that fits in an AX.25 address field.
  if (!ax25ValidAddress(src.c_str())) {
    snprintf(gLastFrameNote, sizeof(gLastFrameNote),
             "TNC drop TNC2 origen no representable '%s'", src.c_str());
    gLastFrameErr = AX25_ERR_CALLSIGN;
    flogLine("%s", gLastFrameNote);
    diagNote(gLastFrameNote);
    return true;
  }

  aprsSendTextFrame(*gCfg, frame.c_str(), frame.length());
  return true;
}

bool tncHandleUsbByte(uint8_t b) {
  if (!tncKissActive()) return false;
  const bool kissByte = kissFeed(b, millis());
  // Any byte the KISS state machine did not take (only possible while no frame
  // is open) falls through to the line protocol: that is what keeps the JSON and
  // CLI paths alive while KISS is enabled.
  if (!kissByte) gKissSession = false;
  return kissByte;
}

void tncUsbPoll() {
  if (!tncKissActive()) return;
  // A frame the host never finished must not swallow the JSON/CLI lines behind
  // it (that is the lockout the operator asked about): after a second without
  // bytes, drop it and go back to line mode.
  kissPoll(millis());
}

void tncOutputFrame(const char *frame) {
  if (frame == nullptr || frame[0] == '\0') return;

  // ★★ UNA TRAMA OIDA POR LA RADIO VA A **TODOS** LOS HUESPEDES QUE ESCUCHEN (2026-09-17) ★★
  //   El Bluetooth es un SEGUNDO puerto serie del nodo, asi que la trama se le ofrece tambien
  //   a el, en el MISMO formato que usa el cable (lo decide `bleOutputFrameText()`, mirando el
  //   selector del TNC: KISS binario o texto TNC2). Si no hay nadie escuchando no se cuenta
  //   como perdida: no es un error, es que no hay huesped.
  //   ★ OJO: esto NO cambia el comportamiento del cable. Si el Bluetooth esta apagado (el
  //     valor de fabrica) la funcion sale en la primera linea, como si no existiera.
  bleOutputFrameText(frame);

  if (!tncActive()) return;

  if (tncKissActive()) {
    // The radio layer hands over the text form; KISS must carry the binary
    // AX.25 frame, so it is rebuilt from that text (source + SSID, destination,
    // path and information field). Same builder the USB sink uses.
    uint8_t out[(2 * (AX25_MAX_INFO + 80)) + 4];
    TncFrameInfo info;
    const size_t outLen = tncBuildKissFrame(frame, out, sizeof(out), &info);
    if (outLen == 0) return;  // already logged and counted by tncBuildKissFrame

    if (!Serial) {
      kissFail(AX25_ERR_BUFFER, "usb");
      return;
    }
    const size_t wrote = Serial.write(out, outLen);
    Serial.flush();
    gKissFramesOut++;
    gKissUsbTxBytes += (uint32_t)wrote;
    if (wrote != outLen) {
      // tud_cdc_n_write() only refuses while the host is not connected (DTR
      // low): worth knowing, because it looks exactly like "no KISS at all".
      kissFail(AX25_ERR_BUFFER, "usb-short");
    } else {
      gLastFrameErr = 0;
      gLastFrameNote[0] = '\0';
    }
    // Proof in the trip log of what left through USB (one line per received
    // frame, exactly like the RX/DG lines around it).
    flogLine("TNC TX %s>%s%s %ub usb%u", info.src, info.dst,
             info.path[0] ? info.path : "", (unsigned)info.axLen,
             (unsigned)wrote);
    return;
  }

  // TNC2 (protocol 1): plain text line, exactly as before.
  if (!gKissSession) {
    Serial.print(frame);
    Serial.print(F("\r\n"));
  }
}

int16_t tncKissSendFrame(const char *textLine) {
  if (!tncKissActive() || gCfg == nullptr) return -110;
  if (textLine == nullptr || textLine[0] == '\0') return -110;
  const size_t textLen = strlen(textLine);
  if (textLen > 240) {  // same ceiling the rest of the radio path uses
    // Too long for the radio path: say so instead of dropping it in silence.
    kissFail(AX25_ERR_INFO, "host-len");
    return -110;
  }
  return aprsSendTextFrame(*gCfg, textLine, textLen);
}

int16_t tncSendHostFrame(const uint8_t *frame, size_t len) {
  if (gCfg == nullptr || frame == nullptr) return -110;

  // Binary AX.25 -> the "SRC>DST,PATH:info" text the radio layer already speaks.
  char line[AX25_MAX_TEXT];
  const int err = ax25ToTextLine(frame, len, line, sizeof(line));
  if (err != AX25_OK) {
    kissFail(err, "host");  // never transmit a frame we cannot represent
    return -110;
  }
  gLastFrameErr = 0;
  gLastFrameNote[0] = '\0';

  const size_t textLen = strlen(line);
  if (textLen == 0 || textLen > 240) {
    kissFail(AX25_ERR_INFO, "host-len");
    return -110;
  }
  // Same transmitter, CAD and mute rules as the USB host path and the node's
  // own frames: a frame from the host is not a special case for the radio.
  return aprsSendTextFrame(*gCfg, line, textLen);
}

size_t tncBuildKissFrame(const char *frame, uint8_t *out, size_t outMax,
                         TncFrameInfo *info) {
  if (frame == nullptr || out == nullptr) return 0;

  char src[16] = "", dst[16] = "", path[AX25_MAX_PATH_LEN] = "";
  const char *gt = strchr(frame, '>');
  const char *colon = strchr(frame, ':');
  if (gt == nullptr || colon == nullptr || colon <= gt) {
    kissFail(AX25_ERR_SHORT, "hdr");
    return 0;
  }

  const size_t srcLen = (size_t)(gt - frame);
  if (srcLen == 0 || srcLen >= sizeof(src)) {
    kissFail(AX25_ERR_CALLSIGN, "src");
    return 0;
  }
  memcpy(src, frame, srcLen);
  src[srcLen] = '\0';

  // "SRC>DST[,PATH]:info": the path, when present, ends at the colon.
  const char *body = gt + 1;
  const char *dstEnd = colon;
  const char *comma = nullptr;
  for (const char *p = body; p < dstEnd; p++) {
    if (*p == ',') {
      comma = p;
      break;
    }
  }
  const char *dstStop = (comma != nullptr) ? comma : dstEnd;
  if (dstStop <= body || (size_t)(dstStop - body) >= sizeof(dst)) {
    kissFail(AX25_ERR_CALLSIGN, "dst");
    return 0;
  }
  memcpy(dst, body, (size_t)(dstStop - body));
  dst[dstStop - body] = '\0';
  if (comma != nullptr && comma + 1 < dstEnd) {
    const size_t pathLen = (size_t)(dstEnd - comma - 1);
    if (pathLen >= sizeof(path)) {
      kissFail(AX25_ERR_PATH, "path");
      return 0;
    }
    memcpy(path, comma + 1, pathLen);
    path[pathLen] = '\0';
  }

  char srcCall[AX25_MAX_CALLSIGN + 1] = "", dstCall[AX25_MAX_CALLSIGN + 1] = "";
  uint8_t srcSsid = 0, dstSsid = 0;
  int err = ax25ParseAddress(src, srcCall, sizeof(srcCall), &srcSsid);
  if (err != AX25_OK) {
    kissFail(err, "src");
    return 0;
  }
  err = ax25ParseAddress(dst, dstCall, sizeof(dstCall), &dstSsid);
  if (err != AX25_OK) {
    kissFail(err, "dst");
    return 0;
  }

  uint8_t ax[AX25_MAX_INFO + 80];
  const size_t infoLen = strlen(colon + 1);
  const int n = ax25Build(srcCall, srcSsid, dstCall, dstSsid, path,
                          (const uint8_t *)(colon + 1), infoLen, ax, sizeof(ax));
  if (n <= 0) {
    kissFail(n, "build");
    return 0;
  }

  // Escape growth is bounded (FEND/FESC are rare in APRS text): 2 x frame + 4.
  const size_t outLen = kissEncode(ax, (size_t)n, out, outMax);
  if (outLen == 0) {
    kissFail(AX25_ERR_BUFFER, "encode");
    return 0;
  }

  if (info != nullptr) {
    memcpy(info->src, src, sizeof(info->src));
    memcpy(info->dst, dst, sizeof(info->dst));
    memcpy(info->path, path, sizeof(info->path));
    info->axLen = n;
  }
  return outLen;
}

int tncKissLastDrop() { return gLastFrameErr; }
uint32_t tncKissUsbTxBytes() { return gKissUsbTxBytes; }
uint32_t tncKissFramesOut() { return gKissFramesOut; }
