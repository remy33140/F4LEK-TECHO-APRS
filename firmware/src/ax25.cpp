// ax25.cpp — AX.25 UI frames <-> the "SRC>DST,PATH:info" text. See ax25.h.
// Address layout (AX.25 v2.2, 3.12): 7 bytes per address, ASCII shifted left one
// bit and space padded, then SSID = 0x60 | (ssid << 1) | (last ? 1 : 0).
// License: GPL-3.0

#include "ax25.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t kCtrlUI = 0x03;
constexpr uint8_t kPidNone = 0xF0;
constexpr size_t kAddrLen = 7;

bool isCallChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '/';  // "/" is legal inside a callsign
}

// "CALL", "CALL-7" or "CALL*" -> callsign + SSID (+ already-repeated flag).
// The hyphen is the SSID separator, NEVER part of the callsign: the previous
// greedy scan ate it and then reported "> 6 characters", so every address with
// an SSID that did not fit in 6 characters ("EA2OY-10", and the digipeaters
// "WIDE1-1"/"WIDE2-1" of any frame with a longer callsign) was rejected. That
// silently killed the whole RF -> KISS direction (tncOutputFrame() bailed out),
// found on the bench 2026-09-13.
int parseAddress(const char *text, char *call, size_t callMax, uint8_t *ssid,
                 bool *hasBeenRepeated) {
  if (text == nullptr || call == nullptr || ssid == nullptr) return AX25_ERR_CALLSIGN;
  const char *p = text;
  while (*p == ' ') p++;  // tolerate the padding a TNC2 line may carry
  size_t n = 0;
  while (isCallChar(*p)) {
    if (n + 1 >= callMax) return AX25_ERR_CALLSIGN;  // more than 6 characters
    call[n++] = *p++;
  }
  call[n] = '\0';
  if (n == 0) return AX25_ERR_CALLSIGN;

  uint8_t s = 0;
  bool haveSsid = false;
  if (*p == '-') {
    p++;
    if (*p < '0' || *p > '9') return AX25_ERR_SSID;
    unsigned v = 0;
    while (*p >= '0' && *p <= '9') {
      v = (v * 10u) + (unsigned)(*p - '0');
      if (v > 999u) return AX25_ERR_SSID;  // keep the accumulator bounded
      p++;
    }
    if (v > AX25_MAX_SSID) return AX25_ERR_SSID;
    s = (uint8_t)v;
    haveSsid = true;
  }
  if (hasBeenRepeated != nullptr) {
    *hasBeenRepeated = (*p == '*');  // "WIDE1-1*" = already repeated
    if (*p == '*') p++;
  }
  while (*p == ' ') p++;
  if (*p != '\0') return AX25_ERR_CALLSIGN;  // trailing garbage
  *ssid = haveSsid ? s : 0;
  return AX25_OK;
}

// One 7-byte AX.25 address (6 characters << 1, space padded, then SSID byte).
void writeAddress(uint8_t *out, const char *call, uint8_t ssid, bool last) {
  for (size_t i = 0; i < AX25_MAX_CALLSIGN; i++) {
    const char c = (call[i] != '\0') ? call[i] : ' ';
    out[i] = (uint8_t)((uint8_t)c << 1);
  }
  out[6] = (uint8_t)(0x60 | ((ssid & 0x0F) << 1) | (last ? 1 : 0));
}

// Read one 7-byte address into "CALL-SSID". Returns false on illegal bytes.
bool readAddress(const uint8_t *in, bool *last, char *out, size_t outMax) {
  char call[AX25_MAX_CALLSIGN + 1];
  size_t n = 0;
  for (size_t i = 0; i < AX25_MAX_CALLSIGN; i++) {
    char c = (char)(in[i] >> 1);
    if (c == ' ') continue;  // padding
    if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
    if (n + 1 >= outMax) return false;
    call[n++] = c;
  }
  call[n] = '\0';
  if (n == 0) return false;

  const uint8_t ss = (uint8_t)((in[6] >> 1) & 0x0F);
  if (ss == 0) {
    if (n + 1 > outMax) return false;
    memcpy(out, call, n + 1);
  } else {
    if (n + 4 > outMax) return false;  // "-15" + NUL
    snprintf(out, outMax, "%s-%u", call, (unsigned)ss);
  }
  if (last != nullptr) *last = (in[6] & 0x01) != 0;
  return true;
}

}  // namespace

int ax25ParseAddress(const char *text, char *call, size_t callMax, uint8_t *ssid) {
  return parseAddress(text, call, callMax, ssid, nullptr);
}

bool ax25ValidAddress(const char *text) {
  if (text == nullptr) return false;
  // The two buffers exist only because parseAddress() asks for them; the verdict
  // is the whole point of this call.
  char call[AX25_MAX_CALLSIGN + 1];
  uint8_t ssid = 0;
  return parseAddress(text, call, sizeof(call), &ssid, nullptr) == AX25_OK;
}

int ax25Build(const char *src, uint8_t srcSsid, const char *dst, uint8_t dstSsid,
              const char *path, const uint8_t *info, size_t infoLen, uint8_t *out,
              size_t outMax) {
  if (src == nullptr || dst == nullptr || out == nullptr) return AX25_ERR_CALLSIGN;
  if (info == nullptr && infoLen > 0) return AX25_ERR_INFO;
  if (infoLen > AX25_MAX_INFO) return AX25_ERR_INFO;

  char srcCall[AX25_MAX_CALLSIGN + 1];
  char dstCall[AX25_MAX_CALLSIGN + 1];
  uint8_t srcS = 0, dstS = 0;
  int err = parseAddress(src, srcCall, sizeof(srcCall), &srcS, nullptr);
  if (err != AX25_OK) return err;
  err = parseAddress(dst, dstCall, sizeof(dstCall), &dstS, nullptr);
  if (err != AX25_OK) return err;
  // The SSID the caller passes next to the callsign wins when the text form
  // carried none ("EA2OY" + ssid 7 = "EA2OY-7"); with "-7" in the text the two
  // must agree, otherwise the intent is ambiguous.
  if (srcS != 0 && srcS != srcSsid) return AX25_ERR_SSID;
  if (dstS != 0 && dstS != dstSsid) return AX25_ERR_SSID;
  if (srcSsid > AX25_MAX_SSID || dstSsid > AX25_MAX_SSID) return AX25_ERR_SSID;

  // Digipeater path (optional).
  char pathCall[AX25_MAX_PATH][AX25_MAX_CALLSIGN + 1];
  uint8_t pathSsid[AX25_MAX_PATH];
  bool pathRepeated[AX25_MAX_PATH];
  size_t nPath = 0;
  if (path != nullptr && path[0] != '\0') {
    size_t i = 0;
    while (path[i] != '\0') {
      while (path[i] == ' ') i++;
      if (path[i] == '\0') break;
      if (nPath >= AX25_MAX_PATH) return AX25_ERR_PATH;
      char item[24];
      size_t n = 0;
      while (path[i] != '\0' && path[i] != ',') {
        if (n + 1 >= sizeof(item)) return AX25_ERR_PATH;
        item[n++] = path[i++];
      }
      item[n] = '\0';
      while (path[i] == ',') i++;
      bool repeated = false;
      err = parseAddress(item, pathCall[nPath], sizeof(pathCall[0]),
                         &pathSsid[nPath], &repeated);
      if (err != AX25_OK) return AX25_ERR_PATH;
      pathRepeated[nPath] = repeated;
      nPath++;
    }
  }

  const size_t total = kAddrLen * (2 + nPath) + 2 + infoLen;
  if (total > outMax) return AX25_ERR_BUFFER;

  size_t o = 0;
  const bool noPath = (nPath == 0);
  writeAddress(out + o, dstCall, dstSsid, noPath);  // destination goes first
  o += kAddrLen;
  writeAddress(out + o, srcCall, srcSsid, false);  // source is never last here
  o += kAddrLen;
  for (size_t i = 0; i < nPath; i++) {
    const bool last = (i + 1 == nPath);
    writeAddress(out + o, pathCall[i], pathSsid[i], last);
    // H bit (0x80 in the SSID byte) = this digipeater already repeated the frame.
    if (pathRepeated[i]) out[o + 6] |= 0x80;
    o += kAddrLen;
  }
  out[o++] = kCtrlUI;  // UI frame
  out[o++] = kPidNone; // no layer 3
  if (infoLen > 0) memcpy(out + o, info, infoLen);
  o += infoLen;
  return (int)o;
}

int ax25Parse(const uint8_t *frame, size_t len, Ax25Text *out) {
  if (frame == nullptr || out == nullptr) return AX25_ERR_SHORT;
  if (len < 2 * kAddrLen + 2) return AX25_ERR_SHORT;

  memset(out, 0, sizeof(*out));

  size_t o = 0;
  bool last = false;
  if (!readAddress(frame + o, &last, out->dst, sizeof(out->dst))) return AX25_ERR_CALLSIGN;
  o += kAddrLen;
  if (last) return AX25_ERR_SHORT;  // only one address: not a valid frame
  if (!readAddress(frame + o, &last, out->src, sizeof(out->src))) return AX25_ERR_CALLSIGN;
  o += kAddrLen;

  while (!last) {
    if (o + kAddrLen > len) return AX25_ERR_SHORT;
    // Room for one more address (9), the separator (1), the '*' (1) and the NUL.
    if (strlen(out->path) + 12 > sizeof(out->path)) return AX25_ERR_PATH;
    char addr[16];
    if (!readAddress(frame + o, &last, addr, sizeof(addr))) return AX25_ERR_CALLSIGN;
    if (out->path[0] != '\0') strncat(out->path, ",", sizeof(out->path) - strlen(out->path) - 1);
    strncat(out->path, addr, sizeof(out->path) - strlen(out->path) - 1);
    if (last) strncat(out->path, "*", sizeof(out->path) - strlen(out->path) - 1);
    o += kAddrLen;
  }

  if (o + 2 > len) return AX25_ERR_SHORT;
  if (frame[o] != kCtrlUI || frame[o + 1] != kPidNone) return AX25_ERR_NOT_UI;
  o += 2;

  const size_t infoLen = len - o;
  if (infoLen > AX25_MAX_INFO) return AX25_ERR_INFO;
  if (infoLen > 0) memcpy(out->info, frame + o, infoLen);
  out->info[infoLen] = '\0';
  out->infoLen = infoLen;
  return AX25_OK;
}

int ax25ToTextLine(const uint8_t *frame, size_t len, char *line, size_t lineMax) {
  if (line == nullptr || lineMax == 0) return AX25_ERR_BUFFER;
  Ax25Text t;
  const int err = ax25Parse(frame, len, &t);
  if (err != AX25_OK) return err;
  const int n = (t.path[0] != '\0')
                    ? snprintf(line, lineMax, "%s>%s,%s:%s", t.src, t.dst, t.path, t.info)
                    : snprintf(line, lineMax, "%s>%s:%s", t.src, t.dst, t.info);
  if (n <= 0 || (size_t)n >= lineMax) return AX25_ERR_BUFFER;
  return AX25_OK;
}
