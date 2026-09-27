# T-Echo bootloader 0.11.0 + SoftDevice S140 7.3.0

Unmodified release files from
[ViezeVingertjes/lilygo-techo-bootloader v1.0.0](https://github.com/ViezeVingertjes/lilygo-techo-bootloader/releases/tag/v1.0.0),
kept here so the T-Echo can be flashed without depending on an external download.
The flashing procedure is in the [main README](../README.md#flashing-the-bootloader-s140-730).

| File | Use | SHA-256 |
|---|---|---|
| `lilygo_techo_bootloader-0.11.0_s140_7.3.0.zip` | **The upgrade package** (serial DFU, bootloader + SoftDevice) | `a133d2db…d447a9` |
| `techo_sd730_sample.uf2` | Optional test app: blue LED at 1 Hz = SoftDevice 7.3.0 OK | `ac9792fe…e488541` |
| `lilygo_techo_bootloader-0.11.0_s140_7.3.0.hex` | Recovery only, over SWD (J-Link / DAPLink) | `1456587a…6ee1c3` |
| `update-lilygo_techo_bootloader-0.11.0_nosd.uf2` | Bootloader-only update, **does not change the SoftDevice** | `b6796d0a…1fcfa10` |

Checksums match the digests GitHub publishes for the release assets.

**Licences:** the bootloader is MIT (`LICENSE-bootloader-MIT.txt`). The Nordic S140
SoftDevice inside the `.zip` and `.hex` is redistributed under Nordic's own licence
(`s140_nrf52_7.3.0_license-agreement.txt`) and may only be used on Nordic silicon.
