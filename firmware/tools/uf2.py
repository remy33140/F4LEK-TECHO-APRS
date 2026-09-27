#!/usr/bin/env python3
# uf2.py — leer y escribir UF2 (el formato del cargador de los nRF52840)
#
# POR QUE HACE FALTA ESTO: en un T-Echo el SoftDevice NO viene de fabrica "puesto y punto":
# el firmware de fabrica trae el suyo, y se actualiza grabando un UF2 que lleva el SoftDevice
# pegado delante de la aplicacion (asi lo hace el proyecto aleman del T-Echo con `make uf2_sd`,
# y asi lo dice su README: "you must also install the correct SoftDevice"). Este modulo sabe
# leer un UF2 (para mirar que lleva dentro: familias, direcciones y la ficha del SoftDevice) y
# escribir uno nuevo, sin depender de herramientas externas.
#
# Formato de bloque (512 bytes, todo little-endian):
#   0x00 magic1 = 0x0A324655 ('UF2\n')   0x04 magic2 = 0x9E5D5157
#   0x08 flags                           0x0C direccion de destino en la flash
#   0x10 tamano del bloque de datos      0x14 numero de bloque
#   0x18 total de bloques                0x1C tamano de familia (o file size)
#   0x20 numero de familia               0x24..0x1FF datos (hasta 476 B) + relleno
#   0x1FC magic de fin = 0x0AB16F30
#
# License: GPL-3.0

import struct
import sys

MAGIC1 = 0x0A324655
MAGIC2 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
TAM_BLOQUE = 512
MAX_DATOS = 476

# Familias que usa este proyecto
FAMILIA_APP_NRF52840 = 0xADA52840   # aplicacion nRF52840 (la que usa Adafruit)
FAMILIA_SD_NRF52840 = 0x68B9        # SoftDevice nRF52840

# Banderas
FLAG_NOFLASH = 0x00000001           # no grabar esto (solo informativo)


class Bloque:
    def __init__(self, direccion, datos, familia, flags=0):
        self.direccion = direccion
        self.datos = datos
        self.familia = familia
        self.flags = flags

    def __repr__(self):
        return "0x%08X +%d B fam=0x%04X flags=0x%08X" % (
            self.direccion, len(self.datos), self.familia & 0xFFFFFFFF, self.flags)


def lee_uf2(ruta):
    """Devuelve la lista de bloques de un UF2. Revienta si no es un UF2."""
    with open(ruta, "rb") as f:
        bruto = f.read()
    if len(bruto) % TAM_BLOQUE != 0:
        raise ValueError("%s no es un UF2: %d bytes no es multiplo de 512"
                         % (ruta, len(bruto)))
    bloques = []
    for i in range(0, len(bruto), TAM_BLOQUE):
        b = bruto[i:i + TAM_BLOQUE]
        m1, m2, flags, dir_, tam, num, total, fam, tam_fam = struct.unpack_from("<9I", b, 0)
        if m1 != MAGIC1 or m2 != MAGIC2 or struct.unpack_from("<I", b, 0x1FC)[0] != MAGIC_END:
            raise ValueError("bloque %d de %s no tiene las marcas de UF2" % (i // TAM_BLOQUE, ruta))
        if tam > MAX_DATOS:
            raise ValueError("bloque %d dice %d bytes de datos (maximo %d)" % (i // TAM_BLOQUE, tam, MAX_DATOS))
        bloques.append(Bloque(dir_, b[0x24:0x24 + tam], fam, flags))
    return bloques


def regiones(bloques):
    """Junta los bloques en regiones contiguas: [(direccion_inicio, bytearray)]."""
    orden = sorted([b for b in bloques if not (b.flags & FLAG_NOFLASH)],
                   key=lambda b: b.direccion)
    salida = []
    for b in orden:
        if salida and salida[-1][0] + len(salida[-1][1]) == b.direccion:
            salida[-1][1].extend(b.datos)
        else:
            salida.append((b.direccion, bytearray(b.datos)))
    return [(d, bytes(v)) for d, v in salida]


def escribe_uf2(ruta, bloques):
    """Escribe un UF2 con los bloques dados (reparte los datos en trozos de 476 B)."""
    # Se expande lo que venga en trozos grandes
    planos = []
    for b in bloques:
        datos = b.datos
        if not datos:
            continue
        off = 0
        while off < len(datos):
            trozo = datos[off:off + MAX_DATOS]
            planos.append(Bloque(b.direccion + off, trozo, b.familia, b.flags))
            off += len(trozo)
    total = len(planos)
    with open(ruta, "wb") as f:
        for i, b in enumerate(planos):
            fila = struct.pack("<9I", MAGIC1, MAGIC2, b.flags, b.direccion, len(b.datos),
                               i, total, b.familia, FAMILIA_APP_NRF52840)
            fila += b.datos + b"\x00" * (MAX_DATOS - len(b.datos))
            fila += struct.pack("<I", MAGIC_END)
            assert len(fila) == TAM_BLOQUE, len(fila)
            f.write(fila)
    return total


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        print("uso: python uf2.py info <ficha.uf2>")
        print("     python uf2.py saca <ficha.uf2> <desde_hex> <hasta_hex> <salida.bin>")
        return 1
    modo = sys.argv[1]
    ruta = sys.argv[2]
    bloques = lee_uf2(ruta)
    if modo == "info":
        print("fichero : %s" % ruta)
        print("bloques : %d" % len(bloques))
        familias = {}
        for b in bloques:
            familias[b.familia] = familias.get(b.familia, 0) + 1
        print("familias: %s" % ", ".join("0x%04X (%d bloques)" % (k, v) for k, v in sorted(familias.items())))
        print("regiones contiguas:")
        for d, v in regiones(bloques):
            print("   0x%08X - 0x%08X  (%d B)" % (d, d + len(v) - 1, len(v)))
        return 0
    if modo == "saca":
        desde = int(sys.argv[3], 16)
        hasta = int(sys.argv[4], 16)
        salida = sys.argv[5]
        trozo = bytearray()
        for d, v in regiones(bloques):
            for i, byte in enumerate(v):
                if desde <= d + i < hasta:
                    trozo.append(byte)
        with open(salida, "wb") as f:
            f.write(trozo)
        print("escritos %d bytes de 0x%08X-0x%08X en %s" % (len(trozo), desde, hasta, salida))
        return 0
    print("modo desconocido: %s" % modo)
    return 1


if __name__ == "__main__":
    sys.exit(main())
