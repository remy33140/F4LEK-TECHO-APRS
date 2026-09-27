# Procedimiento para los T-Echo (sesión con los amigos)

> ⚠️ **ESTO NO ES NORMATIVO. Es material de consulta** (una receta para una tarde concreta).
> Si contradice al código o a lo que se vea en la placa, **gana la placa**.

**Para qué**: grabar nuestro firmware Kacho System en un **LilyGO T-Echo** y en un
**T-Echo Plus**, **después de copiar lo que traían**, y verificar que funcionan.
**Fecha**: preparado el 2026-09-14 · **Hardware**: `docs/HARDWARE_TECHO.md`

---

## 0-bis. ANTES DE GRABAR: ¿qué SoftDevice lleva tu placa? (esto es crítico)

El **SoftDevice** es el firmware de radio que va **debajo** del nuestro y ocupa la parte baja de la
memoria. Según su versión, **nuestro firmware tiene que empezar en una dirección distinta**:

| Versión del SoftDevice | La aplicación empieza en | Entorno que hay que compilar |
|---|---|---|
| **S140 6.1.1** | `0x26000` | `techo` / `techo_plus` |
| **S140 7.x** (7.2.0, 7.3.x) | `0x27000` | **`techo_s140v7` / `techo_plus_s140v7`** |

**Cómo se mira**: con la placa en modo grabación, abre el fichero **`INFO_UF2.TXT`** de la unidad.
La línea `SoftDevice:` lo dice.

> **⚠️ Si te equivocas de entorno** (grabas un binario de la v6 en una placa con v7), el firmware
> empieza en `0x26000` y **pisa los últimos 4 KB del SoftDevice**. Lo más probable es que el
> cargador de arranque lo rechace, pero si lo aceptara **la radio quedaría inservible**.
> Comprobado el 2026-09-14 con un T-Echo Plus real (el de EA2KR, con S140 **7.2.0**): el binario de
> la v7 sale con la primera dirección en `0x27000`, verificado leyendo el propio `.uf2`.

Para comprobar **qué dirección lleva un `.uf2`** sin grabarlo:

```powershell
$b=[System.IO.File]::ReadAllBytes("<ruta>\firmware.uf2")
"empieza en 0x{0:X}" -f [System.BitConverter]::ToUInt32($b,12)
```

## 0-ter. El cargador de arranque del T-Echo puede ser viejo (y da igual)

El T-Echo de EA2KR llevaba el **cargador de arranque 0.6.1 (octubre de 2021)**, no el 0.10.0 del
nodo de casa. **No es un problema para grabar** (la unidad de memoria y `CURRENT.UF2` funcionan
igual, y el respaldo se hace igual), pero conviene saberlo: **no todas las placas de esta familia
llevan el mismo cargador**, así que las instrucciones de botones pueden variar ligeramente de una
unidad a otra. Si el doble toque no entra, probar la combinación de botones que traiga esa unidad.

---

## 0. Lo que hay que tener claro ANTES de empezar

| Cosa | Estado |
|---|---|
| Los dos entornos compilan | ✅ `techo` y `techo_plus` (35,5 % de flash, sin errores) |
| **Copia de seguridad antes de grabar** | **OBLIGATORIA** y es lo primero (§1) |
| Radio, GPS, batería, sensores, config, registro, digi, tracker | ✅ portados |
| **Pantalla** | ❌ **NO**. Llevan tinta electrónica, que necesita su propia capa de dibujo |
| IMU BHI260AP y motor/zumbador del Plus | ❌ no se usan (el IMU necesita firmware propio; ver `HARDWARE_TECHO.md` §9) |
| Grabado | por el **bootloader de LilyGO** (unidad `TECHOBOOT`), no por `nrfutil` |

**Lo que se va a ver**: los nodos funcionarán **sin pantalla**. Todo lo demás igual que
en una Faketec: repetidor, rastreador, balizas, mensajes, meteorología, registro de viaje
y configuración por USB. La pantalla se hará en su propia sesión.

---

## 0-quater. ¿Se puede sacar el indicativo de la copia de seguridad? NO (y por qué)

**Comprobado el 2026-09-14 con el T-Echo de EA2KR** (que llevaba el firmware de cfr34k, DL5TKL):

- El fichero `CURRENT.UF2` que da el cargador de arranque **copia de 0x1000 a 0xEA000**: el
  SoftDevice y la aplicación.
- La **configuración** de ese firmware (donde va el indicativo) **no vive ahí**: el firmware del
  alemán la guarda con el sistema FDS de Nordic en **0xF0000-0xF2FFF**, que queda **fuera** de lo
  que copia el `.uf2`.
- Por eso **la palabra `EA2KR` no aparece ni una vez** en el respaldo (comprobado buscándola). Lo que
  sí aparece son las cadenas del código: `APLETK` (su *tocall*), `APLRT1` y `APRSPH`.

**Conclusión honesta: el respaldo NO permite sacar el indicativo.** No es que no lo hayamos
encontrado: es que no está en el fichero. Y los ajustes del cargador (en 0xFF000) tampoco están.

**Lo que sí se puede hacer, y es más simple**: el indicativo lo sabe el dueño del aparato (es su
licencia). Al grabar nuestro firmware, se pone en un segundo desde el configurador web o con un
comando. Nuestro firmware **no lleva ningún indicativo grabado a propósito**: la configuración vive
en el nodo y la pone quien lo usa.

> **Nota técnica para no confundirse después**: nuestro firmware guarda la configuración en
> `0xE8000`/`0xE9000` y el registro de viaje en `0xC8000-0xE7FFF`. Los ajustes del firmware viejo
> siguen en la flash (en `0xF0000`) pero **nadie los lee**: no molestan ni se mezclan.
---

## 1. PRIMERO: copia de seguridad de cada placa (no se salta)

El bootloader del T-Echo expone un fichero **`CURRENT.UF2`** que es **la copia entera de
lo que lleva grabado** la placa. Se guarda **antes** de tocar nada.

1. Conectar la placa con un cable **USB-A a USB-C** (con USB-C a USB-C algunas no se
   alimentan: lo dice el propio fabricante).
2. **Doble toque al botón de reset** (arriba a la izquierda), hasta que aparezca una
   unidad llamada **`TECHOBOOT`**. Si no sale a la primera, insistir.
3. Ejecutar:

```
powershell -File tools\copia_techo.ps1
```

4. El script dice la carpeta donde ha guardado la copia (`data\rescue\`) y comprueba con
   **SHA256** que es idéntica al original. **Si dice que no coinciden, no seguir.**
5. **Renombrar la copia** para saber de qué placa es: por ejemplo
   `techo_original_20260915-1900_NORMAL.uf2` y `..._PLUS.uf2`.
   (El script no puede saber cuál es cuál: eso lo sabéis vosotros.)

> **Si algo sale mal después, se vuelve atrás copiando esa copia a la unidad `TECHOBOOT`.**

---

## 2. Grabar nuestro firmware

1. Comprobar que se ha hecho la copia del §1. **Sin copia, no se graba.**
2. Volver a poner la placa en modo grabación (doble toque al reset → unidad `TECHOBOOT`).
3. Copiar el fichero que corresponda **a la unidad `TECHOBOOT`**:

| Placa | Fichero |
|---|---|
| T-Echo normal | `.pio\build\techo\firmware.uf2` |
| T-Echo Plus | `.pio\build\techo_plus\firmware.uf2` |

4. Esperar a que la unidad desaparezca: la placa se reinicia sola.
5. **Ojo**: los dos ficheros son **idénticos en tamaño** y el pinout es el mismo. La
   diferencia (`TEchoPlus`) solo sirve para cuando usemos el IMU, el motor o el zumbador.
   O sea: **si alguien cruza los ficheros, no se rompe nada.**

---

## 3. Verificar que funciona (por radio, que es lo que no engaña)

Igual que con el nodo de casa: **lo que manda por radio es la prueba**.

1. **Configurar** cada nodo antes de que salga a la calle: indicativo propio de cada
   amigo, su SSID, coordenadas de su casa y modo de trabajo. Se hace con el configurador
   web (`web\index.html`, Chrome/Edge) o por consola USB.
2. **Comprobar en la iGate de casa** (`http://192.168.3.236/received-packets.json`):
   que aparezcan sus tramas. **Las horas son UTC**; el reloj local va +2 h.
3. **Que aparezca `T#000`**: es la telemetría del arranque limpio, igual que en el nodo
   de casa.
4. **Comprobar la batería**: `bat` por consola. Con una LiPo de 1 celda lo normal son
   **3,7-4,2 V**. Si sale un número raro (por ejemplo el doble, o 0), el pin o el divisor
   del ADC no son los que creemos: **avisar y no seguir**, porque de eso depende el sueño.
5. **Comprobar los sensores**: `status` debe decir `sensors.wxChip = BME280`. Si dice
   `-`, el BME280 no ha respondido: mirar que el nodo tenga el MOSFET de periferia en
   alto (lo hace el firmware al arrancar) y volver a mirar.
6. **Probar el GPS**: en modo rastreador, que fije y que empiece a mandar posiciones.
   Ojo: **Quectel L76K, no u-blox**, así que si el GPS no fija, antes de culpar al
   firmware mirar antena y cielo (el módulo va con antena propia).
7. **Probar el repetidor**: con el otro T-Echo al lado, que uno repita al otro.

---

## 4. Qué NO va a funcionar (y no es un fallo)

- **La pantalla no se enciende.** No hay capa de dibujo para tinta electrónica todavía.
  El firmware ni siquiera toca el chip de la pantalla (y deja sus pines en entrada al
  apagarse, para no gastar batería).
- **El IMU, el motor háptico y el zumbador del Plus** no se usan.
- **El botón táctil** no hace nada (nuestro menú vive en la pantalla, y aquí no hay).
  **El botón que funciona es el de abajo a la izquierda** (`P1.10`), que es el que usa
  nuestro código.
- **El sueño por batería baja está activo con umbrales de LiPo** (apagar a 3200 mV,
  despertar a 3400 mV). Si preferís que no se duerman mientras probáis, se apaga desde
  el configurador (`sleepCutMv` muy bajo) o se deja enchufado al USB.

---

## 5. Lo que NO se debe hacer

- **No grabar sin haber hecho la copia.** Es lo único irreversible de la tarde.
- **No usar un cable USB-C a USB-C** para entrar en modo grabación (el fabricante avisa
  de que puede no alimentarse).
- **No tocar `C:\NavaTastic Codigo completo` ni `MeshKachoUtility`**, y **no abrir el
  puerto serie de la iGate de casa** (la reinicia).
- **No cambiar los pines "para probar"**: están sacados de tres fuentes que coinciden
  (el firmware del alemán que corre en esa placa, la tabla oficial de LilyGO y
  Meshtastic). Si algo no funciona, **medir antes de tocar**.

---

## 6. Si hay que volver atrás

1. Doble toque al reset → unidad `TECHOBOOT`.
2. Copiar allí la copia del §1.
3. Esperar a que se reinicie. La placa vuelve a como estaba.
