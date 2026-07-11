#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build BL616CL boot2 application and whole-flash images.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
TOOLS_DIR=$(cd "$SCRIPT_DIR/.." && pwd -P)
POST_TOOL_DIR="$TOOLS_DIR/bflb_fw_post_proc"
FLASH_TOOL_DIR="$TOOLS_DIR/bouffalo_flash_cube"
FLASH_CONFIG="$SCRIPT_DIR/flash_factory_cfg.ini"
MFG_NAME=mfg_bl616cl_m0_sdk_5c976f45_autoboot.bin
FLASH_SIZE=$((0x400000))
APP_SIZE=$((0x200000))
MFG_SIZE=$((0x168000))
IMAGE=
RAW_IMAGE=
BOARD_CONFIG=
WORK_DIR=
STAGED_APP=
STAGED_WHOLE=

fail()
{
  printf 'postprocess_bl616cl: %s\n' "$*" >&2
  exit 1
}

cleanup()
{
  if [ -n "$WORK_DIR" ]; then
    rm -rf "$WORK_DIR"
  fi

  if [ -n "$STAGED_APP" ]; then
    rm -f "$STAGED_APP"
  fi

  if [ -n "$STAGED_WHOLE" ]; then
    rm -f "$STAGED_WHOLE"
  fi
}

require_file()
{
  if [ ! -f "$1" ]; then
    fail "required file does not exist: $1"
  fi
}

file_size()
{
  wc -c < "$1" | tr -d ' '
}

compare_segment()
{
  local image=$1
  local offset=$2
  local input=$3
  local size

  size=$(file_size "$input")
  if ! dd if="$image" bs=1 skip="$offset" count="$size" 2>/dev/null |
       cmp -s - "$input"; then
    fail "whole image segment mismatch at offset $offset: $input"
  fi
}

write_segment()
{
  dd if="$3" of="$1" bs=1 seek="$2" conv=notrunc 2>/dev/null
}

select_tools()
{
  case "$(uname -s)" in
    Linux*)
      POST_TOOL_NAME=bflb_fw_post_proc-ubuntu
      FLASH_TOOL_NAME=BLFlashCommand-ubuntu
      ;;
    Darwin*)
      POST_TOOL_NAME=bflb_fw_post_proc-macos
      FLASH_TOOL_NAME=BLFlashCommand-macos
      ;;
    MINGW* | MSYS* | CYGWIN*)
      POST_TOOL_NAME=bflb_fw_post_proc.exe
      FLASH_TOOL_NAME=BLFlashCommand.exe
      ;;
    *)
      fail "unsupported host platform: $(uname -s)"
      ;;
  esac
}

while [ "$#" -gt 0 ]
do
  case "$1" in
    --image)
      [ "$#" -ge 2 ] || fail "--image requires a path"
      IMAGE=$2
      shift 2
      ;;
    --raw-image)
      [ "$#" -ge 2 ] || fail "--raw-image requires a path"
      RAW_IMAGE=$2
      shift 2
      ;;
    --board-config)
      [ "$#" -ge 2 ] || fail "--board-config requires a path"
      BOARD_CONFIG=$2
      shift 2
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

[ -n "$IMAGE" ] || fail "--image is required"
[ -n "$RAW_IMAGE" ] || fail "--raw-image is required"
[ -n "$BOARD_CONFIG" ] || fail "--board-config is required"
require_file "$RAW_IMAGE"
[ -d "$BOARD_CONFIG" ] || fail "board config directory does not exist: $BOARD_CONFIG"

IMAGE_DIR=$(cd "$(dirname "$IMAGE")" && pwd -P)
IMAGE="$IMAGE_DIR/$(basename "$IMAGE")"
RAW_IMAGE_DIR=$(cd "$(dirname "$RAW_IMAGE")" && pwd -P)
RAW_IMAGE="$RAW_IMAGE_DIR/$(basename "$RAW_IMAGE")"
BOARD_CONFIG=$(cd "$BOARD_CONFIG" && pwd -P)
WHOLE_IMAGE="$IMAGE_DIR/nuttx.whole.bin"

select_tools
POST_TOOL="$POST_TOOL_DIR/$POST_TOOL_NAME"
FLASH_TOOL="$FLASH_TOOL_DIR/$FLASH_TOOL_NAME"

for file in \
  "$BOARD_CONFIG/bl_factory_params_IoTKitA_auto.dts" \
  "$BOARD_CONFIG/boot2_bl616cl_isp_release_v8.2.1.bin" \
  "$BOARD_CONFIG/partition_cfg_4M.toml" \
  "$BOARD_CONFIG/$MFG_NAME" \
  "$POST_TOOL" \
  "$FLASH_TOOL" \
  "$FLASH_CONFIG" \
  "$FLASH_TOOL_DIR/chips/bl616cl/eflash_loader/eflash_loader_cfg.conf" \
  "$FLASH_TOOL_DIR/chips/bl616cl/efuse_bootheader/efuse_bootheader_cfg.conf" \
  "$FLASH_TOOL_DIR/chips/bl616cl/efuse_bootheader/flash_para.bin"
do
  require_file "$file"
done

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/bl616cl-postprocess.XXXXXX")
trap cleanup EXIT
BUILD_STAGE="$WORK_DIR/build"
FLASH_STAGE="$WORK_DIR/flash_cube"
mkdir -p "$BUILD_STAGE" "$FLASH_STAGE"
cp "$RAW_IMAGE" "$BUILD_STAGE/nuttx.bin"

"$POST_TOOL" \
  --chipname=bl616cl \
  --imgfile="$BUILD_STAGE/nuttx.bin" \
  --appkeys=shared \
  --brdcfgdir="$BOARD_CONFIG" \
  --mfgfile="$BOARD_CONFIG/$MFG_NAME"

for file in \
  "$BUILD_STAGE/nuttx.bin" \
  "$BUILD_STAGE/boot2_bl616cl_isp_release_v8.2.1.bin" \
  "$BUILD_STAGE/partition.bin" \
  "$BUILD_STAGE/$MFG_NAME"
do
  require_file "$file"
done

if [ "$(file_size "$BUILD_STAGE/nuttx.bin")" -gt "$APP_SIZE" ]; then
  fail "application image exceeds the 0x200000-byte partition"
fi

if [ "$(file_size "$BUILD_STAGE/$MFG_NAME")" -gt "$MFG_SIZE" ]; then
  fail "MFG image exceeds the 0x168000-byte partition"
fi

cp "$FLASH_TOOL" "$FLASH_STAGE/$FLASH_TOOL_NAME"
cp -R "$FLASH_TOOL_DIR/chips" "$FLASH_STAGE/"
cp "$FLASH_CONFIG" "$BUILD_STAGE/flash_factory_cfg.ini"
chmod +x "$FLASH_STAGE/$FLASH_TOOL_NAME"

(
  cd "$BUILD_STAGE"
  "$FLASH_STAGE/$FLASH_TOOL_NAME" \
    --chipname=bl616cl \
    --config="$BUILD_STAGE/flash_factory_cfg.ini" \
    --build
)

PACKED_IMAGE="$FLASH_STAGE/chips/bl616cl/img_create/whole_flash_data.bin"
require_file "$PACKED_IMAGE"
if [ "$(file_size "$PACKED_IMAGE")" -gt "$FLASH_SIZE" ]; then
  fail "FlashCube output exceeds the 4 MiB flash size"
fi

compare_segment "$PACKED_IMAGE" $((0x000000)) \
  "$BUILD_STAGE/boot2_bl616cl_isp_release_v8.2.1.bin"
compare_segment "$PACKED_IMAGE" $((0x00e000)) "$BUILD_STAGE/partition.bin"
compare_segment "$PACKED_IMAGE" $((0x00f000)) "$BUILD_STAGE/partition.bin"
compare_segment "$PACKED_IMAGE" $((0x010000)) "$BUILD_STAGE/nuttx.bin"
compare_segment "$PACKED_IMAGE" $((0x210000)) "$BUILD_STAGE/$MFG_NAME"

PADDED_IMAGE="$WORK_DIR/nuttx.whole.bin"
dd if=/dev/zero bs=1048576 count=4 2>/dev/null | \
  LC_ALL=C tr '\000' '\377' > "$PADDED_IMAGE"
write_segment "$PADDED_IMAGE" $((0x000000)) \
  "$BUILD_STAGE/boot2_bl616cl_isp_release_v8.2.1.bin"
write_segment "$PADDED_IMAGE" $((0x00e000)) "$BUILD_STAGE/partition.bin"
write_segment "$PADDED_IMAGE" $((0x00f000)) "$BUILD_STAGE/partition.bin"
write_segment "$PADDED_IMAGE" $((0x010000)) "$BUILD_STAGE/nuttx.bin"
write_segment "$PADDED_IMAGE" $((0x210000)) "$BUILD_STAGE/$MFG_NAME"

STAGED_APP="$IMAGE_DIR/.nuttx.bin.postprocessed.$$"
STAGED_WHOLE="$IMAGE_DIR/.nuttx.whole.bin.postprocessed.$$"
cp "$BUILD_STAGE/nuttx.bin" "$STAGED_APP"
cp "$PADDED_IMAGE" "$STAGED_WHOLE"
mv "$STAGED_APP" "$IMAGE"
STAGED_APP=
mv "$STAGED_WHOLE" "$WHOLE_IMAGE"
STAGED_WHOLE=
