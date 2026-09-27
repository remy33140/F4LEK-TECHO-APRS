# F4LEK – T-Echo APRS (SOTA)

APRS-over-LoRa firmware (433 MHz) for the **LilyGO T-Echo / T-Echo Plus**, built for
**SOTA activations**: self-spot your summit through APRS2SOTA and see on the e-paper
screen whether the spot went through.

## Based on EA2OY APRS System

This is a fork of **[EA2OY APRS System](https://github.com/EA2OY/EA2OY-APRS-SYSTEM)**
("Kacho System") by EA2OY. It brings the whole base: LoRa APRS digipeater and tracker,
smart beaconing profiles, telemetry, APRS messaging, flash trip log, the e-paper driver
and the web configurator. All credit for that goes to EA2OY. Licence: GPL-3.0, same as
upstream.

## What this fork adds

- **T-Echo only.** Faketec / ProMicro support and the OLED interface have been removed.
- **SOTA menu:** plan the activation once (summit, frequency, mode, callsign), then spot
  it in a couple of clicks with a comment (QRV, QSY, QRT, TEST).
- **SOTA screen** in the carousel: the plan, the APRS2SOTA status of the last spot
  (Sending / Spotted / Dupe / Error / Not sent) and the UTC time for your log.
- **Leaner carousel:** Home, SOTA, Messages, Stations, Last RX, Last TX.
- **Clean home screen:** position, speed, altitude, satellites, received messages.
- **Messages screen** keeping the received APRS messages.
- **English menus and screens.**
- **Bootloader 0.11.0 + SoftDevice S140 7.3.0** on the T-Echo (see below).

## Using it

### Buttons

| Gesture | Outside the menu | In the menu / SOTA wizard |
|---|---|---|
| Touch key (capacitive) | next screen | move / next value |
| Physical button, short | next screen | enter / confirm |
| Physical button, long | **open the menu** | back (exits from the first step) |
| Physical button, double | send a position beacon | decrease a number while editing |

### SOTA

Main menu: `Exit · Sleep · SOTA · Messages · …`

1. **SOTA → Plan activation** (at home or at the foot of the summit):
   association → region → 3-digit number → confirm summit → frequency (6 digits,
   `145.500`) → mode → callsign prefix (`(none)` in your own country).
   `SAVE PLAN?` → short press. The plan is stored in flash and shown on the SOTA screen.
   The first row, `Re F/PE-103`, reuses your last summit and prefix and jumps straight to
   the frequency. Your recent associations are listed first, marked `*`.
2. **SOTA → SOTA spot** (on the summit): pick a comment → `SEND SPOT?` → short press.
   The spot is sent as an APRS message to `APRS2SOTA`, e.g.
   `F/PE-103 145.500MHz SSB F4LEK/P QRV`.
3. **Check the SOTA screen:**

| Status | Meaning |
|---|---|
| Sending | sent, waiting for the gateway's answer |
| **Spotted** | the spot is published on SOTAwatch |
| Dupe | that spot already exists |
| Error | the gateway refused it (e.g. unknown mode) |
| Not sent | radio error, or no acknowledgement after all retries |

To QSY, run *Plan activation* again (the `Re …` row keeps the summit), then *SOTA spot*.

### Configuration

Open the web configurator (`web/index.html`) in Chrome or Edge, connect over USB and
set at least your callsign. The most common settings are also in the on-device menu.

## Flashing the bootloader (S140 7.3.0)

The T-Echo ships with bootloader 0.6.1 + S140 **6.1.1**. This firmware targets
**bootloader 0.11.0 + S140 7.3.0** (applications start at `0x27000`). Every file you
need is in [`bootloader/`](bootloader/). Source and full guide:
[ViezeVingertjes/lilygo-techo-bootloader](https://github.com/ViezeVingertjes/lilygo-techo-bootloader).

1. **Check:** double-press reset → the `TECHOBOOT` drive appears. `INFO_UF2.TXT` should
   show `s140 6.1.1` and `Board-ID: nRF52840-TEcho-v1`.
2. **Flash bootloader + SoftDevice over serial DFU.** Only the `.zip` can change the
   SoftDevice; the `.uf2` files cannot.
   ```bash
   pip install --user adafruit-nrfutil
   adafruit-nrfutil --verbose dfu serial \
     --package bootloader/lilygo_techo_bootloader-0.11.0_s140_7.3.0.zip \
     -p /dev/ttyACM0 -b 115200 --singlebank --touch 1200
   ```
   (On Windows, use `-p COMx`.) After this the T-Echo has no application and stays in
   the bootloader. That is expected.
3. **Optional test:** copy `bootloader/techo_sd730_sample.uf2` onto `TECHOBOOT`. A blue
   LED blinking at 1 Hz means S140 7.3.0 is running. `INFO_UF2.TXT` now shows
   `s140 7.3.0` and `Bootloader: 0.11.0`.
4. **Flash this firmware:** double-press reset and copy
   [`firmware/release/F4LEK-TECHO-APRS_v1.0alpha_b104_T-Echo_S140v7.uf2`](firmware/release/F4LEK-TECHO-APRS_v1.0alpha_b104_T-Echo_S140v7.uf2)
   onto `TECHOBOOT` (or your own build, see below).

> ⚠️ After the upgrade, only flash firmware built for S140 7.x (`0x27000`). An old
> 6.1.1 build (`0x26000`) overwrites the end of the SoftDevice. If that happens, redo
> step 2. To go back to factory, flash LilyGO's `0.6.1_s140_6.1.1` package the same way.

## Building

```bash
cd firmware
pio run -e techo_s140v7          # T-Echo
pio run -e techo_plus_s140v7     # T-Echo Plus
```

The UF2 lands in `firmware/.pio/build/<env>/firmware.uf2`. The `techo`, `techo_plus` and
`techo_plus_s140v6` environments are for the factory S140 6.1.1 bootloader only.


## Licence

GPL-3.0 (see `LICENSE`), inherited from EA2OY APRS System. The files in `bootloader/`
keep their own licences (MIT, and Nordic's licence for the SoftDevice).
