#!/usr/bin/env python3
# nrf52_uf2.py — post-build UF2 packaging (recipe N-06).
# Converts the app HEX to Adafruit nRF52 UF2 (family 0xADA52840).
# Bootloader limit: app must stay below 0xEA000 (~794 KB / ~3103 UF2 blocks).
# It also guards the flash-log region (0xC8000..0xE7FFF): if the application
# ever grows into it the build fails instead of corrupting stored trips.

import sys
from os.path import basename

Import("env")

FLASH_LOG_BASE = 0xC8000
BOOTLOADER_BASE = 0xEA000


def board_has_flash_log(env):
    """¿Esta placa usa la zona del registro de viaje (0xC8000..0xE7FFF)?

    El registro de viaje es nuestro y vive en esa zona en TODAS las placas, pero
    se deja desactivable por si alguna trae el mapa de memoria distinto: en ese
    caso el tope que aplica es solo el del bootloader.
    """
    try:
        flags = env.get("BUILD_FLAGS", [])
    except Exception:
        return True
    for f in flags:
        if "KACHO_NO_FLASH_LOG" in str(f):
            return False
    return True


def hex_highest_addr(path):
    """Highest byte address used by an Intel HEX file."""
    highest = 0
    base = 0
    with open(path, "r") as fh:
        for line in fh:
            line = line.strip()
            if not line.startswith(":"):
                continue
            count = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            rectype = int(line[7:9], 16)
            if rectype == 2:  # extended segment address (<< 4): what this toolchain emits
                base = int(line[9:13], 16) << 4
            elif rectype == 4:  # extended linear address (<< 16)
                base = int(line[9:13], 16) << 16
            elif rectype == 0 and count > 0:
                end = base + addr + count
                if end > highest:
                    highest = end
    return highest


def nrf52_hex_to_uf2(source, target, env):
    hex_path = target[0].get_abspath()
    uf2_path = hex_path.replace(".hex", ".uf2")

    top = hex_highest_addr(hex_path)
    con_registro = board_has_flash_log(env)
    if con_registro and top > FLASH_LOG_BASE:
        sys.stderr.write(
            "\n*** BUILD STOPPED: the application reaches 0x%X, which runs "
            "into the flash-log region (0x%X). Trim the firmware or move the "
            "log region before flashing.\n" % (top, FLASH_LOG_BASE)
        )
        env.Exit(1)
    if top > BOOTLOADER_BASE:
        sys.stderr.write("\n*** BUILD STOPPED: app over the bootloader at 0x%X\n"
                         % BOOTLOADER_BASE)
        env.Exit(1)
    if con_registro:
        print("Flash map: app ends at 0x%X, trip log 0x%X..0xE7FFF (%.1f KB free)"
              % (top, FLASH_LOG_BASE, (FLASH_LOG_BASE - top) / 1024.0))
    else:
        print("Flash map: app ends at 0x%X, bootloader at 0x%X (%.1f KB free)"
              % (top, BOOTLOADER_BASE, (BOOTLOADER_BASE - top) / 1024.0))

    env.Execute(
        env.VerboseAction(
            '"%s" "$PROJECT_DIR/bin/uf2conv.py" "%s" -c -f 0xADA52840 -o "%s"'
            % (sys.executable, hex_path, uf2_path),
            "Generating UF2 file from %s" % basename(hex_path),
        )
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", nrf52_hex_to_uf2)
