#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
busca_pantalla.py -- busca en un binario de firmware las PISTAS de su driver de pantalla.

No hace magia: busca tres cosas concretas y las lista con su direccion, para luego poder
desensamblar alrededor de esas direcciones.

  1) La SECUENCIA DE ARRANQUE del controlador SSD1681 (el del panel del T-Echo). Los
     valores 0xC7, 0x05, 0x80, 0x03 son muy caracteristicos del driver de este panel.

  2) Los numeros de pin de la pantalla (29, 30, 31 = MOSI, CS, SCK y 28, 2, 3 = DC, RST,
     BUSY). Salen como constantes pequeñas y cuesta mas verlos, asi que ademas los busco
     juntos: si el firmware configura estos pines seguidos, aparecen juntos.

  3) Las cadenas de texto: casi todos los firmwares llevan dentro su nombre o su version.

Uso:  python busca_pantalla.py firmware.bin [direccion_base]
"""
import re
import sys


def cadenas(datos, minimo=6):
    """Saca las cadenas ASCII imprimibles de longitud >= minimo, con su direccion."""
    salida = []
    for m in re.finditer(rb"[\x20-\x7E]{%d,}" % minimo, datos):
        salida.append((m.start(), m.group().decode("ascii", "replace")))
    return salida


def busca_bytes(datos, patron, etiqueta, base):
    """Busca un patron en las dos formas en que puede estar en el firmware.

    Un programa puede escribir la secuencia como bytes seguidos ("12 01 C7 00 00 ...") o
    como palabras de 32 bits ya formadas (0x0001C701, 0x8018053C ...). Se buscan las dos.
    """
    hallazgos = []
    # (a) tal cual, bytes seguidos
    for m in re.finditer(re.escape(patron), datos):
        hallazgos.append((m.start() + base, "bytes seguidos"))
    # (b) por palabras de 32 bits: se busca la primera palabra y se prueba a leer 4 en 4
    for i in range(0, len(patron) - 4 + 1, 4):
        trozo = patron[i:i + 4]
        for m in re.finditer(re.escape(trozo), datos):
            hallazgos.append((m.start() + base, "como palabra de 32 bits (desplazada %d)" % i))
    return hallazgos


def main():
    if len(sys.argv) < 2:
        print("uso: python busca_pantalla.py firmware.bin [direccion_base]")
        return 1

    ruta = sys.argv[1]
    base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x1000

    with open(ruta, "rb") as f:
        datos = f.read()
    print("fichero: %s  (%d bytes, base 0x%X)\n" % (ruta, len(datos), base))

    # ---------------------------------------------------------------- 1) el driver
    print("=" * 78)
    print("1) SECUENCIA DE ARRANQUE DEL SSD1681")
    print("=" * 78)
    # los valores que caracterizan al driver de este panel
    secuencias = {
        "0x01 0xC7 (driver output control)": bytes([0x01, 0xC7]),
        "0x3C 0x05 (border waveform, completo)": bytes([0x3C, 0x05]),
        "0x18 0x80 (sensor de temperatura)": bytes([0x18, 0x80]),
        "0x22 0xF7 (refresco completo)": bytes([0x22, 0xF7]),
        "0x22 0xFF (refresco parcial)": bytes([0x22, 0xFF]),
        "0x11 0x03 (modo de escritura)": bytes([0x11, 0x03]),
    }
    for etiqueta, pat in secuencias.items():
        hallazgos = busca_bytes(datos, pat, etiqueta, base)
        if hallazgos:
            direcciones = ", ".join("0x%X" % d for d, _ in hallazgos[:8])
            print("  SI  %-40s %d veces: %s" % (etiqueta, len(hallazgos), direcciones))
        else:
            print("  no  %-40s" % etiqueta)

    # ---------------------------------------------------------------- 2) los pines
    print()
    print("=" * 78)
    print("2) LOS NUMEROS DE PIN DE LA PANTALLA")
    print("=" * 78)
    # Si el firmware los configura juntos, aparecen juntos. Probamos las combinaciones
    # que tendria sentido ver en una tabla de pines.
    combinaciones = {
        "CS=30, SCK=31, MOSI=29 (nuestros)": bytes([30, 31, 29]),
        "MOSI=29, SCK=31, CS=30": bytes([29, 31, 30]),
        "DC=28, RST=2, BUSY=3": bytes([28, 2, 3]),
        "RST=2, BUSY=3, DC=28": bytes([2, 3, 28]),
    }
    for etiqueta, pat in combinaciones.items():
        n = len(list(re.finditer(re.escape(pat), datos)))
        print("  %-38s %s" % (etiqueta, ("%d veces" % n) if n else "no aparece seguido"))

    # ---------------------------------------------------------------- 3) cadenas
    print()
    print("=" * 78)
    print("3) CADENAS CON NOMBRE O VERSION (las 40 primeras que interesen)")
    print("=" * 78)
    interesantes = re.compile(
        r"(version|VERSION|Version|build|BUILD|nrf|NRF|SDK|S140|bootloader|"
        r"igate|iGate|APRS|aprs|Kacho|KACHO|T-?Echo|epaper|EPD|display|SSD16|GDEH|"
        r"Adafruit|Bluefruit|Meshtastic)", re.I)
    vistas = 0
    for dir_, s in cadenas(datos, 6):
        if interesantes.search(s):
            print("  0x%06X  %s" % (dir_ + base, s[:100]))
            vistas += 1
            if vistas >= 40:
                break
    if vistas == 0:
        print("  (ninguna)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
