# -*- coding: utf-8 -*-
"""LA PRUEBA COMPLETA DEL ARRANQUE DEL BLUETOOTH, por el cable.

QUE HACE, en orden:
  1. espera a que el nodo arranque (pregunta con 'ver');
  2. apunta el estado de partida ('ble');
  3. ENCIENDE el Bluetooth ('set bleEnabled 1') -> aqui es donde el nodo se cae, si se cae;
  4. espera a que el nodo vuelva (el firmware nuevo se recupera solo y lo deja apagado);
  5. lee el informe ('ble'), que dice EN QUE PASO se murio y como estaban las interrupciones;
  6. saca del registro de viaje las lineas del arranque del Bluetooth.

Uso:  python prueba_bt.py COM40
"""
import re
import sys
import time

import serial

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

PUERTO = sys.argv[1] if len(sys.argv) > 1 else "COM40"


def lee(p, segundos):
    """Lee lo que haya durante N segundos y lo devuelve como texto."""
    t0 = time.time()
    trozos = []
    while time.time() - t0 < segundos:
        try:
            datos = p.read(4096)
        except Exception as e:
            trozos.append(("\n[puerto cortado: %s]\n" % e).encode())
            break
        if datos:
            trozos.append(datos)
    return b"".join(trozos).decode("utf-8", errors="replace")


def manda(p, orden, espera=4.0):
    p.reset_input_buffer()
    p.write((orden + "\n").encode("ascii"))
    p.flush()
    return lee(p, espera)


def esperaNodo(segundos_max=45):
    """Abre el puerto y espera a que el nodo conteste a 'ver'."""
    t0 = time.time()
    while time.time() - t0 < segundos_max:
        try:
            with serial.Serial(PUERTO, 115200, timeout=0.2) as p:
                time.sleep(0.5)
                r = manda(p, "ver", 2.0)
                if r.strip():
                    return r.strip()
        except Exception:
            pass
        time.sleep(1.0)
    return None


print("=== 1) esperando a que el nodo arranque ===")
quien = esperaNodo()
print("   el nodo dice: %s" % (quien if quien else "(no contesta)"))

with serial.Serial(PUERTO, 115200, timeout=0.2) as p:
    time.sleep(0.4)

    print("\n=== 2) estado de partida ===")
    print(manda(p, "ble", 5.0).strip())

    print("\n=== 3) ENCIENDO EL BLUETOOTH (set bleEnabled 1) ===")
    salida = manda(p, "set bleEnabled 1", 6.0)
    print(salida.strip())
    if "OK bleEnabled=1" in salida:
        print("   -> el nodo acepto el encendido: a partir de aqui puede caerse")

    print("\n=== 4) esperando a que el nodo vuelva (se recupera solo) ===")
    time.sleep(8)
    try:
        p.reset_input_buffer()
    except Exception:
        pass
    # Si el puerto se cayo con el reinicio, se reabre.
    try:
        p.close()
    except Exception:
        pass
    for intento in range(1, 13):
        try:
            p = serial.Serial(PUERTO, 115200, timeout=0.2)
            time.sleep(0.5)
            r = manda(p, "ver", 2.5)
            if r.strip():
                print("   el nodo ha vuelto (intento %d): %s" % (intento, r.strip()))
                break
        except Exception as e:
            print("   intento %d: %s" % (intento, e))
        time.sleep(3)

    print("\n=== 5) EL INFORME: en que paso se murio ===")
    print(manda(p, "ble", 6.0).strip())

    print("\n=== 6) lo que quedo en el registro de viaje ===")
    dump = manda(p, "log dump", 14.0)
    for linea in dump.splitlines():
        if re.search(r"BLE|MURIO|ISER|STATUS|GPREGRET|arranque", linea, re.I):
            print("   " + linea.strip())

print("\n=== fin de la prueba ===")
