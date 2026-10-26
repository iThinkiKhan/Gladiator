#pragma once
#include <Arduino.h>

// Flight recorder. Text rows go into a small RAM ring (never blocks the caller) and a
// low-priority task appends them to LittleFS in the 'spiffs' partition. Two segment
// files rotate (bb_<seq>.csv), so the newest 1-2 segments survive and the oldest are
// discarded when the partition fills. Rows are CSV, first field is the row type:
//   S  1 Hz system snapshot (every 5 s while idle)    F  5 Hz fast row while moving
//   E  log line (everything appendLog prints)          #  file/segment header
namespace blackbox {
struct Stats {
  bool mounted = false;
  uint32_t pendingBytes = 0, dropped = 0, writes = 0, fsErrors = 0;
  uint32_t writeMaxMs = 0;  // worst flash write since the previous call (stalls all cores)
  uint32_t fileBytes = 0, usedBytes = 0, totalBytes = 0, segment = 0;
};

bool begin();                       // allocate ring, start writer task; mounts in the task
void line(const char *text);        // one row, no trailing newline; thread-safe, nonblocking
void linef(const char *format, ...);
void setUrgent(bool urgent);        // low battery: flush every second instead of every 10 s
void flushSoon();
Stats takeStats();
// Serial status and erase. Reading the log back is done with tools/bb_read.py (esptool): an
// in-firmware serial dump returned scrambled 16-byte blocks on this board and was removed.
void manifest(Print &out);
bool clearAll();
}
