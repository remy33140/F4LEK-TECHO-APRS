// ax25.h — AX.25 UI frame <-> the text form the radio layer speaks
//
// Two jobs:
//   1. ax25Build(): text host input ("EA2OY-7>APZFKT,WIDE1-1:!3000.00S/...")
//      (placeholder coordinates in every example: latitude 3000.00S, never real ones)
//      becomes a binary AX.25 UI frame for KISS.
//   2. ax25Parse(): a binary AX.25 UI frame received over KISS becomes the
//      "SRC>DST[,PATH]:info" text that aprsSendTextFrame() already transmits.
//
// Real AX.25 limits are reported, never hidden: a callsign longer than 6
// characters or an SSID outside 0-15 CANNOT be represented, so the build fails
// with a specific code and the caller warns the user (no silent truncation).
//
// The LoRa APRS wire prefix (0x3C 0xFF 0x01) is NOT here: that belongs to the
// radio layer. This module moves bare AX.25 frames only.
// License: GPL-3.0

#pragma once

#include <stddef.h>
#include <stdint.h>

#define AX25_MAX_CALLSIGN 6    // AX.25 address field: 6 characters
#define AX25_MAX_SSID 15       // 4-bit SSID
#define AX25_MAX_PATH 8        // digipeaters we accept in a path
#define AX25_MAX_PATH_LEN 96   // "WIDE1-1,WIDE2-1,..." as text
#define AX25_MAX_INFO 256      // max information field we build/accept
#define AX25_MAX_TEXT 400      // "SRC>DST,PATH:info" as text

// Error codes (negative), shared by ax25Build() and ax25Parse().
#define AX25_OK 0
#define AX25_ERR_CALLSIGN (-1)       // empty, too long or illegal character
#define AX25_ERR_SSID (-2)           // not a number, or outside 0..15
#define AX25_ERR_PATH (-3)           // malformed / too many digipeaters
#define AX25_ERR_BUFFER (-4)         // output buffer too small
#define AX25_ERR_SHORT (-5)          // fewer bytes than an AX.25 UI header
#define AX25_ERR_INFO (-6)           // information field too long
#define AX25_ERR_NOT_UI (-7)         // control/PID are not a UI frame

// Parse one "CALL-SSID" into callsign + ssid.
// Returns AX25_OK, AX25_ERR_CALLSIGN or AX25_ERR_SSID.
int ax25ParseAddress(const char *text, char *call, size_t callMax, uint8_t *ssid);

// Is `text` an address we can really put in the AX.25 address field?
// This is the ONE yardstick for the callsign: 1..6 characters (A-Z, 0-9 and '/'
// for portable operation), optional "-SSID" with SSID 0..15, no spaces and
// nothing left over. It is the same parse ax25Build()/ax25Parse() apply, so an
// address that passes here is guaranteed to be representable on the air.
// Used to validate the configured callsign (config.cpp) and the source field of
// a frame the host wants transmitted (tnc.cpp): a callsign that fails here
// produces frames other stations cannot read.
bool ax25ValidAddress(const char *text);

// Build an AX.25 UI frame (no FCS: KISS carries the frame without it).
//   src/dst: callsigns without SSID, up to 6 characters (case preserved).
//   path   : optional "WIDE1-1,WIDE2-1" (a trailing '*' marks it already
//            repeated; empty or nullptr = no digipeaters).
//   info   : information field (may contain any byte except a NUL terminator).
// Returns the frame length in bytes, or a negative AX25_ERR_* code.
int ax25Build(const char *src, uint8_t srcSsid, const char *dst, uint8_t dstSsid,
              const char *path, const uint8_t *info, size_t infoLen,
              uint8_t *out, size_t outMax);

// A parsed frame in the text form the radio layer uses.
struct Ax25Text {
  char src[16];               // "EA2OY-7"
  char dst[16];               // "APZFKT"
  char path[AX25_MAX_PATH_LEN];  // "WIDE1-1,WIDE2-1*" ("" when no digipeaters)
  char info[AX25_MAX_INFO + 1];
  size_t infoLen;
};

// Parse a binary AX.25 UI frame into text. Returns AX25_OK or a negative code;
// on error nothing in out must be used.
int ax25Parse(const uint8_t *frame, size_t len, Ax25Text *out);

// Same as ax25Parse(), but writing the ready-to-send "SRC>DST[,PATH]:info" line
// into line. Returns AX25_OK or a negative AX25_ERR_* code.
int ax25ToTextLine(const uint8_t *frame, size_t len, char *line, size_t lineMax);
