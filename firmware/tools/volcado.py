#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
volcado.py -- muestra los bytes de una zona del firmware, en hexadecimal y en ASCII.

Es la herramienta de trabajo para leer a mano una tabla de constantes (por ejemplo la
secuencia de comandos de un driver de pantalla): ensena los bytes alineados de 16 en 16,
con su direccion, y al lado el texto si lo hay.

Uso:  python volcado.py firmware.bin 0x4C080 0x4C140 [direccion_base]
"""
import sys


def main():
    if len(sys.argv) < 4:
        print("uso: python volcado.py firmware.bin <desde> <hasta> [direccion_base]")
        return 1

    ruta = sys.argv[1]
    desde = int(sys.argv[2], 0)
    hasta = int(sys.argv[3], 0)
    base = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x1000

    with open(ruta, "rb") as f:
        datos = f.read()

    ini = desde - base
    fin = hasta - base
    if ini < 0:
        ini = 0
    if fin > len(datos):
        fin = len(datos)

    for off in range(ini, fin, 16):
        trozo = datos[off:off + 16]
        hexa = " ".join("%02X" % b for b in trozo)
        texto = "".join(chr(b) if 32 <= b < 127 else "." for b in trozo)
        print("0x%06X  %-47s  %s" % (off + base, hexa, texto))

    return 0


if __name__ == "__main__":
    sys.exit(main())
