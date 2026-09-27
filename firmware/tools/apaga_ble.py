# -*- coding: utf-8 -*-
"""APAGA EL BLUETOOTH EN LA CONFIGURACION GUARDADA DEL NODO (por el cable, sin grabar nada).

POR QUE: la configuracion vive en la flash interna y **sobrevive a las grabaciones**. Si en
algun momento se guardo `bleEnabled=true` (por ejemplo probando el Bluetooth), ese valor sigue
ahi aunque se grabe otro firmware. Y como el arranque del Bluetooth se cae en esta placa, el
nodo entra en bucle: no le da tiempo ni a atender el menu para apagarlo.

QUE HACE, en orden y sin tocar la flash hasta el final:
  1. PIDE la configuracion ({"cmd":"get"}) y ensena como esta `bleEnabled` y el indicativo;
  2. si esta encendido, manda {"cmd":"set","config":{"bleEnabled":false}} — el protocolo la
     valida y la guarda de forma ATOMICA (storeSave);
  3. VUELVE A PEDIRLA para comprobar que quedo apagada de verdad (no se fia de la respuesta).

Uso:  python apaga_ble.py COM40
"""
import json
import sys
import time

import serial

PUERTO = sys.argv[1] if len(sys.argv) > 1 else "COM40"


def charla(p, orden, segundos=4.0):
    """Manda una orden JSON y devuelve las lineas que contesta el nodo."""
    try:
        p.reset_input_buffer()
    except Exception:
        pass
    p.write((orden + "\n").encode("ascii"))
    p.flush()
    t0 = time.time()
    salida = []
    while time.time() - t0 < segundos:
        d = p.read(4096)
        if d:
            salida.append(d.decode("utf-8", errors="replace"))
    return "".join(salida)


def configDe(texto):
    """Saca el objeto de configuracion de la respuesta (una linea JSON)."""
    for linea in texto.splitlines():
        linea = linea.strip()
        if '"config"' in linea:
            try:
                return json.loads(linea)
            except Exception:
                pass
    return None


print("abriendo %s a 115200 ..." % PUERTO)
with serial.Serial(PUERTO, 115200, timeout=0.2) as p:
    time.sleep(0.5)

    print("\n=== 1) la configuracion que tiene guardada AHORA ===")
    antes = configDe(charla(p, '{"cmd":"get"}', 5.0))
    if not antes:
        print("   el nodo no ha contestado con la configuracion. ¿esta en el COM correcto?")
        sys.exit(1)
    cfg = antes.get("config", {})
    print("   indicativo  : %s" % cfg.get("callsign"))
    print("   bleEnabled  : %s   <-- esto es lo que hay que apagar" % cfg.get("bleEnabled"))
    print("   blePin      : %s" % ("(hay)" if cfg.get("blePin") else "(no)"))

    if cfg.get("bleEnabled") is False:
        print("\n=== 2) ya estaba apagado: no se toca la flash ===")
        sys.exit(0)

    print("\n=== 2) lo APAGO y lo guardo ===")
    resp = charla(p, '{"cmd":"set","config":{"bleEnabled":false}}', 6.0)
    print("   respuesta: %s" % resp.strip().splitlines()[-1][:150] if resp.strip() else "   (sin respuesta)")

    print("\n=== 3) COMPROBACION: se lo vuelvo a pedir ===")
    despues = configDe(charla(p, '{"cmd":"get"}', 5.0))
    if not despues:
        print("   no ha contestado a la segunda pregunta")
        sys.exit(1)
    cfg2 = despues.get("config", {})
    print("   bleEnabled  : %s" % cfg2.get("bleEnabled"))
    if cfg2.get("bleEnabled") is False:
        print("\n   *** APAGADO Y GUARDADO. El nodo ya no intentara arrancar el Bluetooth. ***")
    else:
        print("\n   *** NO se ha quedado apagado: hay que mirarlo. ***")
        sys.exit(1)
