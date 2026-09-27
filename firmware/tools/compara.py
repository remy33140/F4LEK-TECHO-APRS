# -*- coding: utf-8 -*-
"""Compara dos ficheros de codigo y ensena SOLO las lineas que cambian (con contexto).

Para que: las dos versiones del core de Adafruit (1.10601.0, la de Meshtastic, y 1.10700.0, la
nuestra) tienen ficheros distintos. Saber QUE cambio es lo que puede explicar que una arranque
el Bluetooth y la otra no.

Uso:  python compara.py <ficheroA> <ficheroB> [lineas_de_contexto]
"""
import difflib
import sys

A, B = sys.argv[1], sys.argv[2]
CTX = int(sys.argv[3]) if len(sys.argv) > 3 else 2


def lee(r):
    try:
        return open(r, "r", encoding="utf-8", errors="replace").read().splitlines()
    except FileNotFoundError:
        return None


la, lb = lee(A), lee(B)
if la is None or lb is None:
    print("falta alguno de los dos ficheros")
    sys.exit(1)

print("A: %s (%d lineas)" % (A, len(la)))
print("B: %s (%d lineas)" % (B, len(lb)))
print()
dif = list(difflib.unified_diff(la, lb, fromfile="1.10601.0", tofile="1.10700.0",
                                lineterm="", n=CTX))
if not dif:
    print("(no hay diferencias de contenido: seran finales de linea o metadatos)")
else:
    for l in dif:
        print(l)
