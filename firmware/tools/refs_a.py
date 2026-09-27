#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
refs_a.py -- busca en el firmware 32 bits que sean una direccion concreta.

En ARM Cortex-M las direcciones de las tablas y los datos van "literales" dentro del codigo:
la instruccion `ldr r0, [pc, #N]` no lleva la direccion dentro, sino que carga un numero que
esta justo despues, en una zona llamada "literal pool". Por eso, para saber QUE CODIGO usa
una tabla, se busca el numero de la direccion de esa tabla en el binario: cada aparicion es
(un literal pool de) una instruccion que la usa.

El desensamblador luego dice alrededor de que funcion esta. Esta herramienta solo localiza
las apariciones y ensena un poco de contexto.

Uso:  python refs_a.py firmware.bin 0x4C08C 0x4C0EE [direccion_base]
"""
import struct
import sys


def main():
    if len(sys.argv) < 3:
        print("uso: python refs_a.py firmware.bin <dir1> [dir2 ...] [base=0x1000]")
        return 1

    ruta = sys.argv[1]
    argumentos = sys.argv[2:]
    base = 0x1000
    if argumentos and argumentos[-1].lower().startswith("base="):
        base = int(argumentos[-1].split("=", 1)[1], 0)
        argumentos = argumentos[:-1]
    direcciones = [int(a, 0) for a in argumentos]

    with open(ruta, "rb") as f:
        datos = f.read()

    for objetivo in direcciones:
        print("=" * 78)
        print("quien usa la direccion 0x%X" % objetivo)
        print("=" * 78)
        patron = struct.pack("<I", objetivo)          # como numero de 32 bits
        encontrados = 0
        for i in range(0, len(datos) - 4, 4):
            if datos[i:i + 4] == patron:
                direccion = i + base
                print("  el numero esta en 0x%06X" % direccion)
                encontrados += 1
        if encontrados == 0:
            print("  (no aparece como numero; prueba a buscar sus bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
