#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
saca_log.py -- extrae las cadenas de los mensajes de depuracion del firmware.

cfr34k (y el firmware que traia la placa) usa el sistema NRF_LOG del SDK de Nordic: cada
mensaje de depuracion viaja dentro del firmware con su cadena de formato y su fichero de
origen. Eso es ORO para la ingenieria inversa, porque en esos mensajes el propio autor
escribio que esta haciendo: "[EPD] ...", "initializing SPI", numeros de pin, etc.

Esta herramienta saca las cadenas que llevan delante el nombre de un modulo entre corchetes
o que parecen mensajes, y ademas las rutas de fichero fuente ("./src/loquesea.c").

Uso:  python saca_log.py firmware.bin [filtro]
"""
import re
import sys


def main():
    if len(sys.argv) < 2:
        print("uso: python saca_log.py firmware.bin [filtro]")
        return 1

    ruta = sys.argv[1]
    filtro = sys.argv[2] if len(sys.argv) > 2 else None

    with open(ruta, "rb") as f:
        datos = f.read()

    print("=" * 78)
    print("RUTAS DE FICHERO FUENTE (dicen que modulos se compilaron)")
    print("=" * 78)
    rutas = set()
    for m in re.finditer(rb"[.A-Za-z0-9_/\\-]{0,20}\.c\b", datos):
        s = m.group().decode("ascii", "replace")
        if "/" in s or "\\" in s:
            rutas.add(s)
    for s in sorted(rutas):
        print("  %s" % s)

    print()
    print("=" * 78)
    print("MENSAJES DE DEPURACION (los que empiezan por <algo>: o [algo])")
    print("=" * 78)
    vistos = set()
    for m in re.finditer(rb"[\x20-\x7E]{5,160}", datos):
        s = m.group().decode("ascii", "replace")
        if filtro and filtro.lower() not in s.lower():
            continue
        # los mensajes del NRF_LOG suelen acabar en "." o ":" y empiezan en minuscula o
        # llevan un nombre de modulo. Se descartan los que son claramente rutas o codigo.
        if s in vistos:
            continue
        if re.match(r"^[<\[\(]", s) or re.match(r"^[a-z][a-z0-9_ ]{4,}[.:]$", s) or \
           re.search(r"(init|Init|start|Start|send|Send|wait|Wait|error|Error|fail)", s):
            if ".c" in s or "/" in s:
                continue
            vistos.add(s)
            print("  %s" % s)
            if len(vistos) > 300:
                print("  ... (cortado)")
                break

    return 0


if __name__ == "__main__":
    sys.exit(main())
