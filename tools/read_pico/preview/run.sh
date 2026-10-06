#!/usr/bin/env bash
# Host-renders the Read Pico views to PNG files (clang or gcc, Python 3).
# Usage: tools/read_pico/preview/run.sh [output directory]
set -euo pipefail
cd "$(dirname "$0")/../../.."
P=tools/read_pico/preview
M=firmware/targets/mindreset_read_pico/main
C=firmware/components
OUT=${1:-firmware/targets/mindreset_read_pico/build/preview}
mkdir -p "$OUT"
c++ -std=c++17 -O1 -w -I$P/stubs -I$M -I$C/usage_core/include -I$C/usage_ble/include \
    -I$C/usage_ota/include -I$C/usage_protocol/include \
    $P/preview.cpp $M/display_ui.cpp $M/demo_view.cpp $M/fonts.cpp \
    $C/usage_core/src/usage_state.cpp $C/usage_ota/src/presentation.cpp -o "$OUT/preview"
"$OUT/preview" "$OUT"
python3 - "$OUT" <<'PY'
import struct, sys, zlib
from pathlib import Path
for pgm in sorted(Path(sys.argv[1]).glob('board*.pgm')):
    _, wh, _, px = pgm.read_bytes().split(b'\n', 3)
    w, h = map(int, wh.split())
    raw = b''.join(b'\0' + px[y * w:(y + 1) * w] for y in range(h))
    chunk = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    pgm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    pgm.unlink()
    print(pgm.with_suffix('.png'))
PY
