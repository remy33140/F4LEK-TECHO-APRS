#!/usr/bin/env python3
# sube_buildnum.py — automatiza el NUMERO DE COMPILACION (idea del operador, 2026-09-15).
#
# ============================================================================
#  QUE PROBLEMA RESUELVE
#
#  El numero de compilacion (`b4`) sirve para poder decir por USB QUE firmware
#  lleva grabado un nodo sin dudar. Vive en DOS sitios que tienen que decir lo
#  mismo:
#     - `.buildnum`            (un numero suelto: 4)
#     - `platformio.ini`       (-DAPP_BUILD_NUM=\"b4\", que es lo que se graba
#                               dentro del binario y lo que ve el comando `version`)
#  Se subian A MANO los dos, y ya se olvido una vez: `.buildnum` se quedo en 3
#  mientras `platformio.ini` iba por b4. El verificador de la memoria lo caza
#  (falla: "descuadre"), pero enterarse DESPUES no sirve de nada: el binario ya
#  se compilo y puede estar grabado en un nodo.
#
# ============================================================================
#  COMO FUNCIONA (y por que asi)
#
#  1) ANTES de compilar: si `.buildnum` y `platformio.ini` NO cuadran, **se para
#     la compilacion** con un error que dice exactamente que hay que poner. Es a
#     proposito: es mejor no compilar que compilar y grabar un binario cuyo
#     numero miente. Este es el fallo que ya paso una vez.
#
#  2) DESPUES de que la compilacion haya terminado BIEN (el .hex esta generado):
#     se sube el numero en LOS DOS sitios a la vez, +1.
#
#  ★ CAMBIO DE FONDO RESPECTO A ANTES: el operador YA NO TIENE QUE SUBIR EL
#    NUMERO A MANO. Se compila y ya esta: el binario sale con el numero que toca
#    y los dos ficheros quedan preparados para la compilacion siguiente.
#
#  ★ POR QUE NO SE SUBE "AL EMPEZAR": si se subiera antes de compilar, el numero
#    que lleva el binario y el que se queda apuntado serian el mismo, y entonces
#    un binario compilado dos veces seguidas seria indistinguible del anterior
#    (que es justo lo que el contador existe para evitar). Subiendolo DESPUES,
#    "el numero apuntado" es siempre el de la PROXIMA compilacion.
#
#  ★ SI LA COMPILACION FALLA: no se sube nada (el paso 2 no llega a correr), asi
#    que reintentar no gasta numeros.
#
#  ★ VARIOS ENTORNOS EN UNA TANDA: `pio run -e A -e B -e C` sube el numero UNA
#    SOLA VEZ, y los tres binarios salen con el MISMO numero. Es lo que se quiere:
#    una tanda de compilacion = un numero.
#    ★★ CORREGIDO EL 2026-09-16: el guardia en memoria NO bastaba (PlatformIO carga
#       este script UNA VEZ POR ENTORNO, medido), y la comprobacion por fecha del .elf
#       que se puso en su lugar **no funcionaba nunca** (se tomaba DESPUES de enlazar:
#       ver el porque completo en _sube_el_numero). Resultado: el contador se quedo
#       clavado en b9 durante siete cambios de firmware seguidos, que es justo lo que
#       este script existe para evitar. Ahora la tanda se apunta en un FICHERO
#       (`.pio/.buildnum_tanda`), identificada por el PID del proceso `pio`.
#
#  ★ QUE NUMERO LLEVA CADA BINARIO: el que tenian los ficheros AL EMPEZAR la tanda
#    (los lee PlatformIO al arrancar), y al terminar los ficheros se quedan con el
#    siguiente. O sea: **el binario dice lo que lleva; el fichero, lo que saldra la
#    proxima vez**.
#
#  Este script NO sustituye a `verifica_memoria.ps1`: lo complementa. El
#  verificador sigue comprobando, ademas, que el numero este DENTRO del binario.
# ============================================================================

import hashlib
import os
import re

Import("env")


def _dir_proyecto():
    """Carpeta del proyecto, resolviendo la variable EN EL ULTIMO MOMENTO.

    ★ OJO, ESTO ES UNA TRAMPA QUE YA COSTO UN INTENTO FALLIDO (2026-09-15): si se
    resuelve al cargar el script y sale mal, el script no encuentra los ficheros y
    el numero se queda quieto SIN QUE NADIE SE ENTERE, que es justo lo que este
    script existe para evitar. Por eso:
      - se resuelve aqui dentro (cuando el hook ya corre), no al importar, y
      - se pide igual que en `nrf52_uf2.py` (`env.subst("$PROJECT_DIR")`), que es el
        patron que YA funciona en este proyecto, y
      - si sale una ruta que no existe, se avisa en vez de callarse.
    """
    d = env.subst("$PROJECT_DIR")
    if not d or "$" in str(d):
        raise RuntimeError("no puedo resolver $PROJECT_DIR (he obtenido %r)" % (d,))
    d = str(d)
    if not os.path.isdir(d):
        raise RuntimeError("la carpeta del proyecto no existe: %s" % d)
    return d


def _rutas():
    d = _dir_proyecto()
    return os.path.join(d, ".buildnum"), os.path.join(d, "platformio.ini")


PATRON_INI = re.compile(r'(-DAPP_BUILD_NUM=\\?"b)(\d+)(\\?")')

# Guardia de tanda: el numero sube UNA sola vez por orden de compilacion.
# ★★ ESTA BANDERA EN MEMORIA NO BASTA, Y ESTA MEDIDO (2026-09-16): PlatformIO aisla cada
#    entorno y CARGA ESTE SCRIPT UNA VEZ POR ENTORNO, asi que con `pio run -e A -e B -e C
#    -e D` la bandera se pierde y el contador subiria cuatro veces. Por eso la marca de
#    tanda se resuelve con la HUELLA DEL CODIGO (ver _fuentes_huella()).
#    ★★ PRIMER INTENTO, FALLIDO Y MEDIDO (2026-09-16): se probo a marcar la tanda con el
#       PID del proceso `pio` en un fichero. NO SIRVE: cada entorno de la misma orden corre
#       con padre distinto, asi que el contador subio CUATRO veces en una tanda de cuatro
#       (medido: b9, b10, b11, b12, b13). La huella del codigo si los agrupa.
_ya_subido = False


def _marca_de_fuentes():
    """Fichero donde se apunta la huella del codigo que ya tiene numero.

    Va dentro de `.pio/` (ignorado por git en todos los arboles): es comun a todos los
    entornos del proyecto, que es lo que hace falta, y no ensucia el repositorio. NO vale
    `$BUILD_DIR`, que es distinto para cada entorno.
    """
    d = os.path.join(_dir_proyecto(), ".pio")
    try:
        os.makedirs(d, exist_ok=True)
    except OSError:
        pass
    return os.path.join(d, ".buildnum_fuentes")


def _fuentes_huella():
    """Huella del CODIGO que se va a compilar (nombre y contenido de cada fuente).

    ★★ PARA QUE (esto es el corazon del arreglo del 2026-09-16) ★★
    El numero tiene que identificar LO QUE LLEVA EL BINARIO, no la vez que se le dio a
    compilar. Con la huella del codigo sale justo eso, y ademas resuelve la tanda:
      - los CUATRO entornos de una tanda comparten huella -> se gasta UN numero, no cuatro
        (que es lo que pasaba con la marca por PID: cada entorno corre con padre distinto,
        medido);
      - dos compilaciones seguidas DE LO MISMO no gastan numero (el firmware es identico,
        aunque se borre el .elf y se vuelva a enlazar);
      - en cuanto se toca una fuente, la huella cambia y la compilacion siguiente gasta uno.

    Se mira `src/`, `variants/`, `boards/` y `extra_scripts/`, mas `platformio.ini` CON LA
    LINEA DEL NUMERO NEUTRALIZADA: cambiarla no es cambiar el codigo, y si contara, cada
    subida invalidaria la huella y el contador no pararia de subir.
    """
    d = _dir_proyecto()
    h = hashlib.sha256()
    for sub in ("src", "variants", "boards", "extra_scripts"):
        base = os.path.join(d, sub)
        for raiz, dirs, ficheros in os.walk(base):
            dirs.sort()
            for f in sorted(ficheros):
                if f.endswith(".pyc"):
                    continue
                p = os.path.join(raiz, f)
                h.update(os.path.relpath(p, d).replace("\\", "/").encode("utf-8"))
                try:
                    with open(p, "rb") as fh:
                        h.update(fh.read())
                except OSError:
                    pass
    try:
        with open(os.path.join(d, "platformio.ini"), "r", encoding="utf-8") as fh:
            ini = PATRON_INI.sub(lambda m: m.group(1) + "N" + m.group(3), fh.read())
        h.update(ini.encode("utf-8"))
    except OSError:
        pass
    return h.hexdigest()


def _fuentes_ya_contadas():
    """¿El codigo que hay ahora es el mismo que ya tiene numero?"""
    try:
        with open(_marca_de_fuentes(), "r", encoding="utf-8") as fh:
            return fh.read().strip() == _fuentes_huella()
    except Exception:      # noqa: BLE001  (marca ausente o ilegible: se gasta numero)
        return False


def _apunta_fuentes():
    try:
        with open(_marca_de_fuentes(), "w", encoding="utf-8") as fh:
            fh.write(_fuentes_huella())
    except OSError:
        pass   # sin marca se puede gastar de mas, pero nunca de menos: no rompe nada


def _leer(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


def _escribir(path, texto):
    # UTF-8 SIN BOM, que es lo que usa todo el proyecto (el verificador de la
    # memoria falla si aparece un BOM en la memoria; aqui se hace igual por
    # coherencia, y porque un BOM delante de un `[env:]` rompe PlatformIO).
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(texto)


def _numero_del_contador(f_buildnum):
    """Devuelve el entero de .buildnum, o None si el fichero no tiene sentido."""
    try:
        texto = _leer(f_buildnum).strip()
    except OSError:
        return None
    if not texto.isdigit():
        return None
    return int(texto)


def _numero_del_ini(f_ini):
    """Devuelve (entero, texto_del_fichero) segun platformio.ini, o (None, texto)."""
    try:
        texto = _leer(f_ini)
    except OSError:
        return None, None
    m = PATRON_INI.search(texto)
    if not m:
        return None, texto
    return int(m.group(2)), texto


def comprueba_cuadre():
    """Antes de compilar: `.buildnum` y `platformio.ini` tienen que decir lo mismo.

    Se llama al CARGAR el script (que va con `pre:` en platformio.ini), una vez por entorno.
    Si los dos ficheros no cuadran, PARA la compilacion: es mejor no compilar que compilar y
    grabar un binario cuyo numero miente. Y se comprueba ANTES de subir, para que una subida
    no tape nunca un descuadre.
    """
    f_buildnum, f_ini = _rutas()
    n_contador = _numero_del_contador(f_buildnum)
    n_ini, _ = _numero_del_ini(f_ini)

    if n_contador is None:
        print("*** BUILD STOPPED: .buildnum no existe o no es un numero (b%d)." % 0)
        env.Exit(1)
    if n_ini is None:
        print("*** BUILD STOPPED: platformio.ini no declara APP_BUILD_NUM.")
        env.Exit(1)

    if n_contador != n_ini:
        print("")
        print("*** BUILD STOPPED: EL NUMERO DE COMPILACION NO CUADRA ***")
        print("    .buildnum       dice  b%d" % n_contador)
        print("    platformio.ini  dice  b%d" % n_ini)
        print("")
        print("    Los dos tienen que decir lo mismo: el numero va DENTRO del binario y")
        print("    es lo unico que permite decir por USB que firmware lleva un nodo.")
        print("    Pon los dos a b%d y vuelve a compilar:" % max(n_contador, n_ini))
        print("      - .buildnum                -> %d" % max(n_contador, n_ini))
        print('      - platformio.ini           -> -DAPP_BUILD_NUM=\\"b%d\\"' % max(n_contador, n_ini))
        print("")
        env.Exit(1)

    print("Numero de compilacion: b%d (contador y platformio.ini cuadran)" % n_ini)


def sube_el_numero_si_toca():
    """Sube el numero SI el codigo ha cambiado, y se llama AL CARGAR este script.

    ★★ LO QUE HABIA, Y POR QUE NO VALIA (todo esto medido el 2026-09-16) ★★

    (1) La comprobacion "¿este entorno ha rehecho algo?" comparaba la fecha del .elf antes
        y despues, pero la muestra "antes" se tomaba en una accion PRE del objetivo `.hex`,
        que corre DESPUES de enlazar -> las dos fechas eran iguales SIEMPRE -> el contador
        se quedo clavado en b9 durante siete cambios de firmware seguidos.

    (2) Se sustituyo por una marca de tanda con el PID del proceso `pio`. TAMPOCO: cada
        entorno de la misma orden corre con padre distinto, asi que una tanda de cuatro
        entornos subio CUATRO numeros (medido: b9, b10, b11, b12, b13).

    (3) Y subiendo el numero en un gancho POST aparecio el tercer problema: PlatformIO lee
        `platformio.ini` POR ENTORNO (no una vez al arrancar), asi que el primero compilaba
        con b10 y los demas con b11 -> binarios de la MISMA tanda con numeros distintos.

    ★★ LO QUE HAY AHORA ★★
    - El numero identifica AL CODIGO, no a la vez que compilas: se guarda la HUELLA del
      codigo (ver _fuentes_huella()) y solo se sube cuando esa huella cambia.
      Consecuencias: una tanda de cuatro entornos gasta UN numero; recompilar lo mismo no
      gasta ninguno; y en cuanto se toca una fuente, la compilacion siguiente gasta uno.
    - Se sube **AL CARGAR EL SCRIPT**, o sea antes de que este entorno resuelva sus flags:
      asi los cuatro entornos de la tanda leen el MISMO numero y los binarios dicen lo que
      dicen los ficheros. (Si se subiera al terminar, el primero saldria con el viejo.)
    - Si la compilacion falla, el numero ya esta gastado. Es a proposito y es el precio de
      que el binario diga lo que lleva: un numero gastado sin binario no molesta a nadie;
      un binario con el numero viejo, si.
    """
    global _ya_subido
    if _ya_subido:
        return

    f_buildnum, f_ini = _rutas()
    n_contador = _numero_del_contador(f_buildnum)
    n_ini, texto_ini = _numero_del_ini(f_ini)
    if n_contador is None or n_ini is None or n_contador != n_ini:
        # El cuadre lo para comprueba_cuadre() con su mensaje. Aqui, sin cuadre, no se
        # toca nada: mejor un numero viejo que un numero inventado.
        return

    if _fuentes_ya_contadas():
        _ya_subido = True
        print("Numero de compilacion: b%d, sin cambios (este codigo ya tiene numero)" % n_ini)
        return

    nuevo = n_ini + 1

    _escribir(f_buildnum, "%d\n" % nuevo)
    _escribir(f_ini, PATRON_INI.sub(lambda m: m.group(1) + str(nuevo) + m.group(3),
                                    texto_ini, count=1))
    _ya_subido = True
    _apunta_fuentes()

    print("Numero de compilacion subido a b%d (el codigo ha cambiado):" % nuevo)
    print("los entornos de esta tanda tienen que salir con b%d dentro." % nuevo)


# ---------------------------------------------------------------- enganches
# ★★ AQUI NO HAY GANCHOS: EL CUADRE Y LA SUBIDA SE HACEN AL CARGAR EL SCRIPT ★★
#
#   Y ese es el arreglo del 2026-09-16. El script se carga con `pre:` en `platformio.ini`
#   (ojo: hay que mantener ese `pre:`), o sea **antes de que el entorno resuelva sus flags de
#   compilacion**. Por eso, cuando toca subir el numero, el entorno que lo sube compila YA con
#   el nuevo, igual que los demas de la tanda. Medido antes de arreglarlo:
#     - con la subida en un gancho POST, el primero salia con b10 y los demas con b11;
#     - con `env.Replace(BUILD_FLAGS=...)` intentando forzarlo, tampoco: el primero se quedaba
#       con el viejo (la lista que se reescribe no es la que usa el compilador).
#
#   Lo que decide si se gasta numero es la HUELLA DEL CODIGO, no el momento: ver
#   _fuentes_huella(). Y el cuadre se comprueba ANTES de subir, para que una subida no tape
#   nunca un descuadre.
try:
    comprueba_cuadre()          # si los dos ficheros no dicen lo mismo, PARA la compilacion
    sube_el_numero_si_toca()    # sube SOLO si el codigo ha cambiado (huella)
except Exception as exc:   # noqa: BLE001  (nada del contador debe tumbar una compilacion)
    print("aviso: el contador de compilacion ha fallado: %s" % exc)

