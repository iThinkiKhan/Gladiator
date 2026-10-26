#!/usr/bin/env python3
"""Read the S3 flight recorder straight from flash and write it out as CSV text.

The recorder is a ring of 4 KB sectors in the 'spiffs' partition (see
src/blackbox/BlackBox.cpp). Reading it with esptool avoids the USB serial dump, which
can drop bytes. Usage: python tools/bb_read.py COM3 out.csv [--kb 3456]
"""
import struct, subprocess, sys, tempfile, os
from pathlib import Path

port, out = sys.argv[1], sys.argv[2]
kb = int(sys.argv[sys.argv.index("--kb") + 1]) if "--kb" in sys.argv else 3456
esptool = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
python = Path.home() / ".platformio/penv/Scripts/python.exe"
raw = Path(tempfile.gettempdir()) / "bb_raw.bin"
subprocess.run([str(python), str(esptool), "--chip", "esp32s3", "-p", port, "-b", "921600",
                "read_flash", "0xC90000", str(kb * 1024), str(raw)], check=True,
               stdout=subprocess.DEVNULL)
data, SECTOR = raw.read_bytes(), 4096
sectors = []
for i in range(len(data) // SECTOR):
    sec = data[i * SECTOR:(i + 1) * SECTOR]
    magic, seq = struct.unpack("<II", sec[:8])
    if magic != 0x31584242 or seq in (0, 0xFFFFFFFF):
        continue
    end = 16
    while end < SECTOR and sec[end] != 0xFF:
        end += 1
    sectors.append((seq, sec[16:end]))
sectors.sort()
text = b"".join(p for _, p in sectors)
if sectors and sectors[0][0] != 1:  # ring has wrapped: drop the first partial row
    text = text[text.find(b"\n") + 1:]
Path(out).write_bytes(text)
print(f"{len(sectors)} sectors, seq {sectors[0][0] if sectors else 0}..{sectors[-1][0] if sectors else 0}, "
      f"{len(text)} bytes, {text.count(chr(10).encode())} rows -> {out}")
