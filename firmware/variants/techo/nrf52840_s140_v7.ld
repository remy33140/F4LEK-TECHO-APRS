/* Guion de enlazado para nRF52840 con SoftDevice S140 **version 7**.
 *
 * POR QUE HACE FALTA: el framework de Adafruit solo trae el guion de la S140 v6
 * (`nrf52840_s140_v6.ld`, aplicacion en 0x26000). Las placas que llevan grabada la
 * S140 v7 (como el LilyGO T-Echo Plus de EA2KR, con S140 7.2.0) tienen el SoftDevice
 * un poco mas grande, asi que **la aplicacion empieza en 0x27000**.
 *
 * Si se grabara un binario enlazado para la v6, empezaria en 0x26000 y pisaria los
 * ultimos 4 KB del SoftDevice: o el bootloader lo rechaza, o se lo come y deja la
 * radio inservible. Por eso hay un entorno aparte para estas placas.
 *
 * DE DONDE SALEN LOS NUMEROS (no inventados):
 *   - 0x27000: lo usa el guion de Meshtastic para S140 v7
 *     (`src/platform/nrf52/nrf52840_s140_v7.ld`, comprobado el 2026-09-14) y tambien
 *     el guion de la v7 que SI trae el framework para el nRF52833.
 *   - 0xED000 como final de la zona de aplicacion y 0x20006000 de RAM: los mismos que
 *     el guion de la v6 del framework, para que la unica diferencia sea el inicio.
 */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx)     : ORIGIN = 0x27000, LENGTH = 0xED000 - 0x27000

  /* ★★★ LA RAM, CORREGIDA EL 2026-09-17 (b33) — AQUI ESTABA EL BUCLE ★★★
   * Antes ponia 0x20006000 "porque es lo que reserva el framework para el nRF52840", y ese
   * fue el error: ese numero es el de la **S140 v6**. El SoftDevice v7 de esta placa se queda
   * menos RAM, y su zona empieza mas abajo. Con la aplicacion enlazada en 0x20006000, sus
   * variables caian DENTRO de la RAM del SoftDevice: con el Bluetooth apagado no pasaba nada,
   * y **al arrancarlo el SoftDevice escribia encima y el nodo se reiniciaba en bucle** — justo
   * lo medido en la placa con el b32 (y con el b25).
   *
   * EL NUMERO BUENO, y de donde sale: **0x20004260**, el mismo que usa el firmware del T-Echo
   * que SI arranca Bluetooth en este hardware (`_referencias\t-echo-lora-aprs\t-echo.ld`,
   * `RAM (rwx) : ORIGIN = 0x20004260`, con la S140 7.2.0 grabada junto a la aplicacion).
   *
   * ★ ES UN NUMERO A PROBAR, NO UN DATO SEGURO: el trozo exacto de RAM que se queda el
   *   SoftDevice depende de como este construido el suyo. Si algun dia el Bluetooth no
   *   arranca en una placa con este guion, esto es lo primero que hay que mirar (el arranque
   *   de la aplicacion devuelve por donde empieza la RAM libre). */
  RAM (rwx) :  ORIGIN = 0x20004260, LENGTH = 0x20040000 - 0x20004260
}

SECTIONS
{
  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM

  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
}

/* ★ NOTA (b38, 2026-09-17): aqui se intento meter una zona `.noinit` para que el nodo
 *   recordara el fallo en RAM que no se borra. NO SE PUDO, y el porque queda escrito para no
 *   repetirlo:
 *     - el arranque del core (`gcc_startup_nrf52840.S`, lineas 223-245) **copia datos desde la
 *       flash hasta `__bss_start__`**, o sea que todo lo que quede ENTRE `.data` y `.bss` se
 *       reescribe en cada arranque (la "memoria" no sobreviviria y no se notaria);
 *     - y si se coloca DESPUES de `.bss`, el enlazador se lia con `.heap` (que alinea el
 *       contador con `__StackLimit`) y falla con "cannot move location counter backwards".
 *
 *   LA SOLUCION QUE SI FUNCIONA esta en `src/ble_kiss.cpp`: los registros **`GPREGRET`** del
 *   nRF52840, que no son RAM (el chip los conserva al reiniciar) y no necesitan tocar el
 *   enlazador. Alli se apunta en que paso se iba, y si el Bluetooth llego a estar ARRIBA. */

INCLUDE "nrf52_common.ld"
