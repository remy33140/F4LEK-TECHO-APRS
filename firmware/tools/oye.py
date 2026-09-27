#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
oye.py -- escucha un puerto serie y ensena todo lo que llega, en vivo.

Sirve para ver lo que el nodo imprime AL ARRANCAR, que es justo lo que no se guarda en el
registro persistente: los mensajes de arranque salen por el puerto USB, pero el registro
solo empieza a escribir despues, asi que mirando un "log dump" NUNCA se ven. Hay que
pillarlos en el momento.

Baja la linea DTR al abrir el puerto: en las placas nRF52 eso REINICIA el nodo, y asi la
captura empieza desde el primer mensaje del arranque.

Uso:  python oye.py COM40 [segundos]
"""
import sys
import time

try:
    import serial
except ImportError:
    print("Falta pyserial. Instalando...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial", "-q"])
    import serial


def main():
    if len(sys.argv) < 2:
        print("uso: python oye.py COMx [segundos]")
        return 1

    puerto = sys.argv[1]
    segundos = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
    # Por defecto NO se toca DTR. Bajar DTR reinicia la placa, y para ver la salida de
    # arranque hace falta ese reinicio; pero si lo que queremos es saber si el nodo esta
    # vivo, hay que entrar SIN reiniciarlo (si no, lo mata uno mismo justo al mirar).
    reiniciar = "-r" in sys.argv

    print("escuchando %s durante %.0f s%s..." % (
        puerto, segundos, " (reiniciando el nodo)" if reiniciar else " (sin reiniciar)"))
    with serial.Serial(puerto, 115200, timeout=0.2) as p:
        if not reiniciar:
            p.dtr = False
            p.rts = False
        fin = time.time() + segundos
        lineas = 0
        while time.time() < fin:
            datos = p.readline()
            if datos:
                texto = datos.decode("utf-8", "replace").rstrip("\r\n")
                if texto:
                    print(texto)
                    lineas += 1
        print("--- fin de la escucha: %d lineas ---" % lineas)
    return 0


if __name__ == "__main__":
    sys.exit(main())
