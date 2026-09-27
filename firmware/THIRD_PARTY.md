# THIRD-PARTY NOTICES — APRS_NAVARRICA_FAKETEC_433

This firmware is licensed under the GNU General Public License v3.0 (see
`LICENSE`). It incorporates or is derived from the following third-party
works; their notices are preserved in the source files where applicable.

| Component | Source | License | Notes |
|---|---|---|---|
| Adafruit nRF52 Arduino core (`g_ADigitalPinMap`, `initVariant`, wiring) | github.com/adafruit/Adafruit_nRF52_Arduino | LGPL-2.1 | `variants/faketec/variant.cpp` keeps the LGPL boilerplate (lineage Arduino LLC/Sandeep Mistry/Adafruit) |
| Faketec/ProMicro pinout, 3V3-rail enable, SX1262 wiring | NavaTastic fork (Meshtastic 2.7.26), operator repo `C:\NavaTastic Codigo completo` | GPL-3.0 | `variants/faketec/variant.h`, `src/pins_faketec.h` |
| RadioLib | github.com/jgromes/RadioLib | MIT | `lib_deps` in `platformio.ini` |
| ArduinoJson | github.com/bblanchon/ArduinoJson | MIT | `lib_deps` in `platformio.ini` |
| uf2conv.py | github.com/adafruit/Adafruit_UF2 (`tools/uf2conv.py`) via `_referencias\t-echo-lora-aprs` | MIT | `bin/uf2conv.py` |
| APRS-LoRa reference (RF params, config catalog) | richonguzman/LoRa_APRS_iGate (CA2RXU) | GPL-3.0 | Interop reference only (`_referencias\LoRa_APRS_iGate_HEAD`) |

GPL-3.0-compatible incorporation: LGPL-2.1 and MIT components may be combined
into a GPL-3.0 work; LGPL-2.1 and MIT copyright notices are retained verbatim
in the respective files.

FORBIDDEN: CA2RXU nRF52 UF2 binaries (distributed GPL-3.0 without source;
not a source of rights). No such binary is used in this project.
