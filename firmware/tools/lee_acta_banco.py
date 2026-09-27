# -*- coding: utf-8 -*-
"""Lee el acta del banco de diagnostico AGUANTANDO los reinicios del nodo.

POR QUE CON REINTENTOS: al abrir el puerto, el `Serial.begin(115200)` del nodo se reinicia
(le llega la senal de DTR del PC). Y el banco, al terminar de medir, se reinicia a proposito.
O sea: el puerto se cae varias veces mientras el nodo imprime. Un lector de una sola pasada se
queda sin el acta; este se vuelve a enganchar cada vez y va acumulando TODO lo que se vea.

Uso:  python lee_acta_banco.py COM40 [segundos_totales]
"""
import sys
import time

import serial

# La consola de Windows (cp1252) revienta con los caracteres de las cajas de texto del acta
# ("####" con adornos, flechas...). Se fuerza UTF-8 con reemplazo y se escribe en BYTES, que
# es la unica forma de que salga TODO sin perder lineas por el camino.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

PUERTO = sys.argv[1] if len(sys.argv) > 1 else "COM40"
TOTAL = float(sys.argv[2]) if len(sys.argv) > 2 else 40.0

trozo_total = []
t0 = time.time()
intentos = 0


def saca(datos: bytes):
    """Escribe los bytes tal cual, sin que la consola los interprete."""
    try:
        sys.stdout.buffer.write(datos)
        sys.stdout.buffer.flush()
    except Exception:
        sys.stdout.write(datos.decode("utf-8", errors="replace"))
        sys.stdout.flush()

while time.time() - t0 < TOTAL:
    intentos += 1
    try:
        with serial.Serial(PUERTO, 115200, timeout=0.2) as p:
            print("[%5.1fs] puerto abierto (intento %d)" % (time.time() - t0, intentos))
            # No se toca DTR/RTS: se deja como esta para no provocar mas reinicios.
            t_apertura = time.time()
            while time.time() - t0 < TOTAL:
                try:
                    datos = p.read(4096)
                except Exception as e:
                    print("[%5.1fs] se corto la lectura: %s" % (time.time() - t0, e))
                    break
                if datos:
                    trozo_total.append(datos)
                    saca(datos)
                # Si lleva 12 s abierto sin que se caiga, se manda `rep` una vez.
                elif time.time() - t_apertura > 12 and not getattr(p, "_rep_enviado", False):
                    try:
                        p.write(b"rep\n")
                        p.flush()
                        p._rep_enviado = True
                        print("\n[%5.1fs] mandado: rep" % (time.time() - t0))
                    except Exception as e:
                        print("[%5.1fs] no se pudo mandar rep: %s" % (time.time() - t0, e))
    except Exception as e:
        print("[%5.1fs] no se pudo abrir: %s" % (time.time() - t0, e))
        time.sleep(1.0)

bruto = b"".join(trozo_total)
print("\n=== TOTAL: %d bytes en %d intentos ===" % (len(bruto), intentos))
