// store.h — config persistence in internal flash (APRS_NAVARRICA_FAKETEC_433)
// Two 4 KB pages just below the Adafruit bootloader (0xEA000), double-buffered
// atomic save with CRC32. Pattern: N-03 resilience.bin (atomic + CRC), N-06
// bootloader limit. License: GPL-3.0

#pragma once

#include "config.h"

// Load config from flash. Returns true and applies the stored config when a
// valid snapshot (magic+version+CRC32) exists; otherwise leaves cfg unchanged.
bool storeLoad(DigiConfig &cfg);

// Atomically save the current config. Returns true on success.
bool storeSave(const DigiConfig &cfg);

// Erase both pages (factory). Returns true on success.
bool storeWipe();
