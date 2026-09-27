// flog.h — persistent trip/audit log in the internal flash
//
// Records what the node did (beacons sent, frames repeated, GPS fixes, power
// events) with the GPS clock, so a trip can be reviewed later over USB.
// Enabled only while the tracker is active (mode 1/2), so a stationary digi
// never wears the flash.
//
// *** NO FLASH WRITES IN THE BOOT PATH (hardware lesson, 2026-09-13) ***
// Programming the flash on this core stalls the CPU on the NVMC and broke the
// USB CDC port of a node that was otherwise perfectly alive (COM present, zero
// lines out). flogInit() therefore only READS, and nothing in setup() may call
// flogLine(): the first line of a boot is written once the node is running (the
// main loop, or the Bluetooth start-up with Bluetooth on). License: GPL-3.0

#pragma once

#include <Arduino.h>

// Scan the log region and locate the last page written. Call once at boot.
// READ-ONLY: it does not touch the flash (a page is formatted lazily by the
// first line that is actually written).
void flogInit();

// Tracker mode only: the caller enables/disables it when the mode changes.
void flogSetEnabled(bool on);
bool flogEnabled();

// Append one timestamped line (printf style). Returns false when the log is
// disabled or the write was refused (low battery / not enough space).
// The single writer of the log: never call it from the boot path.
bool flogLine(const char *fmt, ...);

// Dump every stored line in chronological order (USB CLI "log dump").
void flogDump(Stream &out);

// Same walk, but handing each line to a callback (used in firmware, e.g. the
// ?APRSH query that lists the stations heard in the last hours). The line is
// NUL-terminated and valid only during the call.
void flogEachLine(void (*cb)(const char *line, void *ctx), void *ctx);

// Health: lines stored, pages used, times the ring wrapped around.
void flogStats(Stream &out);

// Erase the whole log region.
bool flogClear();

uint32_t flogLineCount();
