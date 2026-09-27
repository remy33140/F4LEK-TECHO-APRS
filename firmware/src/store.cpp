// store.cpp — config persistence in internal flash (APRS_NAVARRICA_FAKETEC_433)
//
// Layout: two 4 KB pages at 0xE8000 / 0xE9000 (below the Adafruit bootloader
// at 0xEA000 — the core InternalFileSystem lives at 0xED000, inside the
// bootloader region, and is NOT usable here). Save alternates pages; each
// header carries magic + version + seq + CRC32 of the JSON payload.
// License: GPL-3.0

#include "store.h"

#include "power.h"

#include <Arduino.h>
#include <ArduinoJson.h>

namespace {

constexpr uint32_t kStoreMagic = 0x43465041;  // "APFC"
constexpr uint16_t kStoreVersion = 1;
constexpr uint32_t kPageAddr0 = 0xE8000;
constexpr uint32_t kPageAddr1 = 0xE9000;
constexpr uint32_t kPageSize = 0x1000;  // 4 KB
constexpr size_t kMaxPayload = kPageSize - 32;
constexpr uint32_t kHeaderSize = 16;  // magic4 + ver2 + len2 + seq4 + crc4

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t len;
  uint32_t seq;
  uint32_t crc;
};

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

void readPage(uint32_t addr, uint8_t *out) {
  memcpy(out, (const void *)addr, kPageSize);
}

// Returns the page index with the valid snapshot (0/1) or -1 when none valid.
int findValidPage(uint8_t *pageBuf) {
  int best = -1;
  uint32_t bestSeq = 0;
  for (int i = 0; i < 2; i++) {
    uint8_t *p = pageBuf + (size_t)i * kPageSize;
    Header h;
    memcpy(&h, p, sizeof(h));
    if (h.magic != kStoreMagic || h.version != kStoreVersion) continue;
    if (h.len == 0 || h.len > kMaxPayload) continue;
    uint32_t crc = crc32(p + kHeaderSize, h.len);
    if (crc != h.crc) continue;
    if (best < 0 || (int32_t)(h.seq - bestSeq) > 0) {
      best = i;
      bestSeq = h.seq;
    }
  }
  return best;
}

}  // namespace

bool storeLoad(DigiConfig &cfg) {
  uint8_t *buf = (uint8_t *)malloc(2 * kPageSize);
  if (!buf) return false;
  readPage(kPageAddr0, buf);
  readPage(kPageAddr1, buf + kPageSize);
  int page = findValidPage(buf);
  if (page < 0) {
    free(buf);
    return false;
  }
  uint8_t *payload = buf + (size_t)page * kPageSize + kHeaderSize;
  Header h;
  memcpy(&h, buf + (size_t)page * kPageSize, sizeof(h));

  JsonDocument doc;
  DeserializationError derr = deserializeJson(doc, payload, h.len);
  if (derr) {
    free(buf);
    return false;
  }
  String err;
  bool ok = configFromJson(cfg, doc.as<JsonObjectConst>(), err);
  free(buf);
  return ok;
}

bool storeSave(const DigiConfig &cfg) {
  // Never touch NVMC when running from a nearly-empty battery (NavaTastic
  // rule: unsafe power can lock/corrupt flash writes). Bench/USB always saves.
  if (!powerUsbPresent() && powerReadMv() < 3000) return false;

  JsonDocument doc;
  configToJson(cfg, doc.to<JsonObject>());
  String json;
  serializeJson(doc, json);
  if (json.length() == 0 || json.length() > kMaxPayload) return false;

  uint8_t *buf = (uint8_t *)malloc(2 * kPageSize);
  if (!buf) return false;
  readPage(kPageAddr0, buf);
  readPage(kPageAddr1, buf + kPageSize);

  int validPage = findValidPage(buf);
  uint32_t seq = (validPage >= 0)
                     ? ((Header *)&buf[(size_t)validPage * kPageSize])->seq + 1
                     : 1;
  int target = (validPage == 0) ? 1 : 0;  // always write the other page

  uint8_t *page = buf + (size_t)target * kPageSize;
  Header h;
  h.magic = kStoreMagic;
  h.version = kStoreVersion;
  h.len = (uint16_t)json.length();
  h.seq = seq;
  h.crc = crc32((const uint8_t *)json.c_str(), json.length());

  memset(page, 0xFF, kHeaderSize);  // fresh header area
  memcpy(page, &h, sizeof(h));
  memset(page + kHeaderSize, 0xFF, kPageSize - kHeaderSize);
  memcpy(page + kHeaderSize, json.c_str(), json.length());

  uint32_t addr = (target == 0) ? kPageAddr0 : kPageAddr1;
  nvmcErasePage(addr);
  // Write in aligned words; kPageSize is 4-aligned.
  nvmcWriteWords(addr, (const uint32_t *)page, kPageSize / 4);

  free(buf);
  return true;
}

bool storeWipe() {
  nvmcErasePage(kPageAddr0);
  nvmcErasePage(kPageAddr1);
  return true;
}
