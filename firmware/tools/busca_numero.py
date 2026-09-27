#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
busca_numero.py -- busca un numero de 32 bits en un binario, en las dos formas.

Sirve para localizar en el firmware de fabrica el codigo que usa un periferico concreto: la
direccion base de un periferico (por ejemplo SPIM0 = 0x40003000) aparece en el codigo dentro
de las instrucciones, y a partir de ahi se puede desensamblar alrededor.

Se busca como palabra de 32 bits normal y como "direccion cargada con literal" (en ARM las
constantes grandes se cargan con ldr desde una zona de datos, asi que el numero aparece
suelto en el binario).

Uso:  python busca_numero.py firmware.bin 0x40003000 [base]
"""
import re
import struct
import sys


def main():
    if len(sys.argv) < 3:
        print("uso: python busca_numero.py firmware.bin <numero> [base=0x1000]")
        return 1

    ruta = sys.argv[1]
    objetivo = int(sys.argv[2], 0)
    base = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x1000

    with open(ruta, "rb") as f:
        datos = f.read()

    print("buscando 0x%08X en %s (base 0x%X)" % (objetivo, ruta, base))
    print("=" * 70)

    patron = struct.pack("<I", objetivo)
    hallazgos = []
    for i in range(0, len(datos) - 4, 4):
        if datos[i:i + 4] == patron:
            hallazgos.append(i + base)

    if not hallazgos:
        # puede estar partido en dos mitades (instruccion movw/movt)
        alto = (objetivo >> 16) & 0xFFFF
        bajo = objetivo & 0xFFFF
        print("no aparece entero (puede venir en dos mitades movw/movt: 0x%04X / 0x%04X)"
              % (bajo, alto))
        for nombre, valor in (("mitad baja", bajo), ("mitad alta", alto)):
            p = struct.pack("<H", valor)
            n = len(list(re.finditer(re.escape(p), datos)))
            print("  %s (0x%04X) aparece %d veces" % (nombre, valor, n))
        return 0

    print("aparece %d veces:" % len(hallazgos))
    for d in hallazgos:
        print("  0x%06X" % d)

    # Se saca tambien el contexto: 32 bytes antes y 32 despues, para ver que hay alrededor.
    print()
    print("contexto de las primeras apariciones:")
    for d in hallazgos[:6]:
        off = d - base
        ini = max(0, off - 32)
        fin = min(len(datos), off + 36)
        print("  --- 0x%06X ---" % d)
        for o in range(ini, fin, 16):
            t = datos[o:o + 16]
            print("    0x%06X  %s" % (o + base, " ".join("%02X" % b for b in t)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
