# -*- coding: utf-8 -*-
"""Manda una orden al nodo por el cable y recoge lo que contesta.

Para que: el nodo (firmware Kacho) habla por lineas. Este guion abre el puerto, manda la orden
y lee la respuesta. Se usa cuando NO se puede tirar del movil (por ejemplo para el arranque del
Bluetooth, que se cae y deja el puerto loco).

Uso:  python charla_nodo.py COM40 "status" [segundos]
      python charla_nodo.py COM40 "ble" 6
"""
import sys
import time

import serial

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

PUERTO = sys.argv[1] if len(sys.argv) > 1 else "COM40"
ORDEN = sys.argv[2] if len(sys.argv) > 2 else "status"
SEGUNDOS = float(sys.argv[3]) if len(sys.argv) > 3 else 5.0

print("abriendo %s a 115200 ..." % PUERTO)
with serial.Serial(PUERTO, 115200, timeout=0.2) as p:
    time.sleep(0.4)
    p.reset_input_buffer()
    print(">>> %s" % ORDEN)
    p.write((ORDEN + "\n").encode("ascii"))
    p.flush()
    t0 = time.time()
    while time.time() - t0 < SEGUNDOS:
        datos = p.read(8192)
        if datos:
            try:
                sys.stdout.buffer.write(datos)
                sys.stdout.buffer.flush()
            except Exception:
                pass
print("\n=== fin ===")
