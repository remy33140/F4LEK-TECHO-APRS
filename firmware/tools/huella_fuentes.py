#!/usr/bin/env python3
# huella_fuentes.py — LA HUELLA DEL CODIGO, calculada EXACTAMENTE igual que en
# `extra_scripts\sube_buildnum.py` (`_fuentes_huella()`).
#
# ============================================================================
#  PARA QUE EXISTE ESTE FICHERO (2026-09-16, noche)
#
#  El contador de compilacion gasta un numero cuando la huella del codigo cambia, y
#  NO lo gasta cuando la marca `.pio\.buildnum_fuentes` ya lleva esa misma huella.
#
#  `compila_a_medida.ps1` compila una COPIA del arbol con otros valores de fabrica y
#  necesita que el binario salga con el MISMO numero que el publicado (b13), asi que
#  le pone el numero a mano... y hasta hoy BORRABA la marca, con el comentario "para
#  que no reste nada". Justo al reves: sin marca, el contador da el codigo por nuevo y
#  gasta un numero -> el binario a medida salia con un numero distinto del publicado.
#  Paso de verdad en una compilacion a medida, y el guion se paro avisando.
#
#  La marca tiene que llevar LA HUELLA REAL de las fuentes YA PARCHEADAS, y para eso
#  hay que calcularla con el MISMO algoritmo que el contador. Se podria reescribir en
#  PowerShell, pero cualquier detalle que se escape (el orden de `os.walk`, los `.pyc`
#  que se salta, la linea del numero neutralizada en `platformio.ini`) daria una
#  huella distinta y volveriamos al mismo fallo, esta vez en silencio. Por eso esto es
#  Python: es el mismo lenguaje, y el codigo de abajo esta CALCADO.
#
#  ★ SI ALGUN DIA CAMBIA `_fuentes_huella()` EN `sube_buildnum.py`, HAY QUE CAMBIAR
#    ESTO IGUAL. Se comprueba en un segundo: la huella de un arbol recien compilado
#    tiene que coincidir con lo que dice su `.pio\.buildnum_fuentes`.
#
#  Uso:
#    python huella_fuentes.py <carpeta del proyecto>              (imprime la huella)
#    python huella_fuentes.py <carpeta del proyecto> --escribir   (la apunta en .pio)
# ============================================================================

import hashlib
import os
import re
import sys

PATRON_INI = re.compile(r'(-DAPP_BUILD_NUM=\\?"b)(\d+)(\\?")')


def huella(d):
    """Huella del codigo que se va a compilar (misma que la del contador)."""
    h = hashlib.sha256()
    for sub in ("src", "variants", "boards", "extra_scripts"):
        base = os.path.join(d, sub)
        for raiz, dirs, ficheros in os.walk(base):
            dirs.sort()
            for f in sorted(ficheros):
                if f.endswith(".pyc"):
                    continue
                p = os.path.join(raiz, f)
                h.update(os.path.relpath(p, d).replace("\\", "/").encode("utf-8"))
                try:
                    with open(p, "rb") as fh:
                        h.update(fh.read())
                except OSError:
                    pass
    try:
        with open(os.path.join(d, "platformio.ini"), "r", encoding="utf-8") as fh:
            ini = PATRON_INI.sub(lambda m: m.group(1) + "N" + m.group(3), fh.read())
        h.update(ini.encode("utf-8"))
    except OSError:
        pass
    return h.hexdigest()


def main():
    if len(sys.argv) < 2:
        print("uso: huella_fuentes.py <carpeta del proyecto> [--escribir]")
        return 2
    d = sys.argv[1]
    if not os.path.isdir(d):
        print("no existe la carpeta: %s" % d)
        return 1
    valor = huella(d)
    if len(sys.argv) > 2 and sys.argv[2] == "--escribir":
        destino = os.path.join(d, ".pio")
        os.makedirs(destino, exist_ok=True)
        with open(os.path.join(destino, ".buildnum_fuentes"), "w", encoding="utf-8") as fh:
            fh.write(valor)
        print("marca apuntada: %s" % valor)
    else:
        print(valor)
    return 0


if __name__ == "__main__":
    sys.exit(main())
