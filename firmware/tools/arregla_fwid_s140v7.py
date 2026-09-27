# -*- coding: utf-8 -*-
"""Cambia el fwid que esperan los entornos S140 v7: 0x0101 -> 0x0123.

POR QUE: `0x0101` no es el fwid de ninguna S140. El de la S140 v7 es `0x0123` (es el mismo
que declara el framework de Adafruit para sus placas con S140 v7 en `boards.txt` y el que
pide el paquete DFU en `--sd-req`). Comprobado leyendo los hex oficiales de los SoftDevice:
en la direccion buena (0x300C) la S140 7.3.0 dice 0x0123 y la S140 6.1.1 dice 0x00B6.

Se hace por bloques de `[env:...]` para no tocar el 0x00B6 de las Faketec. Deja copia .bak
y NO escribe si el texto no es exactamente el esperado.
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INI = os.path.join(RAIZ, "platformio.ini")

VIEJO = "-D SD_ESPERADO_FWID=0x0101"
NUEVO = "-D SD_ESPERADO_FWID=0x0123"

with open(INI, "r", encoding="utf-8") as f:
    texto = f.read()

# Trocea por entornos: cada bloque empieza en [env:...]
trozos = re.split(r"(?m)^(?=\[env:)", texto)
tocados = 0
salida = []
for t in trozos:
    cabecera = t.split("\n", 1)[0].strip()
    if cabecera in ("[env:techo_s140v7]", "[env:techo_plus_s140v7]") and VIEJO in t:
        n = t.count(VIEJO)
        t = t.replace(VIEJO, NUEVO)
        tocados += n
        print("  %-26s -> %d cambio(s)" % (cabecera, n))
    salida.append(t)

if tocados != 2:
    print("FALLO: esperaba 2 cambios (uno por entorno s140v7) y he hecho %d. NO escribo nada." % tocados)
    sys.exit(1)

nuevo_texto = "".join(salida)
if VIEJO in nuevo_texto:
    print("FALLO: queda algun 0x0101 suelto. NO escribo nada.")
    sys.exit(1)

with open(INI + ".bak", "w", encoding="utf-8", newline="") as f:
    f.write(texto)
with open(INI, "w", encoding="utf-8", newline="") as f:
    f.write(nuevo_texto)
print("  escrito %s (copia en platformio.ini.bak)" % INI)
