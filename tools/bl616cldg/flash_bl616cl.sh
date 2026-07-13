#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Flash a BL616CL whole image from address 0x0.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
FLASH_TOOL_DIR=$(cd "$SCRIPT_DIR/../bouffalo_flash_cube" && pwd -P)
IMAGE=
PORT=
BAUDRATE=2000000
WORK_DIR=

fail()
{
  printf 'flash_bl616cl: %s\n' "$*" >&2
  exit 1
}

cleanup()
{
  if [ -n "$WORK_DIR" ]; then
    rm -rf "$WORK_DIR"
  fi
}

require_file()
{
  [ -f "$1" ] || fail "required flash runtime asset does not exist: $1"
}

read_magic()
{
  od -An -tx1 -N4 -j "$2" "$1" | tr -d ' \n'
}

range_is_erased()
{
  od -An -v -tu1 -N "$3" -j "$2" "$1" |
    awk '{ for (i = 1; i <= NF; i++) if ($i != 255) exit 1 }'
}

while [ "$#" -gt 0 ]
do
  case "$1" in
    --image)
      [ "$#" -ge 2 ] || fail "--image requires a path"
      IMAGE=$2
      shift 2
      ;;
    --port)
      [ "$#" -ge 2 ] || fail "--port requires a value"
      PORT=$2
      shift 2
      ;;
    --baudrate)
      [ "$#" -ge 2 ] || fail "--baudrate requires a value"
      BAUDRATE=$2
      shift 2
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

[ -n "$IMAGE" ] || fail "--image is required"
[ -n "$PORT" ] || fail "--port is required"
[ -f "$IMAGE" ] || fail "whole image does not exist: $IMAGE"
IMAGE_DIR=$(cd "$(dirname "$IMAGE")" && pwd -P)
IMAGE="$IMAGE_DIR/$(basename "$IMAGE")"

if [ "$(wc -c < "$IMAGE" | tr -d ' ')" -ne $((0x400000)) ]; then
  fail "whole image must be exactly 4 MiB: $IMAGE"
fi

if [ "$(read_magic "$IMAGE" $((0x000000)))" != 42464e50 ] ||
   [ "$(read_magic "$IMAGE" $((0x00e000)))" != 42465054 ] ||
   [ "$(read_magic "$IMAGE" $((0x00f000)))" != 42465054 ] ||
   [ "$(read_magic "$IMAGE" $((0x010000)))" != 42464e50 ]; then
  fail "whole image layout is invalid: $IMAGE"
fi

if ! range_is_erased "$IMAGE" $((0x210000)) $((0x1f0000)); then
  fail "whole image MFG partition is not erased: $IMAGE"
fi

case "$(uname -s)" in
  Linux*) FLASH_TOOL_NAME=BLFlashCommand-ubuntu ;;
  Darwin*) FLASH_TOOL_NAME=BLFlashCommand-macos ;;
  MINGW* | MSYS* | CYGWIN*) FLASH_TOOL_NAME=BLFlashCommand.exe ;;
  *) fail "unsupported host platform: $(uname -s)" ;;
esac

FLASH_TOOL="$FLASH_TOOL_DIR/$FLASH_TOOL_NAME"
for file in \
  "$FLASH_TOOL" \
  "$FLASH_TOOL_DIR/chips/bl616cl/eflash_loader/eflash_loader_cfg.conf" \
  "$FLASH_TOOL_DIR/chips/bl616cl/efuse_bootheader/efuse_bootheader_cfg.conf" \
  "$FLASH_TOOL_DIR/chips/bl616cl/efuse_bootheader/flash_para.bin"
do
  require_file "$file"
done

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/bl616cl-flash.XXXXXX")
trap cleanup EXIT
cp "$FLASH_TOOL" "$WORK_DIR/$FLASH_TOOL_NAME"
cp -R "$FLASH_TOOL_DIR/chips" "$WORK_DIR/"
chmod +x "$WORK_DIR/$FLASH_TOOL_NAME"

"$WORK_DIR/$FLASH_TOOL_NAME" \
  --interface=uart \
  --chipname=bl616cl \
  --port="$PORT" \
  --baudrate="$BAUDRATE" \
  --firmware="$IMAGE"
