// flog.cpp — persistent trip/audit log in the internal flash
//
// Layout: 32 x 4 KB pages from 0xC8000 to 0xE7FFF (right below the config pages
// at 0xE8000/0xE9000 and far away from the application, so a log failure can
// never touch the config). It is a ring: when the last page fills up, the
// OLDEST page is erased and reused, so the log never "fills up" and the newest
// data is always kept.
//
// Power-loss safety (the whole point of the design):
//   - every line is a record with its own header written AFTER the payload, so
//     an interrupted write leaves a 0xFF header and the record is ignored;
//   - a corrupted record ends that page, the previous ones stay readable;
//   - nothing is written while the battery is low (same rule as the config):
//     writing the flash during a brownout is what corrupts it;
//   - records are appended, so filling a page needs no erase; only recycling a
//     page erases (32 pages => very low wear).
//
// USB safety (hardware lesson, 2026-09-13): every write goes through
// flogLine() below, which programs the NVMC and waits for READY. That stall
// during START-UP killed the USB CDC port of a node that was otherwise alive
// (COM present, zero lines out), so nothing in the boot path may call it:
// flogInit() is read-only, and the first line of a boot is written once the
// node is running (main loop, or the Bluetooth start-up with Bluetooth on).
// License: GPL-3.0

#include "flog.h"

#include <stdarg.h>

#include <nrf.h>

#include "gps.h"
#include "power.h"

namespace {

constexpr uint32_t kLogBase = 0xC8000;   // first log page
constexpr uint32_t kPageSize = 0x1000;   // 4 KB
constexpr uint8_t kPages = 32;           // 128 KB total
constexpr uint8_t kPageHdr = 8;          // magic(4) + seq(4)
constexpr uint16_t kMaxRec = 200;        // max payload bytes per record
constexpr uint32_t kMagic = 0x474F4C41;  // "ALOG"

uint8_t gCurPage = 0;
uint32_t gCurSeq = 0;
uint16_t gCurOff = kPageHdr;
bool gEnabled = false;
bool gPageReady = false;  // current page has a valid header
uint32_t gLines = 0;
char gBuf[256];

uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)d[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
    }
  }
  return crc;
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

// Write an arbitrary byte range (flash needs whole 32-bit words, padded 0xFF).
void nvmcWriteBytes(uint32_t addr, const uint8_t *data, size_t len) {
  if (len == 0) return;
  size_t words = (len + 3) / 4;
  nvmcWaitReady();
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
  nvmcWaitReady();
  for (size_t i = 0; i < words; i++) {
    uint32_t w = 0xFFFFFFFFu;
    uint8_t *p = (uint8_t *)&w;
    for (size_t b = 0; b < 4; b++) {
      size_t idx = i * 4 + b;
      if (idx < len) p[b] = data[idx];
    }
    *(volatile uint32_t *)(addr + i * 4) = w;
    nvmcWaitReady();
  }
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
}

uint32_t pageAddr(uint8_t page) {
  return kLogBase + (uint32_t)page * kPageSize;
}

// Walk one page: returns the number of valid records and sets `endOff` to the
// first free offset. `seq` gets the page sequence (0 when the page is empty).
uint16_t scanPage(uint8_t page, uint32_t &seq, uint16_t &endOff) {
  const uint32_t addr = pageAddr(page);
  seq = 0;
  endOff = kPageHdr;
  uint32_t magic = 0;
  memcpy(&magic, (const void *)addr, 4);
  if (magic != kMagic) return 0;
  memcpy(&seq, (const void *)(addr + 4), 4);

  uint16_t off = kPageHdr;
  uint16_t count = 0;
  while ((uint32_t)off + 4 <= kPageSize) {
    uint8_t hdr[4];
    memcpy(hdr, (const void *)(addr + off), 4);
    uint16_t len = (uint16_t)(hdr[0] | (hdr[1] << 8));
    uint16_t crc = (uint16_t)(hdr[2] | (hdr[3] << 8));
    if (len == 0 || len > kMaxRec) break;  // 0xFFFF/0 ⇒ free space or cut write
    uint16_t total = (uint16_t)(4 + ((len + 3) & ~3u));
    if ((uint32_t)off + total > kPageSize) break;
    const uint8_t *payload = (const uint8_t *)(addr + off + 4);
    if (crc16(payload, len) != crc) break;  // torn record ends the page
    off = (uint16_t)(off + total);
    count++;
  }
  endOff = off;
  return count;
}

bool ensurePage() {
  if (gPageReady) return true;
  nvmcErasePage(pageAddr(gCurPage));
  uint8_t hdr[kPageHdr];
  memcpy(hdr, &kMagic, 4);
  memcpy(hdr + 4, &gCurSeq, 4);
  nvmcWriteBytes(pageAddr(gCurPage), hdr, kPageHdr);
  gCurOff = kPageHdr;
  gPageReady = true;
  return true;
}

bool nextPage() {
  gCurPage = (uint8_t)((gCurPage + 1) % kPages);
  gCurSeq++;
  gPageReady = false;
  gCurOff = kPageHdr;
  return ensurePage();
}

// Low battery: never touch the flash (same rule as the config store).
bool powerSafeToWrite() {
  if (powerUsbPresent()) return true;
  return powerReadMv() >= 3000;
}

}  // namespace

void flogInit() {
  uint32_t bestSeq = 0;
  int bestPage = -1;
  uint32_t total = 0;

  for (uint8_t p = 0; p < kPages; p++) {
    uint32_t seq = 0;
    uint16_t endOff = 0;
    uint16_t n = scanPage(p, seq, endOff);
    total += n;
    if (seq == 0) continue;
    if (bestPage < 0 || (int32_t)(seq - bestSeq) > 0) {
      bestPage = (int)p;
      bestSeq = seq;
    }
  }

  gLines = total;
  if (bestPage < 0) {
    gCurPage = 0;
    gCurSeq = 1;
    gCurOff = kPageHdr;
    gPageReady = false;  // formatted on the first write
  } else {
    uint32_t seq = 0;
    uint16_t endOff = 0;
    scanPage((uint8_t)bestPage, seq, endOff);
    gCurPage = (uint8_t)bestPage;
    gCurSeq = seq;
    gCurOff = endOff;
    gPageReady = true;
  }

  // HARDWARE LESSON, 2026-09-13: this function used to leave a "flog listo"
  // marker in the log here, and that single NVMC write during start-up left the
  // node alive on the air but with a DEAD USB CDC port (COM present in Windows,
  // zero lines read for 12 s). Nothing in the boot path may program the flash:
  // flogInit() only READS the ring (scanPage uses memcpy, it never touches
  // NVMC) and a page is formatted lazily on the first real flogLine(), once the
  // node is running.
}

void flogSetEnabled(bool on) { gEnabled = on; }

bool flogEnabled() { return gEnabled; }

uint32_t flogLineCount() { return gLines; }

bool flogLine(const char *fmt, ...) {
  if (!gEnabled) return false;
  const GpsData &g = gpsGet();

  // Timestamp: GPS UTC (the module gives date + time). No clock, no log.
  int n = 0;
  if (g.timeValid) {
    if (g.dateValid) {
      n = snprintf(gBuf, sizeof(gBuf), "%04u-%02u-%02u %02u:%02u:%02u ",
                   (unsigned)g.utcYear, (unsigned)g.utcMonth,
                   (unsigned)g.utcDay, (unsigned)g.utcH, (unsigned)g.utcM,
                   (unsigned)g.utcS);
    } else {
      n = snprintf(gBuf, sizeof(gBuf), "%02u:%02u:%02u ", (unsigned)g.utcH,
                   (unsigned)g.utcM, (unsigned)g.utcS);
    }
  } else {
    n = snprintf(gBuf, sizeof(gBuf), "%lus ", (unsigned long)(millis() / 1000));
  }
  if (n < 0 || n >= (int)sizeof(gBuf)) return false;

  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(gBuf + n, sizeof(gBuf) - n, fmt, ap);
  va_end(ap);
  if (m < 0) return false;
  size_t len = strlen(gBuf);
  if (len == 0 || len > kMaxRec) return false;

  if (!powerSafeToWrite()) return false;

  uint16_t total = (uint16_t)(4 + ((len + 3) & ~3u));
  if (!gPageReady || (uint32_t)gCurOff + total > kPageSize) {
    // Page full (or not formatted yet): move on / recycle the oldest page.
    if (gPageReady) {
      nextPage();
    } else {
      ensurePage();
    }
    if ((uint32_t)gCurOff + total > kPageSize) return false;  // record too big
  }

  const uint32_t addr = pageAddr(gCurPage) + gCurOff;
  const uint16_t crc = crc16((const uint8_t *)gBuf, len);
  // Payload first, header last: an interrupted write leaves a dead record.
  nvmcWriteBytes(addr + 4, (const uint8_t *)gBuf, len);
  uint8_t hdr[4] = {(uint8_t)(len & 0xFF), (uint8_t)(len >> 8),
                    (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8)};
  nvmcWriteBytes(addr, hdr, 4);

  gCurOff = (uint16_t)(gCurOff + total);
  gLines++;
  return true;
}

namespace {

// Walk every stored line in chronological order and hand it to cb. Shared by
// flogDump (USB) and flogEachLine (in-firmware readers such as the ?APRSH
// query, which needs the stations heard in the last N hours).
void forEachLine(void (*cb)(const char *line, void *ctx), void *ctx) {
  // Collect valid pages and sort them by sequence (oldest first).
  uint8_t order[kPages];
  uint32_t seqs[kPages];
  uint8_t used = 0;
  for (uint8_t p = 0; p < kPages; p++) {
    uint32_t seq = 0;
    uint16_t endOff = 0;
    if (scanPage(p, seq, endOff) > 0) {
      order[used] = p;
      seqs[used] = seq;
      used++;
    }
  }
  for (uint8_t i = 0; i + 1 < used; i++) {
    for (uint8_t j = 0; j + 1 < used - i; j++) {
      if ((int32_t)(seqs[j] - seqs[j + 1]) > 0) {
        uint32_t ts = seqs[j];
        seqs[j] = seqs[j + 1];
        seqs[j + 1] = ts;
        uint8_t tp = order[j];
        order[j] = order[j + 1];
        order[j + 1] = tp;
      }
    }
  }

  char line[kMaxRec + 1];
  for (uint8_t i = 0; i < used; i++) {
    const uint32_t addr = pageAddr(order[i]);
    uint16_t off = kPageHdr;
    while ((uint32_t)off + 4 <= kPageSize) {
      uint8_t hdr[4];
      memcpy(hdr, (const void *)(addr + off), 4);
      uint16_t len = (uint16_t)(hdr[0] | (hdr[1] << 8));
      uint16_t crc = (uint16_t)(hdr[2] | (hdr[3] << 8));
      if (len == 0 || len > kMaxRec) break;
      uint16_t total = (uint16_t)(4 + ((len + 3) & ~3u));
      if ((uint32_t)off + total > kPageSize) break;
      const uint8_t *payload = (const uint8_t *)(addr + off + 4);
      if (crc16(payload, len) != crc) break;
      memcpy(line, payload, len);
      line[len] = '\0';
      cb(line, ctx);
      off = (uint16_t)(off + total);
    }
  }
}

void dumpCb(const char *line, void *ctx) {
  Stream *out = (Stream *)ctx;
  out->print("LOG ");
  out->println(line);
}

}  // namespace

void flogEachLine(void (*cb)(const char *line, void *ctx), void *ctx) {
  if (cb == nullptr) return;
  forEachLine(cb, ctx);
}

void flogDump(Stream &out) {
  out.println("LOG BEGIN");
  forEachLine(dumpCb, &out);
  out.println("LOG END");
}

void flogStats(Stream &out) {
  uint8_t usedPages = 0;
  for (uint8_t p = 0; p < kPages; p++) {
    uint32_t seq = 0;
    uint16_t endOff = 0;
    if (scanPage(p, seq, endOff) > 0) usedPages++;
  }
  out.print("log: ");
  out.print(gEnabled ? "on" : "off");
  out.print(" lines=");
  out.print(gLines);
  out.print(" pages=");
  out.print(usedPages);
  out.print("/");
  out.print(kPages);
  out.print(" page=");
  out.print(gCurPage);
  out.print(" seq=");
  out.print(gCurSeq);
  out.print(" used=");
  out.print(gCurOff);
  out.print("/");
  out.print(kPageSize);
  out.print(" wraps=");
  out.println(gCurSeq > kPages ? (gCurSeq - kPages) / kPages + 1 : 0);
}

bool flogClear() {
  if (!powerSafeToWrite()) return false;
  for (uint8_t p = 0; p < kPages; p++) nvmcErasePage(pageAddr(p));
  gCurPage = 0;
  gCurSeq = 1;
  gCurOff = kPageHdr;
  gPageReady = false;
  gLines = 0;
  return true;
}
