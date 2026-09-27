# -*- coding: utf-8 -*-
"""Mira QUE LLEVA DENTRO un volcado UF2 (de quien es el firmware) sin grabarlo.

POR QUE: el 2026-09-17 se dio por hecho que `techorepofirm/T-Echo-main/uf2/CURRENT.UF2` era
"el firmware de fabrica de LilyGO" solo porque su SoftDevice era la v6.1.1, y resulto que
dentro llevaba **Meshtastic**. Este guion lo comprueba leyendo los TEXTOS del binario, que es
lo que delata de quien es (nombres de ficheros de compilacion, cadenas de la aplicacion).

Uso:  python mira_volcado.py <ficha.uf2> [<ficha2.uf2> ...]
"""
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uf2 import lee_uf2, regiones  # noqa: E402

PISTAS = ("meshtastic", "kacho", "lilygo", "softrf", "adafruit", "aprs",
          "init bluefruit", "t-echo", "ble name", "e-ink", "inkhud")


def mira(ruta):
    print("=" * 78)
    print("FICHERO : %s" % ruta)
    print("TAMANO  : %d B   SHA-256 %s" % (os.path.getsize(ruta),
                                           hashlib.sha256(open(ruta, "rb").read()).hexdigest()))
    try:
        bloques = lee_uf2(ruta)
        regs = regiones(bloques)
    except Exception as e:
        print("  ERROR leyendo como UF2: %s" % e)
        return
    fams = sorted({b.familia for b in bloques})
    print("FAMILIA : %s" % ", ".join("0x%08X" % f for f in fams))
    for a, v in regs:
        print("   escribe 0x%05X - 0x%05X  (%d B)" % (a, a + len(v) - 1, len(v)))

    # La ficha del SoftDevice (offsets de nrf_sdm.h, con su MBR)
    d = {}
    for a, v in regs:
        for i, b in enumerate(v):
            d[a + i] = b

    def u32(a):
        return int.from_bytes(bytes(d.get(a + i, 0xFF) for i in range(4)), "little")

    def u16(a):
        return int.from_bytes(bytes(d.get(a + i, 0xFF) for i in range(2)), "little")

    print("SOFTDEV : ficha en 0x3000 -> fwid=0x%04X  id=0x%08X  limite=0x%X"
          % (u16(0x300C), u32(0x3010), u32(0x3008)))
    # La tabla de vectores: dice DONDE empieza la aplicacion
    for base in (0x26000, 0x27000):
        sp, pc = u32(base), u32(base + 4)
        vale = 0x20000000 <= sp <= 0x20040000 and 0x1000 <= pc < 0x100000
        print("VECTORES: 0x%05X -> SP=0x%08X reset=0x%08X %s"
              % (base, sp, pc, "(tabla de vectores: la app empieza AQUI)" if vale else ""))

    # Los TEXTOS: lo que delata de quien es el firmware
    texto = bytearray()
    for a, v in regs:
        texto.extend(v)
    t = bytes(texto).decode("latin-1")
    cads = sorted(set(re.findall(r"[ -~]{8,}", t)))
    print("TEXTOS que delatan de quien es:")
    for p in PISTAS:
        golpe = [c for c in cads if p in c.lower()]
        print("   %-16s %s" % (p, ("SI -> ej: " + golpe[0].strip()[:60]) if golpe else "no"))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for r in sys.argv[1:]:
        mira(r)
