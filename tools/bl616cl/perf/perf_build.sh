#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build a board config with perfmon added, for profiling a workload.
#
# Usage: perf_build.sh <out-dir> [config]
#
# Copies configs/<config> (default wifi) to a temporary config, enables
# BL616CL_PERFMON and the perfmon command, builds it and stages the image in
# <out-dir>: nuttx.bin, partition.bin, final_nuttx, nuttx.map and a
# flash_prog_cfg.ini that points there.  The temporary config and its build
# directory are removed afterwards.  Flash the image with
#   vendor/bouffalolab/vela flash --config <out-dir>/flash_prog_cfg.ini \
#     --port /dev/ttyUSB3 --baudrate 1000000
# See docs/bl616cl-perfmon.md.

set -euo pipefail

[ $# -ge 1 ] || { sed -n '3,14p' "$0"; exit 1; }

TOOLS=$(cd "$(dirname "$0")" && pwd -P)
VENDOR=$(cd "$TOOLS/../../.." && pwd -P)
ROOT=$(cd "$VENDOR/../.." && pwd -P)
CONFIGS=$VENDOR/boards/bl616cl/ai-m64l-32s-kit/configs
SRC=${2:-wifi}
CFG=tmp-perf
BUILD=$ROOT/cmake_out/ai-m64l-32s-kit_$CFG
OUT=$(realpath -m "$1")

cleanup()
{
  rm -rf "$CONFIGS/$CFG" "$BUILD"
}
trap cleanup EXIT

[ -f "$CONFIGS/$SRC/defconfig" ] || { echo "no config $SRC"; exit 1; }
cleanup
cp -r "$CONFIGS/$SRC" "$CONFIGS/$CFG"
printf 'CONFIG_BL616CL_PERFMON=y\nCONFIG_BL_PERF_TOOLS_PERFMON=y\n' \
  >> "$CONFIGS/$CFG/defconfig"

mkdir -p "$OUT"
if ! "$VENDOR/vela" build "ai-m64l-32s-kit/$CFG" > "$OUT/build.log" 2>&1; then
  echo "build FAILED, see $OUT/build.log"
  exit 1
fi

cp "$BUILD"/{nuttx.bin,partition.bin,final_nuttx,nuttx.map} "$OUT/"
sed "s#$BUILD/#$OUT/#" "$BUILD/flash_prog_cfg.ini" > "$OUT/flash_prog_cfg.ini"
echo "$SRC + perfmon staged in $OUT"
