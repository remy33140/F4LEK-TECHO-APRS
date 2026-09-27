# -*- coding: utf-8 -*-
"""Lee un Intel HEX y saca lo que hay en una direccion concreta.

Para que: comprobar el `fwid` del SoftDevice ANTES de grabarlo. Si no es el que espera
nuestro firmware, no se graba. Uso:

    python lee_hex.py <ficha.hex> <direccion_hex> [cuantos_bytes]
"""
import sys

def lee_hex(ruta):
    """Devuelve {direccion: byte} de un Intel HEX (soporta 04 y 02)."""
    mem = {}
    base = 0
    with open(ruta, 'r', encoding='ascii', errors='replace') as f:
        for cruda in f:
            l = cruda.strip()
            if not l.startswith(':'):
                continue
            b = bytes.fromhex(l[1:])
            n, dir_lo, tipo = b[0], (b[1] << 8) | b[2], b[3]
            datos = b[4:4 + n]
            if tipo == 0x00:
                for i, v in enumerate(datos):
                    mem[base + dir_lo + i] = v
            elif tipo == 0x04:
                base = ((datos[0] << 8) | datos[1]) << 16
            elif tipo == 0x02:
                base = ((datos[0] << 8) | datos[1]) << 4
    return mem

def main():
    ruta = sys.argv[1]
    dir_obj = int(sys.argv[2], 16)
    cuantos = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    mem = lee_hex(ruta)
    direcciones = sorted(mem)
    print("fichero   : %s" % ruta)
    print("bytes     : %d" % len(mem))
    print("rango     : 0x%08X - 0x%08X" % (direcciones[0], direcciones[-1]))
    print()
    print("=== 0x%08X, %d bytes ===" % (dir_obj, cuantos))
    for i in range(cuantos):
        d = dir_obj + i
        print("  0x%08X = 0x%02X%s" % (d, mem.get(d, -1) & 0xFF, "" if d in mem else "   (NO esta en el hex)"))
    # El dato que decide: la ficha del SoftDevice (offsets de nrf_sdm.h)
    print()
    print("=== la ficha del SoftDevice en 0x2000 ===")
    campos = [("tamano de la ficha", 0x2000, 1), ("tamano del SD", 0x2008, 4),
              ("fwid", 0x200C, 2), ("id", 0x2010, 2), ("version (M.mmm.bbb)", 0x2014, 4)]
    for nombre, d, n in campos:
        if all((d + i) in mem for i in range(n)):
            v = 0
            for i in range(n):
                v |= mem[d + i] << (8 * i)
            print("  %-22s 0x%08X = 0x%0*X" % (nombre, d, n * 2, v))
        else:
            print("  %-22s 0x%08X = (no esta: zona sin datos en el hex)" % (nombre, d))
    if 0x2014 in mem:
        v = 0
        for i in range(4):
            v |= mem[0x2014 + i] << (8 * i)
        print("  version desglosada   %d.%d.%d" % ((v >> 24) & 0xFF, (v >> 16) & 0xFF, v & 0xFFFF))

if __name__ == '__main__':
    main()
