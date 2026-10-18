// nvm.cpp — ver nvm.h (el porque esta alli).
// License: GPL-3.0

#include "nvm.h"

#include <Arduino.h>
#include <string.h>

#include "nrf_error.h"
#include "nrf_sdm.h"
#include "nrf_soc.h"

namespace {

constexpr uint32_t kPage = 4096;
constexpr uint32_t kOpTimeoutMs = 2000;  // una operacion encolada en el SoftDevice
constexpr int kIntentos = 3;
constexpr size_t kTrozoPalabras = 256;   // 1 KB por llamada a sd_flash_write

// El SoftDevice lee el origen MIENTRAS escribe (asincrono): tiene que vivir en RAM y no
// moverse hasta que acabe. Por eso se copia aqui y no se usa el puntero de quien llama.
uint32_t gTrozo[kTrozoPalabras];

bool sdArriba() {
  uint8_t en = 0;
  (void)sd_softdevice_is_enabled(&en);
  return en != 0;
}

void nvmcWaitReady() {
  while (!NRF_NVMC->READY) {
  }
}

bool paginaBorrada(uint32_t addr) {
  const uint32_t *p = (const uint32_t *)addr;
  for (uint32_t i = 0; i < kPage / 4; i++) {
    if (p[i] != 0xFFFFFFFFu) return false;
  }
  return true;
}

// Pide la operacion al SoftDevice hasta que la acepte (NRF_ERROR_BUSY = hay otra en curso)
// y espera a que `hecho()` sea cierto. El evento de fin lo consume el stack de Bluefruit,
// asi que la forma de saber que acabo es mirar la propia flash.
template <typename Pide, typename Hecho>
bool operacionSd(Pide pide, Hecho hecho) {
  for (int intento = 0; intento < kIntentos; intento++) {
    uint32_t t0 = millis();
    uint32_t err;
    while ((err = pide()) == NRF_ERROR_BUSY) {
      if (millis() - t0 > kOpTimeoutMs) return false;
      delay(1);
    }
    if (err != NRF_SUCCESS) return false;
    t0 = millis();
    while (millis() - t0 < kOpTimeoutMs) {
      if (hecho()) return true;
      delay(1);
    }
  }
  return false;
}

}  // namespace

bool nvmErasePage(uint32_t addr) {
  if (sdArriba()) {
    return operacionSd([&] { return sd_flash_page_erase(addr / kPage); },
                       [&] { return paginaBorrada(addr); });
  }
  nvmcWaitReady();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
  nvmcWaitReady();
  NRF_NVMC->ERASEPAGE = addr;
  nvmcWaitReady();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
  return true;
}

bool nvmWrite(uint32_t addr, const void *data, size_t len) {
  const uint8_t *src = (const uint8_t *)data;
  const bool sd = sdArriba();
  while (len > 0) {
    const size_t bytes = len > kTrozoPalabras * 4 ? kTrozoPalabras * 4 : len;
    const size_t palabras = (bytes + 3) / 4;
    gTrozo[palabras - 1] = 0xFFFFFFFFu;  // relleno de la ultima palabra
    memcpy(gTrozo, src, bytes);

    if (sd) {
      const uint32_t dst = addr;
      const bool ok = operacionSd(
          [&] { return sd_flash_write((uint32_t *)dst, gTrozo, palabras); },
          [&] { return memcmp((const void *)dst, gTrozo, palabras * 4) == 0; });
      if (!ok) return false;
    } else {
      nvmcWaitReady();
      NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
      nvmcWaitReady();
      for (size_t i = 0; i < palabras; i++) {
        *(volatile uint32_t *)(addr + i * 4) = gTrozo[i];
        nvmcWaitReady();
      }
      NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    }
    addr += palabras * 4;
    src += bytes;
    len -= bytes;
  }
  return true;
}
