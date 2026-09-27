#!/usr/bin/env python3
"""Decodifica una trama APRS (formato TNC2) con aprslib y muestra lo que entiende.

Uso:
  python tools/check_compressed.py "EA2OY-7>APLRG1:@121018z/8nzJN(^_>A![/A=001418 47.4C"

Sirve para comprobar, con una implementacion INDEPENDIENTE de la nuestra, si un
paquete (sobre todo de posicion comprimida) se esta mandando bien o no.
aprslib se instala aparte:  pip install --target .pio/pylibs aprslib
License: GPL-3.0
"""

import json
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".pio", "pylibs"))

try:
    import aprslib
except ImportError:
    print("Falta aprslib: pip install --target .pio/pylibs aprslib")
    raise SystemExit(1)

CAMPOS = ("latitude", "longitude", "course", "speed", "altitude", "symbol",
          "symbol_table", "format", "comment", "raw")

for linea in sys.argv[1:]:
    print("=" * 70)
    print("TNC2 :", linea)
    try:
        d = aprslib.parse(linea)
    except Exception as e:  # noqa: BLE001
        print("ERROR:", e)
        continue
    for k in CAMPOS:
        if k in d:
            print(f"  {k:12} = {d[k]}")
    print("  (todo)      =", json.dumps({k: v for k, v in d.items()
                                         if k in CAMPOS}, default=str))
