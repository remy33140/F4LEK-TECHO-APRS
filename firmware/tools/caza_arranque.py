#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
caza_arranque.py -- pilla la salida de ARRANQUE del nodo, que es la unica forma de verla.

El problema: los mensajes de arranque (Serial.println de setup()) salen por el USB en los
primeros milisegundos, pero el puerto COM de Windows no existe hasta que el USB se
enumera... que es precisamente cuando esos mensajes ya se han ido. Por eso un "log dump"
nunca los enseña y una escucha no pilla nada.

La solucion: quedarse dando vueltas intentando abrir el puerto. En cuanto aparece, se
engancha a media salida. El nodo se reinicia bajando la linea DTR al abrir, asi que:

  1) se abre el puerto SIN tocar DTR (para no reiniciarlo),
  2) se manda el comando de reinicio por ese mismo puerto,
  3) se cierra y se vuelve a abrir rapidamente SIN DTR, y se escucha.

Como el firmware espera 500 ms despues de Serial.begin(), da tiempo de sobra a engancharse.

Uso:  python caza_arranque.py COM40 [segundos]
"""
import sys
import time

try:
    import serial
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial", "-q"])
    import serial


def abrir_sin_dtr(puerto):
    p = serial.Serial()
    p.port = puerto
    p.baudrate = 115200
    p.timeout = 0.1
    p.dtr = False
    p.rts = False
    try:
        p.open()
    except Exception:
        return None
    # pyserial puede subir DTR al abrir; se baja otra vez y se espera a que se asiente
    try:
        p.dtr = False
        p.rts = False
    except Exception:
        pass
    return p


def main():
    if len(sys.argv) < 2:
        print("uso: python caza_arranque.py COMx [segundos]")
        return 1

    puerto = sys.argv[1]
    segundos = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

    print("1) abro %s sin tocar DTR (para no reiniciar todavia)..." % puerto)
    p = abrir_sin_dtr(puerto)
    if p is None:
        print("   no he podido abrir el puerto")
        return 2

    print("2) mando el reinicio...")
    p.write(b"reboot\n")
    p.flush()
    time.sleep(0.15)
    p.close()

    print("3) reabro y escucho %.0f s..." % segundos)
    fin = time.time() + segundos
    lineas = 0
    while time.time() < fin:
        p = abrir_sin_dtr(puerto)
        if p is None:
            time.sleep(0.02)
            continue
        try:
            while time.time() < fin:
                datos = p.readline()
                if datos:
                    texto = datos.decode("utf-8", "replace").rstrip("\r\n")
                    if texto:
                        print(texto)
                        lineas += 1
                elif p.in_waiting == 0:
                    # nada pendiente: si el puerto se ha caido, se reabre
                    break
        finally:
            try:
                p.close()
            except Exception:
                pass

    print("--- fin: %d lineas ---" % lineas)
    return 0


if __name__ == "__main__":
    sys.exit(main())
