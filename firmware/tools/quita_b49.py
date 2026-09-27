# -*- coding: utf-8 -*-
"""Quita el bloque del "arranque sucio del cargador" (b49) de `src/ble_kiss.cpp`.

POR QUE: ese bloque metia un `NVIC_SystemReset()` al arrancar cuando la placa decia que el
arranque anterior no fue por software. En la placa eso se convirtio en un **bucle de
reinicios**: la placa reinicia, no marca el motivo como "por software", y vuelve a reiniciar.
El sintoma fue claro: el nodo arrancaba y se reiniciaba antes de dibujar la pantalla, con el
Bluetooth apagado y sin tocar el SoftDevice. Es un fallo de una hipotesis que no era.

Uso:  python quita_b49.py [--aplicar]
Sin `--aplicar` solo dice que lineas quitaria.
"""
import sys

RUTA = r"src\ble_kiss.cpp"
APLICAR = "--aplicar" in sys.argv

lineas = open(RUTA, encoding="utf-8").read().split("\n")

ini = None
for i, l in enumerate(lineas):
    if "ARRANQUE" in l and "SUCIO" in l:
        ini = i - 1  # la linea del `/*` esta justo antes
        break
if ini is None:
    print("FALLO: no encuentro el bloque (¿ya se quito?)")
    sys.exit(1)

fin = None
for j in range(ini, min(ini + 60, len(lineas))):
    if "NVIC_SystemReset();" in lineas[j]:
        fin = j + 2  # el `}` del else y el `return;`
        break
if fin is None:
    print("FALLO: no encuentro el final del bloque")
    sys.exit(1)

print("bloque encontrado: lineas %d..%d" % (ini + 1, fin + 1))
print("  primera: %s" % lineas[ini].strip()[:70])
print("  ultima : %s" % lineas[fin - 1].strip()[:70])

if not APLICAR:
    print("\n(sin --aplicar: no se toca nada)")
    sys.exit(0)

del lineas[ini:fin]
open(RUTA, "w", encoding="utf-8", newline="").write("\n".join(lineas))
print("\nQUITADO. Quedan %d lineas." % len(lineas))
