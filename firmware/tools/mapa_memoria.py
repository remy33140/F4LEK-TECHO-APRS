# -*- coding: utf-8 -*-
"""mapa_memoria.py - saca el MAPA REAL de un UF2 o un Intel HEX (solo lectura).

PARA QUE: saber donde acaba de verdad el SoftDevice y donde empieza la aplicacion, sin
depender de lo que diga un comentario. Lee las regiones contiguas, la ficha del SoftDevice
(offsets de nrf_sdm.h, base 0x1000 = MBR_SIZE) y, si la aplicacion esta en el UF2, la
direccion de su tabla de vectores.

NO GRABA NADA Y NO TOCA NINGUN PUERTO. Solo abre ficheros.

uso:  python mapa_memoria.py <ficha.uf2|ficha.hex> [...]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uf2 import lee_uf2, regiones        # noqa: E402
from lee_hex import lee_hex              # noqa: E402

MBR = 0x1000
SD_INFO = MBR + 0x2000                   # SOFTDEVICE_INFO_STRUCT_ADDRESS
SD_FWID = SD_INFO + 0x0C                 # SD_FWID_OFFSET
SD_ID = SD_INFO + 0x10                   # SD_ID_OFFSET
SD_SIZE = SD_INFO + 0x08                 # SD_SIZE_OFFSET
SD_VERSION = SD_INFO + 0x14              # SD_VERSION_OFFSET


def u32(mem, d):
    if not all((d + i) in mem for i in range(4)):
        return None
    v = 0
    for i in range(4):
        v |= mem[d + i] << (8 * i)
    return v


def u16(mem, d):
    if not all((d + i) in mem for i in range(2)):
        return None
    return mem[d] | (mem[d + 1] << 8)


def carga(ruta):
    """Devuelve (mem, regiones) de un UF2 o un HEX."""
    if ruta.lower().endswith(".uf2"):
        bl = lee_uf2(ruta)
        mem = {}
        for d, v in regiones(bl):
            for i, byte in enumerate(v):
                mem[d + i] = byte
        fams = sorted({b.familia for b in bl})
        return mem, regiones(bl), fams
    mem = lee_hex(ruta)
    direcciones = sorted(mem)
    regs = []
    for d in direcciones:
        if regs and regs[-1][0] + len(regs[-1][1]) == d:
            regs[-1][1].append(mem[d])
        else:
            regs.append((d, bytearray([mem[d]])))
    return mem, [(d, bytes(v)) for d, v in regs], []


def informa(ruta):
    print("=" * 78)
    print("FICHERO : %s" % ruta)
    if not os.path.exists(ruta):
        print("  (no existe)")
        return
    print("TAMANO  : %d bytes" % os.path.getsize(ruta))
    mem, regs, fams = carga(ruta)
    if fams:
        print("FAMILIAS: %s" % ", ".join("0x%04X" % f for f in fams))
    print("REGIONES CONTIGUAS (%d):" % len(regs))
    for d, v in regs:
        print("   0x%08X - 0x%08X   %8d B" % (d, d + len(v) - 1, len(v)))

    print("FICHA DEL SOFTDEVICE (leida en 0x%04X, offsets de nrf_sdm.h):" % SD_INFO)
    tam = u32(mem, SD_SIZE)
    fwid = u16(mem, SD_FWID)
    idsd = u16(mem, SD_ID)
    ver = u32(mem, SD_VERSION)
    if tam is None or fwid is None:
        print("   NO HAY FICHA en 0x%04X (zona sin datos)" % SD_INFO)
    else:
        print("   tamano del SD   = 0x%06X  (%d B)" % (tam, tam))
        print("   fwid            = 0x%04X" % fwid)
        print("   id              = 0x%04X" % idsd)
        if ver is not None:
            print("   version         = 0x%08X  -> %d.%d.%d" %
                  (ver, (ver >> 24) & 0xFF, (ver >> 16) & 0xFF, ver & 0xFFFF))
        if tam:
            print("   el SD acabaria en 0x%08X (base 0x%04X + 0x%X)" % (MBR + tam, MBR, tam))

    # Tabla de vectores: busca una pila (0x2000xxxx) seguida de un PC en flash, alineado a
    # 0x1000, por encima de 0x1000. La primera que aparezca es la aplicacion.
    print("TABLA DE VECTORES (candidatos alineados a 0x1000):")
    for d in sorted(set(mem)):
        if d < 0x1000 or d > 0x100000 or (d & 0xFFF) != 0:
            continue
        if d not in mem:
            continue
        sp = u32(mem, d)
        pc = u32(mem, d + 4)
        if sp is None or pc is None:
            continue
        if 0x20000000 <= sp <= 0x20040000 and 0x1000 <= (pc & 0xFFFFFFFE) <= 0x100000:
            print("   0x%08X: SP=0x%08X  Reset=0x%08X  <- parece tabla de vectores"
                  % (d, sp, pc & 0xFFFFFFFE))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for ruta in sys.argv[1:]:
        informa(ruta)
    return 0


if __name__ == "__main__":
    sys.exit(main())
