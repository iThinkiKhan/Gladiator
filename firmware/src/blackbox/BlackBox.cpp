#include "BlackBox.h"
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <stdarg.h>

// Raw circular log in the 'spiffs' data partition (nothing else uses it). The partition is
// a ring of 4 KB sectors, each starting with a 16-byte header {magic, sequence}. Rows are
// plain ASCII appended after the header; erased flash reads 0xFF, which never occurs in
// text, so the end of a sector's data is its first 0xFF. When a sector fills, the next one
// in the ring is erased and stamped with seq+1, so the newest ~3.4 MB of rows survive and
// the oldest sector is the one with the smallest sequence number. A row can straddle two
// sectors; dumping concatenates sectors in sequence order, which rejoins it.
namespace blackbox {
namespace {
constexpr size_t RING_BYTES = 16384;
constexpr size_t MAX_ROW = 400;
constexpr size_t CHUNK = 1024;
constexpr size_t SECTOR = 4096, HEADER = 16;
constexpr uint32_t MAGIC = 0x31584242;  // "BBX1" little-endian
constexpr uint32_t FLUSH_NORMAL_MS = 10000, FLUSH_URGENT_MS = 1000, FLUSH_BYTES = 3000;

struct SectorHeader { uint32_t magic, seq, reserved0, reserved1; };

uint8_t *ring = nullptr;
size_t head = 0, tail = 0;  // head = next write, tail = next read
portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t dropped = 0, writes = 0, fsErrors = 0, writeMaxMs = 0;
volatile bool urgent = false, flushRequested = false, mounted = false;
volatile uint32_t bytesWritten = 0, currentSeq = 0;

const esp_partition_t *part = nullptr;
uint32_t sectorCount = 0, sectorIndex = 0, sectorOffset = HEADER;

size_t used() { return (head + RING_BYTES - tail) % RING_BYTES; }

bool readHeader(uint32_t index, SectorHeader &h) {
  if (esp_partition_read(part, index * SECTOR, &h, sizeof h) != ESP_OK) return false;
  return h.magic == MAGIC && h.seq != 0 && h.seq != 0xFFFFFFFFu;
}

bool startSector(uint32_t index, uint32_t seq) {
  esp_err_t err = esp_partition_erase_range(part, index * SECTOR, SECTOR);
  if (err != ESP_OK) { ++fsErrors; Serial.printf("[blackbox] erase sector %u failed: %d\n", (unsigned)index, err); return false; }
  SectorHeader h{MAGIC, seq, 0, 0};
  err = esp_partition_write(part, index * SECTOR, &h, sizeof h);
  if (err != ESP_OK) { ++fsErrors; Serial.printf("[blackbox] header write failed: %d\n", err); return false; }
  sectorIndex = index;
  sectorOffset = HEADER;
  currentSeq = seq;
  return true;
}

// Resume after the highest-numbered sector, or start the ring on a blank partition.
bool mountRing() {
  part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  if (!part) { Serial.println("[blackbox] no 'spiffs' partition"); return false; }
  sectorCount = part->size / SECTOR;
  uint32_t best = 0, bestIndex = 0;
  for (uint32_t i = 0; i < sectorCount; ++i) {
    SectorHeader h;
    if (readHeader(i, h) && h.seq >= best) { best = h.seq; bestIndex = i; }
  }
  if (!best) return startSector(0, 1);

  static uint8_t scratch[SECTOR];
  if (esp_partition_read(part, bestIndex * SECTOR, scratch, SECTOR) != ESP_OK) { ++fsErrors; return false; }
  size_t end = HEADER;
  while (end < SECTOR && scratch[end] != 0xFF) ++end;
  if (end >= SECTOR) return startSector((bestIndex + 1) % sectorCount, best + 1);
  sectorIndex = bestIndex;
  sectorOffset = end;
  currentSeq = best;
  return true;
}

bool appendBytes(const uint8_t *data, size_t length) {
  while (length) {
    if (sectorOffset >= SECTOR && !startSector((sectorIndex + 1) % sectorCount, currentSeq + 1)) return false;
    const size_t n = min(length, SECTOR - sectorOffset);
    const esp_err_t err = esp_partition_write(part, sectorIndex * SECTOR + sectorOffset, data, n);
    if (err != ESP_OK) { ++fsErrors; Serial.printf("[blackbox] write failed: %d\n", err); return false; }
    sectorOffset += n;
    bytesWritten += n;
    data += n;
    length -= n;
  }
  return true;
}

void writePending() {
  static uint8_t buffer[CHUNK];
  while (true) {
    size_t n;
    portENTER_CRITICAL(&ringMux);
    n = used();
    if (n > CHUNK) n = CHUNK;
    for (size_t i = 0; i < n; ++i) buffer[i] = ring[(tail + i) % RING_BYTES];
    portEXIT_CRITICAL(&ringMux);
    if (!n) return;

    const uint32_t t0 = millis();
    const bool ok = appendBytes(buffer, n);
    const uint32_t ms = millis() - t0;
    if (ms > writeMaxMs) writeMaxMs = ms;
    if (!ok) return;  // leave the rows in the ring; retry next flush
    ++writes;
    portENTER_CRITICAL(&ringMux);
    tail = (tail + n) % RING_BYTES;
    portEXIT_CRITICAL(&ringMux);
  }
}

void writerTask(void *) {
  uint32_t lastFlush = millis(), lastMountTry = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    const uint32_t now = millis();
    if (!mounted) {
      if (lastMountTry && now - lastMountTry < 30000) continue;
      lastMountTry = now;
      if (mountRing()) {
        mounted = true;
        linef("#,ring,sectors,%u,resumeSector,%u,seq,%u,offset,%u", (unsigned)sectorCount,
              (unsigned)sectorIndex, (unsigned)currentSeq, (unsigned)sectorOffset);
      } else {
        Serial.println("[blackbox] ring init failed; will retry");
        continue;
      }
    }
    portENTER_CRITICAL(&ringMux);
    const size_t pending = used();
    portEXIT_CRITICAL(&ringMux);
    const uint32_t interval = urgent ? FLUSH_URGENT_MS : FLUSH_NORMAL_MS;
    if (pending && (flushRequested || pending >= FLUSH_BYTES || now - lastFlush >= interval)) {
      flushRequested = false;
      lastFlush = now;
      writePending();
    }
  }
}

// Valid sectors as (seq, index), sorted oldest first. Returns the count.
size_t sortedSectors(uint32_t *seqs, uint32_t *indexes, size_t capacity) {
  size_t count = 0;
  for (uint32_t i = 0; i < sectorCount && count < capacity; ++i) {
    SectorHeader h;
    if (!readHeader(i, h)) continue;
    size_t at = count++;
    while (at > 0 && seqs[at - 1] > h.seq) { seqs[at] = seqs[at - 1]; indexes[at] = indexes[at - 1]; --at; }
    seqs[at] = h.seq;
    indexes[at] = i;
  }
  return count;
}
}  // namespace

bool begin() {
  if (ring) return true;
  ring = static_cast<uint8_t *>(heap_caps_malloc(RING_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!ring) ring = static_cast<uint8_t *>(heap_caps_malloc(RING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ring) return false;
  // Same priority as loopTask; the writer sleeps nearly always.
  return xTaskCreatePinnedToCore(writerTask, "blackbox", 6144, nullptr, 1, nullptr, 1) == pdPASS;
}

void line(const char *text) {
  if (!ring || !text) return;
  size_t n = strnlen(text, MAX_ROW);
  if (!n) return;
  portENTER_CRITICAL(&ringMux);
  if (RING_BYTES - 1 - used() < n + 1) {
    ++dropped;
  } else {
    for (size_t i = 0; i < n; ++i) { ring[head] = text[i]; head = (head + 1) % RING_BYTES; }
    ring[head] = '\n';
    head = (head + 1) % RING_BYTES;
  }
  portEXIT_CRITICAL(&ringMux);
}

void linef(const char *format, ...) {
  char row[MAX_ROW + 1];
  va_list args;
  va_start(args, format);
  vsnprintf(row, sizeof row, format, args);
  va_end(args);
  line(row);
}

void setUrgent(bool value) { urgent = value; }
void flushSoon() { flushRequested = true; }

Stats takeStats() {
  Stats s;
  s.mounted = mounted;
  portENTER_CRITICAL(&ringMux);
  s.pendingBytes = used();
  portEXIT_CRITICAL(&ringMux);
  s.dropped = dropped;
  s.writes = writes;
  s.fsErrors = fsErrors;
  s.writeMaxMs = writeMaxMs;
  writeMaxMs = 0;
  s.fileBytes = bytesWritten;
  s.segment = currentSeq;
  s.totalBytes = part ? part->size : 0;
  const uint32_t seq = currentSeq;
  s.usedBytes = (seq < sectorCount ? seq : sectorCount) * SECTOR;
  return s;
}

void manifest(Print &out) {
  if (!mounted) { out.println("blackbox: not ready"); return; }
  static uint32_t seqs[1024], indexes[1024];
  const size_t count = sortedSectors(seqs, indexes, 1024);
  out.printf("blackbox: %u of %u sectors in use, sequence %u..%u, writing sector %u, %u KB written this boot\n",
             (unsigned)count, (unsigned)sectorCount, (unsigned)(count ? seqs[0] : 0),
             (unsigned)(count ? seqs[count - 1] : 0), (unsigned)sectorIndex, (unsigned)(bytesWritten / 1024));
}

bool clearAll() {
  if (!mounted || !part) return false;
  if (esp_partition_erase_range(part, 0, sectorCount * SECTOR) != ESP_OK) { ++fsErrors; return false; }
  bytesWritten = 0;
  return startSector(0, 1);
}
}  // namespace blackbox
