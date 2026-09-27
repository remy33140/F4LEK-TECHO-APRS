// kiss.h — KISS framing core (transport independent)
//
// Only the SLIP-style framing of the KISS protocol lives here: no Arduino
// Serial calls, no BLE calls, no APRS knowledge. The caller feeds it one byte
// at a time (kissFeed) and registers a handler (kissSetFrameHandler); when a
// complete frame reassembles, the handler gets the AX.25 frame WITHOUT the
// KISS command byte. That is why the same core serves USB and Bluetooth: each
// transport owns one KissState and drives the very same code (no second KISS
// implementation anywhere).
//
// Framing (KISS as implemented by every APRS TNC):
//   FEND (0xC0) marks both the start and the end of a frame.
//   FESC (0xDB) escapes: 0xDB 0xDC = a literal 0xC0, 0xDB 0xDD = a literal 0xDB.
//   The first byte of the frame body is the command/port byte: high nibble =
//   port, low nibble = command (0 = data).
//
// The LoRa-APRS wire prefix (0x3C 0xFF 0x01) is NOT part of KISS: it belongs to
// the radio layer. KISS carries the bare AX.25 frame.
// License: GPL-3.0

#pragma once

#include <stddef.h>
#include <stdint.h>

// KISS special bytes.
#define KISS_FEND 0xC0
#define KISS_FESC 0xDB
#define KISS_TFEND 0xDC  // byte after FESC that means FEND
#define KISS_TFESC 0xDD  // byte after FESC that means FESC

// Command nibble of the first (command/port) byte.
#define KISS_CMD_DATA 0x00

// Largest AX.25 frame we accept: 8 address octets (7 bytes each) + control +
// PID + a 256-byte information field = 330 bytes, rounded up to the classic
// "340 bytes" AX.25 UI limit used across the APRS ecosystem. A frame that grows
// past this is dropped (but the state machine stays in sync: the closing FEND
// still resets it), so no host can overrun the buffer.
#define KISS_MAX_FRAME 340

// One complete AX.25 frame, no FCS, no KISS framing bytes.
struct KissFrame {
  uint8_t data[KISS_MAX_FRAME];
  size_t len;
};

// Called once per complete, accepted frame (port 0, command 0, 1..KISS_MAX_FRAME
// bytes). The pointer is only valid during the call: copy what you need.
typedef void (*KissFrameHandler)(const uint8_t *frame, size_t len);

// --- One state machine per transport ----------------------------------------
// The framing is transport independent, but it is NOT reentrant: a single set
// of file-scope globals would let the bytes of one host corrupt the frame
// another host is half-way through. USB and Bluetooth are two independent byte
// streams that can be in use at the same time, so each one owns a KissState.
// The kissXxx() calls below the struct are the USB instance, kept byte-for-byte
// as they were: tnc.cpp, protocol.cpp and the verified USB behaviour do not
// change. ble.cpp owns the second instance.
struct KissState {
  KissFrameHandler handler = nullptr;
  bool inFrame = false;    // between the opening and the closing FEND
  bool escaped = false;    // previous byte was FESC
  bool overrun = false;    // frame too long: dropped at the closing FEND
  uint32_t lastByteMs = 0; // when the open frame last advanced (idle timeout)
  size_t pos = 0;
  KissFrame frame;
};

// Reset ONE instance to idle (handler kept). Call before first use.
void kissStateInit(KissState *st);

// Register the frame handler of one instance (nullptr disables it).
void kissStateSetHandler(KissState *st, KissFrameHandler handler);

// Feed one received byte into one instance. Same contract as kissFeed().
bool kissStateFeed(KissState *st, uint8_t b, uint32_t nowMs);

// Per-instance kissPoll(): abandon a frame left half-sent.
void kissStatePoll(KissState *st, uint32_t nowMs);

// Per-instance kissInFrame().
bool kissStateInFrame(const KissState *st);

// Per-instance kissReset().
void kissStateReset(KissState *st);

// --- USB instance (the original global API, unchanged) ----------------------
// Register the frame handler (nullptr disables it). Passing a handler does NOT
// reset the state machine; kissReset() does.
void kissSetFrameHandler(KissFrameHandler handler);

// An open frame is abandoned after this long without a byte: long enough for any
// real host (a 340-byte frame at 115200 baud is ~30 ms), short enough that a
// truncated one can never swallow the JSON/CLI lines that follow it.
#define KISS_MAX_FRAME_IDLE_MS 1000u

// Feed one received byte. nowMs is the caller's millisecond clock (Arduino
// millis(), the caller passes it so this file stays free of Arduino calls); it
// is only used to abandon a frame left half-sent (see kissPoll). Returns true
// when the byte was part of a KISS frame (so the caller's text-line logic must
// ignore it), false when KISS is not engaged and the byte may be treated as text.
bool kissFeed(uint8_t b, uint32_t nowMs);

// Call once per main loop: after KISS_MAX_FRAME_IDLE_MS without a byte, an open
// frame is dropped (never emitted) and the port goes back to line mode. Without
// this, a host that died mid-frame would leave the reader inside a frame for
// ever and every JSON/CLI line would be eaten as KISS payload, locking the
// operator out of the node.
void kissPoll(uint32_t nowMs);

// True while a frame is being reassembled (a start FEND was seen and the closing
// one has not arrived yet). USB instance.
bool kissInFrame();

// Drop any half-received frame and leave the state machine idle. The frame
// handler stays registered.
void kissReset();

// Encode a complete AX.25 frame as KISS bytes in out (FEND, command byte,
// escaped body, FEND). Returns the number of bytes written, or 0 when it does
// not fit: out must hold at least kissEncodeMaxLen(len) bytes.
size_t kissEncode(const uint8_t *ax25, size_t len, uint8_t *out, size_t outMax);

// Worst case size of kissEncode() for a frame of len bytes.
size_t kissEncodeMaxLen(size_t len);
