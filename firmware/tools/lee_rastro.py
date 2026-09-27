# -*- coding: utf-8 -*-
"""lee_rastro.py - lee el rastro del banco de pruebas del SoftDevice SIN ABRIR NINGUN PUERTO COM.

=============================================================================
 PARA QUE EXISTE
=============================================================================
 El diagnostico del arranque del Bluetooth se apunta en la FLASH (pagina 0xE7000, ver
 `diag_sd\\ble_rastro.h`) porque esta MEDIDO que en cuanto el SoftDevice arranca el USB se
 queda mudo. Pero entonces, ¿como se lee? Aqui esta el truco, que ya uso el banco viejo
 `_BLE_DIAG`:

   **con el nodo en modo cargador, la unidad que expone (TECHOBOOT) lleva un fichero
   `CURRENT.UF2` que es una COPIA ENTERA de la zona de aplicacion de la flash
   (0x27000..0xEA000)**. Como 0xE7000 cae dentro de esa zona, el rastro viaja ahi dentro.

 O sea: se puede leer el resultado COPIANDO UN FICHERO, sin abrir el puerto serie, sin ADB y
 sin tocar ningun COM. Es la unica forma de leer un diagnostico cuyo instrumento (el USB) es
 justo lo que se rompe.

=============================================================================
 COMO SE USA (nada de esto graba ni toca el nodo)
=============================================================================
  1) El operador graba `techo_diag_sd` en el nodo (doble toque al reset + copiar el .uf2).
  2) El nodo arranca, hace sus pasos, los apunta en la flash y se queda latiendo.
  3) Doble toque al reset OTRA VEZ -> aparece la unidad del cargador.
  4) Se copia `CURRENT.UF2` al PC y se le pasa a esta herramienta:

         python tools\\lee_rastro.py F:\\CURRENT.UF2

     (o un .bin/.hex de la zona, o incluso el firmware.uf2 del propio banco de pruebas)

 ESTA HERRAMIENTA SOLO LEE FICHEROS. No graba, no abre puertos, no habla con el nodo.

=============================================================================
 QUE SACA
=============================================================================
 - Si hubo un FALLO DURO en el arranque anterior: sus registros (CFSR, HFSR, MMFAR, BFAR, PC).
 - El ULTIMO PASO al que llego el arranque anterior, EN PALABRAS.
 - Los codigos de error de `sd_softdevice_enable()` y de `sd_ble_enable()`.
 - La RAM que el SoftDevice PIDE DE VERDAD (el valor con el que sale `sd_ble_enable`).
 - La ficha del SoftDevice leida de la flash (fwid, tamano, id, version), en las dos
   direcciones posibles (0x3000 y 0x2FFC), para no depender de cual sea la buena.

 License: GPL-3.0
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# --- el formato, copiado de diag_sd/ble_rastro.h (si alli cambia, aqui tambien) ---
BASE = 0xE7000
MAGIC = 0x4D494147            # "GAIM"
MAX_REG = 120
PASO_BASE = 8 + 4 * MAX_REG   # donde empiezan los TEXTOS

T_PASO, T_ERR, T_DATO, T_TEXTO, T_FALTA = 1, 2, 3, 4, 5

# --- nombres de los pasos y de los datos (copiados de diag_sd/main.cpp) ---
PASOS = {
    1: "arranque: leer el rastro anterior",
    2: "leer la ficha del SoftDevice (0x3000)",
    3: "sd_softdevice_enable()",
    4: "sd_ble_cfg_set() x7",
    5: "sd_ble_enable()",
    6: "sd_ble_gap_device_name_set()",
    7: "sd_softdevice_disable()",
    8: "fin del guion",
}

ERRORES = {
    1: "sd_softdevice_enable()",
    2: "sd_ble_cfg_set(BLE_COMMON_CFG_VS_UUID)",
    3: "sd_ble_cfg_set(BLE_GAP_CFG_ROLE_COUNT)",
    4: "sd_ble_cfg_set(BLE_GATTS_CFG_ATTR_TAB_SIZE)",
    5: "sd_ble_cfg_set(BLE_CONN_CFG_GATT / MTU)",
    6: "sd_ble_cfg_set(BLE_CONN_CFG_GAP)",
    7: "sd_ble_cfg_set(BLE_CONN_CFG_GATTS / hvn)",
    8: "sd_ble_cfg_set(BLE_CONN_CFG_GATTC / write cmd)",
    9: "sd_ble_enable()",
    10: "sd_ble_gap_device_name_set()",
    11: "sd_softdevice_disable()",
}

DATOS = {
    1: "fwid leido en 0x300C",
    2: "tamano del SoftDevice (0x3008)",
    3: "id del SoftDevice (0x3010)",
    4: "version del SoftDevice (0x3014)",
    5: "tamano de la ficha (0x3000, u8)",
    6: "RAM de la aplicacion al entrar (__data_start__)",
    7: "RAM que PIDE el SoftDevice (sale de sd_ble_enable)",
    8: "sd_softdevice_is_enabled()",
    11: "tamano de la ficha leida en 0x2FFC",
    12: "tamano del SD leido en 0x2FFC",
    13: "fwid leido en 0x3008 (ficha desplazada)",
    14: "id del SD leido en 0x300C (ficha desplazada)",
    15: "version del SD leida en 0x3010 (ficha desplazada)",
    21: "ULTIMO PASO del arranque ANTERIOR",
    22: "sd_softdevice_enable() del arranque ANTERIOR",
    23: "sd_ble_enable() del arranque ANTERIOR",
    24: "RAM que pidio en el arranque ANTERIOR",
    25: "sd_softdevice_enable() de AHORA",
    26: "primer sd_ble_cfg_set() que fallo (0 = todos bien)",
    27: "RESETREAS",
    28: "NVMC->READY",
    29: "interrupciones reservadas al SD que estaban encendidas",
    30: "prioridad de POWER_CLOCK",
    31: "prioridad de USBD",
    32: "MAINREGSTATUS (alimentacion)",
    33: "USBREGSTATUS (regulador del USB)",
    40: "FALLO DURO anterior: hubo",
    41: "FALLO DURO anterior: CFSR",
    42: "FALLO DURO anterior: HFSR",
    43: "FALLO DURO anterior: MMFAR",
    44: "FALLO DURO anterior: BFAR",
    45: "FALLO DURO anterior: PC (la instruccion que fallo)",
    46: "FALLO DURO anterior: LR",
}

FALTAS = {1: "CFSR", 2: "HFSR", 3: "MMFAR", 4: "BFAR", 5: "PC", 6: "LR"}

# Codigos de error de Nordic (nrf_error.h / nrf_error_sdm.h), los que de verdad salen aqui.
ERRORES_NORDIC = {
    0x0000: "NRF_SUCCESS",
    0x0001: "NRF_ERROR_SVC_HANDLER_MISSING",
    0x0002: "NRF_ERROR_SOFTDEVICE_NOT_ENABLED",
    0x0003: "NRF_ERROR_INTERNAL",
    0x0004: "NRF_ERROR_NO_MEM (a la RAM le falta sitio: mirar el dato 7)",
    0x0005: "NRF_ERROR_NOT_FOUND",
    0x0006: "NRF_ERROR_NOT_SUPPORTED",
    0x0007: "NRF_ERROR_INVALID_PARAM",
    0x0008: "NRF_ERROR_INVALID_STATE (ya estaba arrancado / estado raro)",
    0x0009: "NRF_ERROR_INVALID_LENGTH",
    0x000A: "NRF_ERROR_INVALID_FLAGS",
    0x000B: "NRF_ERROR_INVALID_DATA",
    0x000C: "NRF_ERROR_DATA_SIZE",
    0x000D: "NRF_ERROR_TIMEOUT",
    0x000E: "NRF_ERROR_NULL",
    0x000F: "NRF_ERROR_FORBIDDEN",
    0x0010: "NRF_ERROR_INVALID_ADDR (direccion mal alineada o nula)",
    0x0011: "NRF_ERROR_BUSY",
    0x1001: "NRF_ERROR_SDM_INCORRECT_INTERRUPT_CONFIGURATION (prioridad de interrupcion ilegal)",
    0x1002: "NRF_ERROR_SDM_INCORRECT_CLENR0",
    0x1003: "NRF_ERROR_SDM_LFCLK_SOURCE_UNKNOWN",
    0x2001: "NRF_ERROR_SOC_NVIC_INTERRUPT_NOT_AVAILABLE",
    0x2002: "NRF_ERROR_SOC_NVIC_INTERRUPT_PRIORITY_NOT_ALLOWED",
    0x2003: "NRF_ERROR_SOC_NVIC_SHOULD_NOT_RETURN",
}


def explica(codigo):
    return ERRORES_NORDIC.get(codigo, "(codigo no catalogado)")


# --------------------------------------------------------------------------
# Leer el fichero (UF2 o binario crudo de la pagina)
# --------------------------------------------------------------------------
def memoria_de_uf2(ruta):
    """Devuelve {direccion: byte} de TODOS los bloques de un UF2."""
    with open(ruta, "rb") as f:
        bruto = f.read()
    if len(bruto) % 512 != 0:
        raise ValueError("%s no es un UF2 (%d bytes, no multiplo de 512)" % (ruta, len(bruto)))
    mem = {}
    for i in range(0, len(bruto), 512):
        b = bruto[i:i + 512]
        m1, m2, _flags, dir_, tam = struct.unpack_from("<5I", b, 0)
        if m1 != 0x0A324655 or m2 != 0x9E5D5157:
            continue
        if tam == 0 or tam > 476:
            continue
        for k in range(tam):
            mem[dir_ + k] = b[32 + k]
    return mem


def pagina(ruta, base=BASE):
    """Devuelve los 4096 bytes de la pagina `base`, de un UF2 o de un .bin crudo."""
    if ruta.lower().endswith(".uf2"):
        mem = memoria_de_uf2(ruta)
        datos = bytes(mem.get(base + i, 0xFF) for i in range(0x1000))
        # Aviso util: si la pagina entera sale a 0xFF, o es de otro firmware o el rastro no
        # llego a escribirse. Se dice, en vez de sacar una tabla vacia sin explicacion.
        return datos, mem
    with open(ruta, "rb") as f:
        datos = f.read()
    if len(datos) < 0x1000:
        raise ValueError("%s tiene %d bytes: no cabe ni una pagina de 4 KB" % (ruta, len(datos)))
    return datos[:0x1000], None


# --------------------------------------------------------------------------
# Decodificar
# --------------------------------------------------------------------------
def palabras(pag):
    return list(struct.unpack_from("<%dI" % (len(pag) // 4), pag, 0))


def textos(pag):
    """{paso: texto} recorriendo la zona de textos. Se para cuando el largo no es creible."""
    salida = {}
    off = 0
    while PASO_BASE + off + 4 <= len(pag):
        largo, paso = struct.unpack_from("<HH", pag, PASO_BASE + off)
        if largo == 0 or largo > 200:
            break
        if PASO_BASE + off + 4 + largo > len(pag):
            break
        txt = pag[PASO_BASE + off + 4: PASO_BASE + off + 4 + largo]
        try:
            salida.setdefault(paso, txt.decode("latin-1"))
        except Exception:  # noqa: BLE001
            pass
        off += 4 + ((largo + 3) & ~3)
    return salida


def describe_cobertura(ruta, base):
    """Dice que direcciones trae de verdad el fichero y si cubre la pagina pedida.

    PARA QUE: sin esto, un `CURRENT.UF2` de otra placa, de otra version del cargador o de una
    zona distinta se confunde con "no hay rastro", que es justo lo que no se quiere.
    """
    try:
        mem = memoria_de_uf2(ruta)
    except Exception:  # noqa: BLE001
        print("  (el fichero no es un UF2: no se pueden listar sus direcciones)")
        return
    if not mem:
        print("  (el UF2 no trae ni un bloque legible)")
        return
    ds = sorted(mem)
    print("  El fichero trae %d direcciones, de 0x%08X a 0x%08X." % (len(ds), ds[0], ds[-1]))
    cubre = (base in mem) and ((base + 0xFFF) in mem)
    print("  ¿Cubre la pagina 0x%04X entera? %s" % (base, "SI" if cubre else "NO"))


def informa(ruta, base=BASE):
    print("=" * 78)
    print("RASTRO DEL BANCO DE PRUEBAS DEL SOFTDEVICE")
    print("FICHERO: %s" % ruta)
    if not os.path.exists(ruta):
        print("  (no existe)")
        return 2

    pag, _ = pagina(ruta, base)
    p = palabras(pag)

    if p[0] != MAGIC:
        print()
        print("  La marca de la pagina (0x%04X) NO es la esperada (0x%08X)." % (base, MAGIC))
        print("  Eso significa una de estas tres cosas:")
        print("    a) el banco de pruebas no ha llegado a escribir todavia (arranco y no llego")
        print("       ni al primer paso), o")
        print("    b) esta pagina la ha reclamado el registro de viaje del firmware bueno, o")
        print("    c) el fichero que se ha pasado no cubre la direccion 0x%04X." % base)
        print()
        print("  Primeras palabras de la pagina: %s" %
              " ".join("%08X" % v for v in p[:8]))
        describe_cobertura(ruta, base)
        return 1

    # ★★ LA MARCA NO BASTA: SE COMPRUEBA TAMBIEN QUE EL PRIMER REGISTRO SEA LEGIBLE ★★
    # Leccion MEDIDA el 2026-09-17: en el firmware de fabrica de este nodo, la pagina 0xE7000
    # tiene casualmente las cuatro letras "GAIM" en la PRIMERA palabra (all� hay datos del
    # firmware que no son nuestros), asi que solo con la marca esta herramienta daba por bueno
    # un registro inventado (tipo 36, que no existe) y habria hecho perder el tiempo. Un lector
    # que se cree basura es peor que uno que dice "aqui no hay nada": por eso se exige que el
    # tipo del primer registro sea uno de los que escribe el banco de pruebas.
    primero = p[2] & 0xFFFF
    if primero not in (T_PASO, T_ERR, T_DATO, T_TEXTO, T_FALTA):
        print()
        print("  La marca 0x%08X esta ahi, pero el PRIMER REGISTRO no es del banco de pruebas"
              % MAGIC)
        print("  (tipo %d; los validos son 1..5). Es CASUALIDAD: la palabra de la marca coincide"
              % primero)
        print("  con otros datos de la flash.")
        print("  CONCLUSION: en esta pagina NO hay rastro del banco de pruebas.")
        print("  Primeras palabras: %s" % " ".join("%08X" % v for v in p[:8]))
        describe_cobertura(ruta, base)
        return 1

    arranques = p[1]
    print("  marca OK. ARRANQUES apuntados en esta pagina: %d" % arranques)
    print()

    regs = []
    for i in range(MAX_REG):
        cab = p[2 + i * 2]
        tipo = cab & 0xFFFF
        dato = (cab >> 16) & 0xFFFF
        valor = p[2 + i * 2 + 1]
        if tipo in (0, 0xFFFF):
            break
        regs.append((tipo, dato, valor))

    if not regs:
        print("  La pagina tiene marca pero NINGUN registro: escritura cortada nada mas empezar.")
        return 1

    txt = textos(pag)

    # --- lo primero: el fallo duro, si lo hubo, y donde se quedo la vez anterior ---
    falta = {}
    ultimoPaso = None
    sdEnableAnt = bleEnableAnt = ramAnt = None
    for tipo, dato, valor in regs:
        if tipo == T_PASO:
            ultimoPaso = dato
        elif tipo == T_FALTA:
            falta[dato] = valor
        elif tipo == T_DATO and dato == 21:
            ultimoPaso = valor
        elif tipo == T_DATO and dato == 22:
            sdEnableAnt = valor
        elif tipo == T_DATO and dato == 23:
            bleEnableAnt = valor
        elif tipo == T_DATO and dato == 24:
            ramAnt = valor

    print("-" * 78)
    print("LO QUE DEJO EL ARRANQUE ANTERIOR")
    print("-" * 78)
    if falta:
        print("  ★ HUBO UN FALLO DURO (HardFault) en el arranque anterior:")
        for k in sorted(falta):
            print("      %-6s = 0x%08X" % (FALTAS.get(k, "reg%d" % k), falta[k]))
        cfsr = falta.get(1, 0)
        if cfsr:
            print("      CFSR  = 0x%08X -> %s" % (cfsr, que_falta(cfsr)))
    else:
        print("  No hay registro de fallo duro.")
    if ultimoPaso is not None:
        print("  ULTIMO PASO al que llego: %d%s" %
              (ultimoPaso, (" (" + PASOS[ultimoPaso] + ")") if ultimoPaso in PASOS else ""))
        if ultimoPaso in txt:
            print("      y dejo escrito: \"%s\"" % txt[ultimoPaso])
    if sdEnableAnt is not None:
        print("  sd_softdevice_enable() de entonces = %d (0x%X) %s" %
              (sdEnableAnt, sdEnableAnt, explica(sdEnableAnt)))
    if bleEnableAnt is not None:
        print("  sd_ble_enable() de entonces          = %d (0x%X) %s" %
              (bleEnableAnt, bleEnableAnt, explica(bleEnableAnt)))
    if ramAnt:
        print("  RAM que pedia entonces               = 0x%08X" % ramAnt)
    print()

    print("-" * 78)
    print("TODOS LOS REGISTROS (en orden de escritura)")
    print("-" * 78)
    for tipo, dato, valor in regs:
        if tipo == T_PASO:
            print("  PASO  %-3d %s" % (dato, PASOS.get(dato, "")))
            if dato in txt:
                print("        \"%s\"" % txt[dato])
        elif tipo == T_ERR:
            marca = "  OK " if valor == 0 else " FALLO"
            print("  %s %-45s = %d (0x%X) %s" %
                  (marca, ERRORES.get(dato, "err%d" % dato), valor, valor, explica(valor)))
        elif tipo == T_DATO:
            nombre = DATOS.get(dato, "dato %d" % dato)
            extra = ""
            if dato == 1:
                extra = "  <- 0x0123 = S140 7.3.0 | 0x0100 = S140 7.2.0 | 0x00B6 = S140 6.1.1"
            elif dato in (2, 12):
                extra = "  (0x%X)" % valor
            elif dato in (6, 7, 24):
                extra = "  (0x%08X)" % valor
            elif dato == 4 or dato == 15:
                extra = "  -> %d.%d.%d" % (valor // 1000000, (valor // 1000) % 1000, valor % 1000)
            print("  DATO  %-45s = %d%s" % (nombre, valor, extra))
        elif tipo == T_FALTA:
            print("  FALTA %-6s = 0x%08X" % (FALTAS.get(dato, "reg%d" % dato), valor))
        elif tipo == T_TEXTO:
            print("  TEXTO paso %-3d: %s" % (dato, txt.get(dato, "(no legible)")))
        else:
            print("  ?     tipo %d dato %d valor 0x%08X" % (tipo, dato, valor))
    print()
    print("  --- fin: %d registros ---" % len(regs))
    return 0


def que_falta(cfsr):
    """Traduce el CFSR (registro de causa del fallo duro) a algo legible."""
    partes = []
    if cfsr & (1 << 25):
        partes.append("DIVBYZERO (division por cero)")
    if cfsr & (1 << 24):
        partes.append("UNALIGNED (acceso desalineado)")
    if cfsr & (1 << 19):
        partes.append("IBUSERR (fallo al buscar la instruccion)")
    if cfsr & (1 << 17):
        partes.append("PRECISERR (fallo preciso en el acceso a datos)")
    if cfsr & (1 << 16):
        partes.append("IMPRECISERR (fallo impreciso: suele ser una escritura a una direccion "
                      "que no existe)")
    if cfsr & (1 << 15):
        partes.append("BFARVALID (BFAR dice la direccion que fallo)")
    if cfsr & (1 << 10):
        partes.append("STKERR (fallo al apilar en la excepcion: pila mal)")
    if cfsr & (1 << 9):
        partes.append("UNSTKERR (fallo al desapilar)")
    if cfsr & (1 << 8):
        partes.append("IMPRECISERR en el retorno")
    if cfsr & (1 << 1):
        partes.append("VECTTBL (fallo leyendo la tabla de vectores)")
    return ", ".join(partes) if partes else "(sin bits conocidos)"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("uso: python tools\\lee_rastro.py <CURRENT.UF2|pagina.bin> [otro ...]")
        print("     (opcional) --base 0xE7000   para leer otra pagina")
        return 1
    base = BASE
    args = []
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "--base" and i + 1 < len(sys.argv):
            base = int(sys.argv[i + 1], 16)
            i += 2
            continue
        args.append(sys.argv[i])
        i += 1
    codigo = 0
    for r in args:
        codigo |= informa(r, base)
    return codigo


if __name__ == "__main__":
    sys.exit(main())
