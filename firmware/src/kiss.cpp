// kiss.cpp — KISS framing core. See kiss.h.
// Pure state machine over bytes: nothing here knows about USB, BLE or APRS, so
// the same code serves the USB port and the Bluetooth link. The state lives in
// a KissState (one per transport); the global kissXxx() calls are the USB
// instance and behave exactly as they did when the state was file-scope.
// License: GPL-3.0

#include "kiss.h"

namespace {

// The USB instance. tnc.cpp / protocol.cpp drive it through the global kissXxx()
// wrappers at the bottom of this file, so the verified USB path is untouched.
KissState gUsb;

// End of frame: decide whether it is a frame we accept and hand it over.
void finishFrame(KissState *st) {
  const bool wasOverrun = st->overrun;
  const size_t len = st->pos;

  // Back to idle first: the handler is allowed to touch the serial port.
  st->inFrame = false;
  st->escaped = false;
  st->overrun = false;
  st->pos = 0;

  if (wasOverrun || len < 2 || len > KISS_MAX_FRAME || st->handler == nullptr) {
    return;
  }

  const uint8_t portCmd = st->frame.data[0];
  // Only port 0, command 0 (data) is for us. Every other command byte
  // (TXDELAY, P, SlotTime, TXtail, FullDuplex, Return...) is ignored on purpose:
  // well-behaved hosts (APRSdroid, APRSIS32) send their parameters on connect and
  // would otherwise be confused by an answer.
  if ((portCmd & 0x0F) != KISS_CMD_DATA) return;
  if ((portCmd >> 4) != 0) return;

  // Body without the command byte: the AX.25 frame, exactly as the radio uses it.
  st->handler(st->frame.data + 1, len - 1);
}

}  // namespace

void kissStateInit(KissState *st) {
  if (st == nullptr) return;
  *st = KissState();  // handler nullptr, idle, no bytes seen
}

void kissStateSetHandler(KissState *st, KissFrameHandler handler) {
  if (st == nullptr) return;
  st->handler = handler;
}

bool kissStateFeed(KissState *st, uint8_t b, uint32_t nowMs) {
  if (st == nullptr) return false;
  st->lastByteMs = nowMs;
  if (b == KISS_FEND) {
    if (st->inFrame) finishFrame(st);
    // A FEND while idle is just a keep-alive marker; it also closes a frame that
    // follows another frame with no gap between them.
    st->inFrame = true;
    st->escaped = false;
    st->overrun = false;
    st->pos = 0;
    return true;
  }

  if (!st->inFrame) return false;  // not our byte: the caller may treat it as text

  if (st->escaped) {
    st->escaped = false;
    if (b == KISS_TFEND) {
      b = KISS_FEND;
    } else if (b == KISS_TFESC) {
      b = KISS_FESC;
    }
    // Any other byte after FESC is taken literally (tolerant, and the stream
    // cannot desynchronise because only FEND ends a frame).
  } else if (b == KISS_FESC) {
    st->escaped = true;
    return true;
  }

  if (st->pos >= KISS_MAX_FRAME) {
    st->overrun = true;  // too long: this frame will be dropped at the closing FEND
    return true;
  }
  st->frame.data[st->pos++] = b;
  return true;
}

bool kissStateInFrame(const KissState *st) {
  return st != nullptr && st->inFrame;
}

void kissStatePoll(KissState *st, uint32_t nowMs) {
  if (st == nullptr) return;
  if (!st->inFrame && !st->escaped) return;
  if ((uint32_t)(nowMs - st->lastByteMs) < KISS_MAX_FRAME_IDLE_MS) return;
  // Half a frame with no bytes for a whole second: the host abandoned it (killed
  // app, reset mid-write). Drop it so the port returns to line mode and a JSON or
  // CLI line behind it is not eaten as KISS payload.
  kissStateReset(st);
  st->lastByteMs = nowMs;
}

void kissStateReset(KissState *st) {
  if (st == nullptr) return;
  st->inFrame = false;
  st->escaped = false;
  st->overrun = false;
  st->pos = 0;
}

// --- USB instance: the original global API over gUsb ------------------------

void kissSetFrameHandler(KissFrameHandler handler) {
  kissStateSetHandler(&gUsb, handler);
}

bool kissFeed(uint8_t b, uint32_t nowMs) { return kissStateFeed(&gUsb, b, nowMs); }

bool kissInFrame() { return kissStateInFrame(&gUsb); }

void kissPoll(uint32_t nowMs) { kissStatePoll(&gUsb, nowMs); }

void kissReset() { kissStateReset(&gUsb); }

size_t kissEncodeMaxLen(size_t len) {
  // 2 x len (a frame of nothing but FEND/FESC bytes) + command byte + 2 FENDs.
  return (len * 2) + 3;
}

size_t kissEncode(const uint8_t *ax25, size_t len, uint8_t *out, size_t outMax) {
  if (ax25 == nullptr || out == nullptr) return 0;
  if (len == 0 || len > KISS_MAX_FRAME) return 0;
  if (outMax < kissEncodeMaxLen(len)) return 0;

  size_t n = 0;
  out[n++] = KISS_FEND;
  out[n++] = KISS_CMD_DATA;  // port 0, command 0
  for (size_t i = 0; i < len; i++) {
    const uint8_t c = ax25[i];
    if (c == KISS_FEND) {
      out[n++] = KISS_FESC;
      out[n++] = KISS_TFEND;
    } else if (c == KISS_FESC) {
      out[n++] = KISS_FESC;
      out[n++] = KISS_TFESC;
    } else {
      out[n++] = c;
    }
  }
  out[n++] = KISS_FEND;
  return n;
}
