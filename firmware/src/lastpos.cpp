// lastpos.cpp — ultima posicion conocida del GPS en la flash (ver lastpos.h).
//
// Formato: una cabecera con marca + CRC, seguida de la posicion. Se escribe en
// una pagina propia (0xE7000) con el mismo patron que el resto del proyecto:
// borrar la pagina, escribir, y no dar nada por valido si el CRC no cuadra.
// License: GPL-3.0

#include "lastpos.h"

#include <Arduino.h>
#include <nrf.h>
#include <string.h>

namespace {

constexpr uint32_t kPageAddr = 0xE7000;  // pagina propia, ver lastpos.h
constexpr uint32_t kMagic = 0x53504C41;  // "ALPS" (last position)
constexpr uint16_t kVersion = 1;

// Intervalo minimo entre escrituras. En marcha se manda baliza como mucho cada
// 30 s y se guarda como mucho cada 60 s: la pagina aguanta ~100.000 borrados,
// asi que son anos de uso. En parado, la baliza lenta es cada 15 min.
constexpr uint32_t kMinWriteIntervalMs = 60000;

struct Stored {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t crc;  // CRC32 de todo lo que va detras
  // --- datos ---
  int32_t latE7;   // latitud * 1e7 (entero: la flash guarda enteros)
  int32_t lonE7;   // longitud * 1e7
  float courseDeg;
  float speedKmh;
  float altM;
  uint8_t altValid;
  uint8_t timeValid;
  uint8_t utcH, utcM, utcS;
  uint8_t utcDay, utcMonth;
  uint16_t utcYear;
  uint8_t pad[2];
};

Stored gStored;
bool gLoaded = false;
bool gValid = false;

uint32_t gLastWriteMs = 0;
bool gEverWrote = false;

uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return ~crc;
}

void nvmcWaitReady() {
  while (!NRF_NVMC->READY) {
  }
}

void nvmcErasePage(uint32_t addr) {
  nvmcWaitReady();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
  nvmcWaitReady();
  NRF_NVMC->ERASEPAGE = addr;
  nvmcWaitReady();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
}

void nvmcWriteWords(uint32_t addr, const uint32_t *words, size_t count) {
  nvmcWaitReady();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
  nvmcWaitReady();
  for (size_t i = 0; i < count; i++) {
    *(volatile uint32_t *)addr = words[i];
    addr += 4;
    nvmcWaitReady();
  }
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
}

// Offset del CRC dentro de la estructura: todo lo que va DETRAS del campo crc.
constexpr size_t kCrcOffset = offsetof(Stored, latE7);

}  // namespace

bool lastPosLoad() {
  gLoaded = true;
  gValid = false;

  memcpy(&gStored, (const void *)kPageAddr, sizeof(gStored));

  if (gStored.magic != kMagic || gStored.version != kVersion) return false;

  const size_t datos = sizeof(Stored) - kCrcOffset;
  const uint32_t crc = crc32(((const uint8_t *)&gStored) + kCrcOffset, datos);
  if (crc != gStored.crc) return false;

  // Una posicion (0,0) no es una posicion: es la flash virgen o un guardado a
  // medias. Se descarta.
  if (gStored.latE7 == 0 && gStored.lonE7 == 0) return false;

  gValid = true;
  return true;
}

bool lastPosValid() {
  if (!gLoaded) lastPosLoad();
  return gValid;
}

bool lastPosTimeValid() {
  if (!gLoaded) lastPosLoad();
  return gValid && gStored.timeValid;
}
uint8_t lastPosHour() { return gStored.utcH; }
uint8_t lastPosMinute() { return gStored.utcM; }
uint8_t lastPosSecond() { return gStored.utcS; }
uint8_t lastPosDay() { return gStored.utcDay; }
uint8_t lastPosMonth() { return gStored.utcMonth; }

bool lastPosFill(GpsData &out, bool &fixTimeValid) {
  if (!lastPosValid()) return false;

  out = GpsData();  // todo a cero y fix=false
  out.lat = (double)gStored.latE7 / 1e7;
  out.lon = (double)gStored.lonE7 / 1e7;
  out.courseDeg = gStored.courseDeg;
  out.speedKmh = gStored.speedKmh;
  out.altM = gStored.altM;
  out.altValid = gStored.altValid != 0;
  out.timeValid = gStored.timeValid != 0;
  out.utcH = gStored.utcH;
  out.utcM = gStored.utcM;
  out.utcS = gStored.utcS;
  out.utcDay = gStored.utcDay;
  out.utcMonth = gStored.utcMonth;
  out.utcYear = gStored.utcYear;
  out.dateValid = gStored.timeValid != 0;

  // La hora NO es la de ahora: es la de cuando se tomo. Ver lastpos.h.
  fixTimeValid = false;
  return true;
}

bool lastPosSave(const GpsData &g, bool force) {
  // Nunca se guarda sin fijacion real: la posicion fija configurada del
  // repetidor no es una posicion conocida por GPS.
  if (!g.fix) return false;

  const uint32_t now = millis();
  if (!force && gEverWrote && (now - gLastWriteMs) < kMinWriteIntervalMs) {
    return false;
  }

  Stored s;
  memset(&s, 0, sizeof(s));
  s.magic = kMagic;
  s.version = kVersion;
  s.latE7 = (int32_t)llround(g.lat * 1e7);
  s.lonE7 = (int32_t)llround(g.lon * 1e7);
  s.courseDeg = g.courseDeg;
  s.speedKmh = g.speedKmh;
  s.altM = g.altM;
  s.altValid = g.altValid ? 1 : 0;
  s.timeValid = g.timeValid ? 1 : 0;
  s.utcH = g.utcH;
  s.utcM = g.utcM;
  s.utcS = g.utcS;
  s.utcDay = g.utcDay;
  s.utcMonth = g.utcMonth;
  s.utcYear = g.utcYear;

  const size_t datos = sizeof(Stored) - kCrcOffset;
  s.crc = crc32(((const uint8_t *)&s) + kCrcOffset, datos);

  // Escritura: borrar y escribir la estructura entera en palabras de 32 bits.
  const uint32_t *w = (const uint32_t *)&s;
  const size_t words = (sizeof(Stored) + 3) / 4;

  nvmcErasePage(kPageAddr);
  nvmcWriteWords(kPageAddr, w, words);

  // Comprobacion de lectura: si no cuadra, no damos la posicion por buena.
  Stored back;
  memcpy(&back, (const void *)kPageAddr, sizeof(back));
  const bool ok = (back.magic == kMagic && back.crc == s.crc &&
                   back.latE7 == s.latE7 && back.lonE7 == s.lonE7);

  gStored = s;
  gLoaded = true;
  gValid = ok;
  gLastWriteMs = now;
  gEverWrote = true;
  return ok;
}

bool lastPosWipe() {
  nvmcErasePage(kPageAddr);
  gValid = false;
  gLoaded = true;
  gEverWrote = false;
  return true;
}
