// power.h — resilience: battery monitor + deep sleep / LPCOMP wake (N-03)
// Ported patterns from NavaTastic main-nrf52.cpp cpuDeepSleep() + PowerFSM:
//   - ADC P0.31 (divider 0.5) is the authority for sleep decisions AND for
//     programming the LPCOMP wake threshold (fraction of the 3.3 V rail).
//   - Never sleeps while USB VBUS is present (bench-safe).
//   - delay(3000) before arming LPCOMP (settle; N-03 critical fix) and
//     consecutive-low readings before a clean sleep (external-reset case).
// License: GPL-3.0

#pragma once

#include "config.h"

// Power-fail comparator (2.2 V last resort) + init. Call once at boot.
void powerInit();

// True when USB VBUS is connected (node never sleeps on USB).
bool powerUsbPresent();

// Fresh battery reading from the P0.31 divider (mV).
uint16_t powerReadMv();

// Boot check: when false the node must sleep NOW (battery below cut).
// Handles the "woke with low battery after external reset" case with
// consecutive readings before re-sleeping (NavaTastic Reserva/Vivo).
bool powerBootCheck(const DigiConfig &cfg);

// Runtime low-voltage monitor (call periodically with fresh readings):
// sleeps cleanly after sustained low readings (anti-brownout).
void powerLoop(const DigiConfig &cfg);

// Consecutive low readings counter (diagnostics).
uint8_t powerLowCount();

// Radio/OLED/rail off + LPCOMP armed + System OFF. Never returns.
void powerSleepNow(const DigiConfig &cfg);

// Timed deep sleep (System ON LOWPWR + RTC2, blocks <=500 s) then reboot.
// Used by tracker mode between beacons. Never returns.
void powerSleepTimed(const DigiConfig &cfg, uint32_t secs);
