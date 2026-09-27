#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
uf2_a_bin.py -- saca el binario "crudo" de un fichero UF2.

Un .uf2 no es un binario: es una lista de bloques de 512 bytes. Cada bloque tiene una
cabecera de 32 bytes y lleva 256 bytes de datos que van a una direccion concreta de la
memoria del chip. Esta herramienta:

  1) lee todos los bloques,
  2) los coloca en su direccion,
  3) escribe el binario resultante (rellenando los huecos con 0xFF, que es como esta
     la flash borrada), con su direccion base y su longitud.

Uso:  python uf2_a_bin.py entrada.uf2 salida.bin
"""
import sys
import struct

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
BLOQUE = 512

# OJO CON ESTO, que es donde me equivoque la primera vez:
#
# La cabecera de un bloque UF2 son SIETE enteros de 32 bits (28 bytes) -- magic0, magic1,
# flags, direccion, tamano, numero de bloque, total de bloques -- y los DATOS EMPIEZAN EN
# EL BYTE 28, no en el 32. Yo esperaba 8 enteros y un "magic final" al estilo de otros
# formatos, y no existe tal cosa: esos 4 bytes de mas son ya el primer dato del firmware.
# En nuestro respaldo, el byte 28 vale 0x239A0029, que es el VID/PID de USB de Adafruit
# (0x239A = Adafruit, 0x0029 = T-Echo). Leerlo como si fuera un magic me hacia descartar
# TODOS los bloques.
#
# Si el flag 0x00002000 (familyID) esta puesto, hay 8 enteros (32 bytes) de cabecera.
FLAG_FAMILY_ID = 0x00002000


def main():
    if len(sys.argv) < 3:
        print("uso: python uf2_a_bin.py entrada.uf2 salida.bin")
        return 1

    entrada, salida = sys.argv[1], sys.argv[2]
    with open(entrada, "rb") as f:
        datos = f.read()

    if len(datos) % BLOQUE != 0:
        print("AVISO: el fichero no es multiplo de 512 bytes")

    piezas = []          # (direccion, payload)
    malos = 0
    for off in range(0, len(datos) - BLOQUE + 1, BLOQUE):
        b = datos[off:off + BLOQUE]
        m0, m1, flags, dir_destino, tam, _seq, _total = struct.unpack_from("<7I", b, 0)
        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1:
            malos += 1
            continue
        # la cabecera son 28 bytes, o 32 si viene el identificador de familia
        ini = 32 if (flags & FLAG_FAMILY_ID) else 28
        if tam > BLOQUE - ini:
            malos += 1
            continue
        piezas.append((dir_destino, b[ini:ini + tam]))

    if not piezas:
        print("ERROR: ningun bloque UF2 valido")
        return 2

    base = min(d for d, _ in piezas)
    fin = max(d + len(p) for d, p in piezas)
    print("bloques validos : %d  (descartados: %d)" % (len(piezas), malos))
    print("direccion base  : 0x%X" % base)
    print("direccion final : 0x%X" % fin)
    print("tamano          : %d bytes (0x%X)" % (fin - base, fin - base))

    # Rellenamos los huecos con 0xFF: asi el desensamblador ve el hueco tal cual esta en
    # el chip (flash borrada) y las direcciones de las instrucciones son LAS DE VERDAD,
    # que es lo que hace falta para leer el desensamblado.
    img = bytearray(b"\xFF" * (fin - base))
    for d, p in piezas:
        img[d - base:d - base + len(p)] = p

    with open(salida, "wb") as f:
        f.write(img)

    print("escrito         : %s" % salida)
    return 0


if __name__ == "__main__":
    sys.exit(main())
