#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# A/B check of the Wi-Fi hot code layout that is robust to unrelated moves.
#
# Usage: [WIFI_TEST_PSK=...] layout_ab.sh [-b] <out-dir> [pad...]
#
# Builds configs/wifi with the ld.script hot list ("hot") and without it
# ("base"), each with a filler of every given size (default 0 0x1e0 0x9a0)
# after the Wi-Fi code.  The filler moves the rest of .text the way an
# unrelated change would; UDP throughput depends on that position, so only
# a gain that holds for every filler counts.  Images go to
# <out-dir>/img-<variant>.  With -b each image is then flashed and benched
# (wifi_bench.py, 3 x 20 s, UDP RX offered at 60M) into <out-dir>/<variant>
# and a min-max table per case is printed.
#
# The board ld.script is edited in place and restored on exit; do not build
# other configs meanwhile.  See docs/bl616cl-hot-code-layout.md.

set -euo pipefail

BENCH=0
if [ "${1:-}" = -b ]; then
  BENCH=1
  shift
fi
[ $# -ge 1 ] || { sed -n '3,17p' "$0"; exit 1; }

TOOLS=$(cd "$(dirname "$0")" && pwd -P)
VENDOR=$(cd "$TOOLS/../../.." && pwd -P)
ROOT=$(cd "$VENDOR/../.." && pwd -P)
BOARD=$VENDOR/boards/bl616cl/ai-m64l-32s-kit
LD=$BOARD/scripts/ld.script
CFG=tmp-layout-ab
OUT=$(realpath -m "$1")
shift
PADS=${*:-0 0x1e0 0x9a0}

mkdir -p "$OUT"
cp "$LD" "$OUT/ld.script.orig"

cleanup()
{
  cp "$OUT/ld.script.orig" "$LD"
  rm -rf "$BOARD/configs/$CFG" "$ROOT/cmake_out/ai-m64l-32s-kit_$CFG"
}
trap cleanup EXIT

rm -rf "$BOARD/configs/$CFG" "$ROOT/cmake_out/ai-m64l-32s-kit_$CFG"
cp -r "$BOARD/configs/wifi" "$BOARD/configs/$CFG"
BUILD=$ROOT/cmake_out/ai-m64l-32s-kit_$CFG

VARIANTS=
for kind in hot base; do
  for pad in $PADS; do
    v=$kind-$pad
    VARIANTS="$VARIANTS $v"

    # base drops the list between the two markers; the pad goes after the
    # end marker, so it moves everything behind the Wi-Fi code.

    awk -v kind="$kind" -v pad="$pad" '
      /__bl616cl_wifi_hot_end/ { skip = 0; print;
                                 if (pad != "0") print "    . = . + " pad ";";
                                 next }
      skip { next }
      { print }
      /__bl616cl_wifi_hot_start/ && kind == "base" { skip = 1 }
    ' "$OUT/ld.script.orig" > "$LD"

    if "$VENDOR/vela" build "ai-m64l-32s-kit/$CFG" > "$OUT/build-$v.log" 2>&1; then
      img=$OUT/img-$v
      mkdir -p "$img"
      cp "$BUILD"/{nuttx.bin,partition.bin,final_nuttx,nuttx.map} "$img/"
      sed "s#$BUILD/#$img/#" "$BUILD/flash_prog_cfg.ini" > "$img/flash_prog_cfg.ini"
      echo "$v built, nuttx.bin $(stat -c %s "$img/nuttx.bin") B"
    else
      echo "$v build FAILED, see $OUT/build-$v.log"
      exit 1
    fi
  done
done

cleanup
trap - EXIT
[ $BENCH = 1 ] || exit 0

for v in $VARIANTS; do
  mkdir -p "$OUT/$v"
  if "$VENDOR/vela" flash --config "$OUT/img-$v/flash_prog_cfg.ini" \
       --port "${WIFI_TEST_PORT:-/dev/ttyUSB3}" --baudrate 1000000 \
       > "$OUT/$v/flash.log" 2>&1 \
     && grep -q "All programming completed successfully" "$OUT/$v/flash.log"; then
    python3 "$TOOLS/wifi_bench.py" "$OUT/$v" --reps 3 --time 20 --udp-rx 60 \
      > "$OUT/$v/bench.stdout" 2>&1 || echo "$v bench failed"
  else
    echo "$v flash FAILED"
  fi
done

python3 - "$OUT" $VARIANTS <<'EOF'
import csv, statistics, sys
from pathlib import Path
out, variants = Path(sys.argv[1]), sys.argv[2:]
cases = ("tcp-tx", "udp-tx", "tcp-rx", "udp-rx")
print("| variant | " + " | ".join(cases) + " |")
print("| --- " * (len(cases) + 1) + "|")
for v in variants:
    try:
        rows = list(csv.DictReader(open(out / v / "results.csv")))
    except FileNotFoundError:
        continue
    cells = []
    for c in cases:
        r = [float(x["mbps"]) for x in rows if x["case"] == c and x["mbps"]]
        cells.append(f"{min(r):.1f}-{max(r):.1f} (med {statistics.median(r):.1f})"
                     if r else "-")
    print(f"| {v} | " + " | ".join(cells) + " |")
EOF
