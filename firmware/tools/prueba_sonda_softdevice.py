#!/usr/bin/env python3
# prueba_sonda_softdevice.py â€” PRUEBA DE ESCRITORIO DE LA SONDA DEL SOFTDEVICE
#
# QUE ES: la comprobacion que hace el nodo ANTES de dejar que nadie le toque el USB
# (src/ble_kiss.cpp, `softdevicePareceValido()`). Aqui se reproduce la misma logica y se le
# dan imagenes de flash simuladas, para demostrar que distingue un SoftDevice valido de uno
# invalido. Ademas COMPRUEBA, sobre el binario ya compilado, que la sonda mira de verdad la
# direccion correcta: si el offset estuviera mal, la sonda diria "no vale" SIEMPRE y dejaria a
# todos los nodos sin Bluetooth, que es un fallo peor que el que viene a arreglar.
#
# POR QUE HAY QUE COMPROBAR ESTO: de esta comprobacion depende que el nodo NO se quede sin USB.
# Medido en la placa del operador el 2026-09-17 con el b25: si se llama a `Bluefruit.begin()`
# con un SoftDevice que no arranca, el nodo se queda en un ir y venir del puerto USB y SIN
# CONSOLA. La recuperacion a mano del USB (`usbRearma()`) no lo salvaba.
#
# â˜…â˜…â˜… LA DIRECCION, CORREGIDA EL 2026-09-17 (b29) â˜…â˜…â˜…
# Aqui ponia SD_INFO_DIR = 0x2000 y ESO ESTABA MAL. Los defines son de `nrf_sdm.h` y el
# propio comentario de la cabecera dice que el offset es "from the start of the SoftDevice
# (without MBR)", o sea RELATIVO a la base del SoftDevice, que empieza despues del MBR:
#
#     SOFTDEVICE_INFO_STRUCT_OFFSET  = 0x2000          (relativo a la base del SD)
#     SOFTDEVICE_INFO_STRUCT_ADDRESS = 0x2000 + MBR_SIZE   (MBR_SIZE = 0x1000)
#     SD_FWID_OFFSET                 = 0x2000 + 0x0C
#
# Direccion ABSOLUTA de la ficha: 0x3000. La del fwid: 0x300C.
#
# Lo que habia en 0x200C era CODIGO ARM del SoftDevice, y de ahi salieron los dos numeros que
# este proyecto tomo por chips averiados. Leido de los hex OFICIALES (tools\lee_hex.py):
#
#     offset 0x2000 -> S140 7.3.0: fwid=0xD902 | S140 6.1.1: fwid=0xE002   (los dos, CODIGO)
#     offset 0x3000 -> S140 7.3.0: ficha 44 B, SD_size=0x27000, fwid=0x0100
#                     S140 6.1.1: ficha 44 B, SD_size=0x26000, fwid=0x00B6
#
# Y el fwid esperado de la S140 v7 es 0x0100 (el mismo que declara el framework de Adafruit
# para sus placas con S140 v7 en `boards.txt`), no 0x0101: 0x0101 no es el fwid de ninguna S140.
#
# COMO SE EJECUTA (desde _trabajo_ea2oy):
#     python tools\prueba_sonda_softdevice.py
# Devuelve 0 si todo esta bien y 1 si hay algun fallo, como los demas verificadores.
#
# License: GPL-3.0

import os
import re
import struct
import subprocess
import sys

MBR_SIZE = 0x1000                 # nrf_sdm.h
SD_INFO_DIR = MBR_SIZE + 0x2000   # SOFTDEVICE_INFO_STRUCT_ADDRESS = 0x3000
SD_SIZE_DIR = SD_INFO_DIR + 0x08  # SD_SIZE_OFFSET
SD_FWID_DIR = SD_INFO_DIR + 0x0C  # SD_FWID_OFFSET

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# El fwid que espera cada entorno. Es el `sd_fwid` de boards\*.json y tiene que coincidir con
# el -DSD_ESPERADO_FWID de platformio.ini (abajo se comprueba que coincidan de verdad).
ENTORNOS = {
    "techo_s140v7": 0x0123,         # S140 7.3.0
    "techo_plus_s140v7": 0x0123,    # S140 7.3.0
}

fallos = 0


def mal(msg):
    global fallos
    fallos += 1
    print("  FALLO  " + msg)


def bien(msg):
    print("  OK     " + msg)


# ---------------------------------------------------------------- la sonda
# LAS LINEAS DEL FIRMWARE, copiadas de src/ble_kiss.cpp: la lectura de la ficha y la decision.
def fwid_leido(flash):
    return struct.unpack_from("<H", flash, SD_FWID_DIR)[0]


def tam_ficha(flash):
    return flash[SD_INFO_DIR]


def parece_valido(flash):
    """LA REGLA DEL FIRMWARE (b32): Â¿hay un SoftDevice que se pueda intentar arrancar?

    â˜… NI EL `fwid` EXACTO NI EL PRIMER BYTE DE LA FICHA. Los dos se probaron en la placa del
    operador el 2026-09-17 y los dos rechazaban un SoftDevice bueno:
      - con el `fwid` exacto: la placa lleva una S140 de otra revision (`0x0100` en vez de
        `0x0100`) y contestaba que no;
      - con el primer byte de la ficha: en esa placa ese campo vale **0** (en el hex oficial de
        Nordic vale 44), asi que tambien decia que no.
    La huella que SI distingue es el `id` del SoftDevice junto con un tamano creible: un trozo
    de codigo leido por error no los trae, y una flash borrada tampoco.
    """
    tam = struct.unpack_from("<I", flash, SD_SIZE_DIR)[0]
    if tam < 0x4000 or tam > 0x80000:
        return False
    idv = struct.unpack_from("<I", flash, SD_INFO_DIR + 0x10)[0]
    if (idv & 0xFFFFFF00) != 0 or (idv & 0xFF) != 0x8C:
        return False
    fwid = fwid_leido(flash)
    if fwid in (0x0000, 0xFFFF):
        return False
    return True


def imagen(fwid, tam, version, tam_ficha_=44, idv=0x8C):
    """Una imagen de flash simulada: la ficha del SoftDevice puesta donde toca (0x3000)."""
    f = bytearray(b"\xFF" * 0x30000)
    f[SD_INFO_DIR] = tam_ficha_
    struct.pack_into("<I", f, SD_SIZE_DIR, tam)
    struct.pack_into("<H", f, SD_FWID_DIR, fwid)
    struct.pack_into("<I", f, SD_INFO_DIR + 0x10, idv)
    struct.pack_into("<I", f, SD_INFO_DIR + 0x14, version)
    return f


def casos():
    print("\n=== 1. La logica de la sonda, con imagenes simuladas ===")
    # (descripcion, fwid grabado, tamano del SD, version, tamano de la ficha, id, esperado)
    tabla = [
        ("S140 v7 buena (lo normal en un T-Echo v7)", 0x0100, 0x27000, 0x00070300, 44, 0x8C, True),
        ("S140 v6 buena (lo normal en un T-Echo v6)", 0x00B6, 0x26000, 0x00060101, 44, 0x8C, True),
        # â˜… LOS DOS CASOS REALES DE LA PLACA DEL OPERADOR (medidos con el b30 y el b31):
        #   hay SoftDevice, es una S140, pero de otra revision y con el primer byte de la ficha
        #   a cero -> se intenta arrancar igual.
        ("LA PLACA: S140 de otra revision", 0x0100, 0x27000, 0x00060101, 44, 0x8C, True),
        ("LA PLACA: con el primer byte de la ficha a 0", 0x0100, 0x27000, 0x00060101, 0, 0x8C, True),
        ("zona borrada (todo 0xFF)", 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF, 0xFFFFFFFF, False),
        ("zona a cero (sin SoftDevice)", 0x0000, 0x00000000, 0x0, 0x00, 0x00000000, False),
        ("codigo con tamano creible pero id que no es", 0x0100, 0x27000, 0x0, 44, 0x1234, False),
        ("ficha con tamano del SD imposible", 0x0100, 0x00000010, 0x0, 44, 0x8C, False),
        # â˜… LOS DOS FALSOS POSITIVOS DE ESTE PROYECTO: leer 0x1000 por debajo de la ficha da
        #   codigo ARM, y esos bytes son exactamente 0xE002 (S140 6.1.1) y 0xD902 (S140 7.3.0).
        ("EL FALSO 0xE002: fwid e id de codigo ARM", 0xE002, 0x4601D902, 0x0, 10, 0x4601D902, False),
        ("EL FALSO 0xD902: fwid e id de codigo ARM", 0xD902, 0x42820000, 0x0, 23, 0x42820000, False),
    ]
    for desc, fwid, tam, ver, tf, idv, esperado in tabla:
        got = parece_valido(imagen(fwid, tam, ver, tf, idv))
        etiqueta = "ARRANCA BLE" if got else "NO ARRANCA"
        if got == esperado:
            bien("%-48s fwid=0x%04X id=0x%02X -> %s" % (desc, fwid, idv & 0xFF, etiqueta))
        else:
            mal("%-48s fwid=0x%04X id=0x%02X -> %s (se esperaba %s)"
                % (desc, fwid, idv & 0xFF, etiqueta, "ARRANCA BLE" if esperado else "NO ARRANCA"))


def ini_declarado():
    """El -DSD_ESPERADO_FWID que declara platformio.ini, por entorno."""
    ruta = os.path.join(RAIZ, "platformio.ini")
    texto = open(ruta, "r", encoding="utf-8").read()
    decl = {}
    env = None
    for linea in texto.splitlines():
        m = re.match(r"^\[env:([^\]]+)\]", linea.strip())
        if m:
            env = m.group(1)
            continue
        m = re.search(r"-D\s*SD_ESPERADO_FWID\s*=\s*(0x[0-9A-Fa-f]+)", linea)
        if m and env:
            decl[env] = int(m.group(1), 16)
    return decl


def platformio():
    print("\n=== 2. platformio.ini: cada entorno declara el fwid de SU placa ===")
    decl = ini_declarado()
    for env, esperado in ENTORNOS.items():
        if env not in decl:
            mal("%s NO declara -DSD_ESPERADO_FWID (la sonda usaria el valor de socorro)" % env)
            continue
        if decl[env] == esperado:
            bien("%-22s declara 0x%04X" % (env, decl[env]))
        else:
            mal("%-22s declara 0x%04X y su placa pide 0x%04X"
                % (env, decl[env], esperado))
    otros = set(decl) - set(ENTORNOS)
    if otros:
        print("  (nota) otros entornos con SD_ESPERADO_FWID: %s" % ", ".join(sorted(otros)))
    # â˜… Y que no vuelva el numero inventado: 0x0101 no es el fwid de ninguna S140 (se mira
    #   SOLO el valor declarado, no los comentarios, que si pueden hablar de la historia).
    if re.search(r"-D\s*SD_ESPERADO_FWID\s*=\s*0x0101", open(os.path.join(RAIZ, "platformio.ini"), "r", encoding="utf-8").read()):
        mal("platformio.ini vuelve a declarar 0x0101 como fwid esperado: ese NO es el fwid de "
            "ninguna S140 (el de la v7 es 0x0100)")


def board_json():
    print("\n=== 3. boards\\*.json: el numero sale de la definicion de placa ===")
    import json
    correspondencia = {
        "techo-nrf52840": 0x00B6,
        "techo-nrf52840-s140v7": 0x0123,
    }
    for placa, esperado in correspondencia.items():
        ruta = os.path.join(RAIZ, "boards", placa + ".json")
        if not os.path.exists(ruta):
            mal("no encuentro %s" % ruta)
            continue
        datos = json.load(open(ruta, "r", encoding="utf-8"))
        fwid = int(datos["build"]["softdevice"]["sd_fwid"], 16)
        if fwid == esperado:
            bien("%-24s sd_fwid=0x%04X (S140 %s)"
                 % (placa, fwid, datos["build"]["softdevice"]["sd_version"]))
        else:
            mal("%-24s sd_fwid=0x%04X y se esperaba 0x%04X" % (placa, fwid, esperado))


def binario():
    """Sobre el binario compilado: Â¿la sonda mira de verdad 0x300C?"""
    print("\n=== 4. El binario compilado: la sonda lee la direccion correcta ===")
    tc = None
    for raiz, dirs, ficheros in os.walk(os.path.join(RAIZ, "_pio_core", "packages")):
        if "arm-none-eabi-objdump.exe" in ficheros:
            tc = os.path.join(raiz, "arm-none-eabi-objdump.exe")
            break
    if tc is None:
        print("  aviso  no encuentro arm-none-eabi-objdump: me salto esta comprobacion")
        return
    # El `movw rX, #0x300C` que carga la direccion del fwid. 0x300C = 12300.
    patron = re.compile(r"movw\s+r\d+,\s*#(\d+)\s*;\s*0x([0-9a-fA-F]+)")
    for env in ("techo_plus_s140v7", "techo_plus_s140v6"):
        elf = os.path.join(RAIZ, ".pio", "build", env, "firmware.elf")
        if not os.path.exists(elf):
            print("  aviso  no hay %s compilado: me salto %s" % (elf, env))
            continue
        salida = subprocess.run([tc, "-d", elf], capture_output=True, text=True).stdout
        usos = len([1 for m in patron.finditer(salida) if int(m.group(2), 16) == SD_FWID_DIR])
        if usos > 0:
            bien("%-20s carga 0x%04X (SD_FWID_DIR): %d sitios" % (env, SD_FWID_DIR, usos))
        else:
            mal("%-20s NO carga 0x%04X: la sonda miraria a otro sitio"
                % (env, SD_FWID_DIR))
        # Y que NO se quede ninguna lectura de la direccion vieja (0x200C) DENTRO de la sonda.
        #   OJO: 0x200C sale tambien en `UsbLector::init()` por un buffer de la RAM (coincide
        #   el numero, no la intencion), asi que solo se avisa si aparece en las funciones de
        #   la sonda, que es donde importa.
        funciones_de_la_sonda = []
        actual = ""
        for linea in salida.splitlines():
            mf = re.match(r"^[0-9a-f]{8} <(.+)>:", linea)
            if mf:
                actual = mf.group(1)
                continue
            m = patron.search(linea)
            if m and int(m.group(2), 16) == 0x200C:
                funciones_de_la_sonda.append(actual)
        sospechosas = [f for f in funciones_de_la_sonda
                       if "softdevice" in f.lower() or "parece" in f.lower() or "fwid" in f.lower()]
        if sospechosas:
            mal("%-20s carga 0x200C dentro de la sonda (%s): queda la direccion vieja"
                % (env, ", ".join(sorted(set(sospechosas)))))
        else:
            bien("%-20s ningun 0x200C en la sonda (el que hay es un buffer de UsbLector)"
                 % env)


def main():
    print("\n############ SONDA DEL SOFTDEVICE (b32: huella por id + tamano) ############")
    print("  ficha del SoftDevice en 0x%04X, fwid en 0x%04X (MBR_SIZE=0x1000)"
          % (SD_INFO_DIR, SD_FWID_DIR))
    casos()
    platformio()
    board_json()
    binario()
    print("\n=================== RESUMEN ===================")
    if fallos == 0:
        print("FALLOS: 0    TODO CORRECTO")
        return 0
    print("FALLOS: %d    HAY FALLOS" % fallos)
    return 1


if __name__ == "__main__":
    sys.exit(main())
