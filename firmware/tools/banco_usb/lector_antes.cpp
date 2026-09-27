/* lector_antes.cpp — LA VERSION ANTERIOR, transcrita de git.
 *
 * QUE ES: el `ConfigProtocol::feed()` que habia en `src/protocol.cpp` antes del cambio
 * "T-Echo Project Butter II", copiado linea a linea de git y adaptado solo en los tipos
 * (el `String lineBuf_` de Arduino pasa a ser un `char lineBuf[]`, que es lo mismo para lo
 * que se mide: acumular caracteres hasta el LF con el tope de 4096).
 *
 * PARA QUE: que el banco pueda comparar ANTES y DESPUES con el MISMO guion y el MISMO
 * modelo de bucle. Es el hermano de `button_antes.cpp` del banco del boton.
 *
 * LO QUE NO HABIA ENTONCES (y por eso estas funciones estan vacias):
 *   - `transporteBombea()`: el driver de la tinta no leia el USB en sus esperas.
 *   - `transporteCobra()`: al acabar el repintado tampoco se leia el USB; el puerto se
 *     miraba solo en la cabecera de la vuelta del bucle.
 *   - `transporteEncoladoMax()`: no habia anillo, no habia nada que encolar.
 *
 * License: GPL-3.0
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Lo que el banco pone a disposicion del transporte (ver banco_usb.cpp).
int bancoDisponible(void *ctx);
int bancoLee(void *ctx);
bool bancoByteAlTnc(void *ctx, uint8_t b);
void bancoLinea(void *ctx, const char *s, size_t n);

namespace {

const size_t kMaxLine = 4096;   // el tope que declaraba protocol.h
char lineBuf[kMaxLine + 1];
size_t lineLen = 0;

// ★★ ESTO ES EL `feed()` DE ANTES, TAL CUAL ★★
void feed() {
  while (bancoDisponible(nullptr) > 0) {
    const uint8_t b = (uint8_t)bancoLee(nullptr);
    // Byte-stream coexistence: 0xC0 es el FEND del KISS: desde ese byte todo va a la
    // maquina de estados del KISS hasta el FEND de cierre; cualquier otro byte es texto.
    if (bancoByteAlTnc(nullptr, b)) {
      lineLen = 0;  // una linea de texto a medias muere con la trama binaria
      continue;
    }
    const char c = (char)b;
    if (c == '\r') continue;   // tolerar CRLF
    if (c == '\n') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        bancoLinea(nullptr, lineBuf, lineLen);
      }
      lineLen = 0;
      continue;
    }
    if (lineLen < kMaxLine) lineBuf[lineLen++] = c;
  }
}

}  // namespace

void transporteInit() { lineLen = 0; }

// La cabecera del bucle: `gProtocol.feed(Serial)`.
void transporteFeed() { feed(); }

// El driver de la tinta: antes NO se leia el USB aqui.
void transporteBombea() {}

// Al acabar el repintado: antes no se leia el USB aqui tampoco.
void transporteFinPintado() {}
void transporteCobra() {}

// No habia anillo de bytes: no hay maximo que dar.
uint32_t transporteEncoladoMax() { return 0; }
