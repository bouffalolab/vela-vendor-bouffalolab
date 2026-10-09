#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Export the BL616CL macsw/wl80211 cores of a source-mode wifi build into the
# prebuilt bundles used when the private sources are not checked out.
#
# Usage: export_wifi_prebuilt.sh [build-dir]
#
# build-dir defaults to cmake_out/ai-m64l-32s-kit_wifi and must come from
# "vela build ai-m64l-32s-kit/wifi" without --use-lib. The libraries keep the
# fat LTO objects of that build and, like the native SDK libraries, only the
# early LTO debug info. Rebuild with "--use-lib macsw,wl80211" afterwards and
# compare the images.

set -euo pipefail

CHIP=bl616cl
PROFILE=vela_bl616cl
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
VENDOR_DIR=$(cd "$SCRIPT_DIR/../.." && pwd -P)
SDK_ROOT=$(cd "$VENDOR_DIR/../.." && pwd -P)
WIFI_DIR=$VENDOR_DIR/components/wireless/wifi
MACSW_SRC=$WIFI_DIR/macsw/macsw
WL80211_PUBLIC=$WIFI_DIR/wl80211/wl80211
WL80211_PRIVATE=$WL80211_PUBLIC/src
BUILD_DIR=${1:-$SDK_ROOT/cmake_out/ai-m64l-32s-kit_wifi}
WL80211_BUILD=$BUILD_DIR/wl80211-$CHIP
STAGE=

fail()
{
  printf 'export_wifi_prebuilt: %s\n' "$*" >&2
  exit 1
}

cleanup()
{
  if [ -n "$STAGE" ]; then
    rm -rf "$STAGE"
  fi
}

trap cleanup EXIT

cache_value()
{
  sed -n "s/^$2:[A-Z]*=//p" "$1/CMakeCache.txt"
}

check_source()
{
  [ -f "$1/CMakeLists.txt" ] || fail "missing source checkout: $1"
  if [ -n "$(git -C "$1" status --porcelain --untracked-files=no)" ]; then
    fail "uncommitted changes in $1"
  fi
  if [ -z "$(git -C "$1" branch -r --contains HEAD)" ]; then
    printf 'export_wifi_prebuilt: warning: %s HEAD is not on a remote branch\n' \
      "$1" >&2
  fi
}

check_source "$MACSW_SRC"
check_source "$WL80211_PUBLIC"
check_source "$WL80211_PRIVATE"

[ -f "$BUILD_DIR/CMakeCache.txt" ] || fail "not a configured build: $BUILD_DIR"
[ -f "$WL80211_BUILD/CMakeCache.txt" ] || fail "no wl80211 core build in $BUILD_DIR"
case ",$(cache_value "$BUILD_DIR" BL_USE_LIB_COMPONENTS)," in
  *,macsw,* | *,wl80211,*) fail "$BUILD_DIR links the prebuilt cores" ;;
esac

CMAKE=$(cache_value "$BUILD_DIR" CMAKE_COMMAND)
CC=$(cache_value "$WL80211_BUILD" CMAKE_C_COMPILER)
OBJCOPY=$(cache_value "$WL80211_BUILD" CMAKE_OBJCOPY)
if [ ! -x "$CMAKE" ] || [ ! -x "$CC" ] || [ ! -x "$OBJCOPY" ]; then
  fail "cannot find cmake/gcc/objcopy in the build caches"
fi

# Bring both cores up to date with the checked-out sources.
"$CMAKE" --build "$WL80211_BUILD"
"$CMAKE" --build "$BUILD_DIR" --target macsw_$CHIP macsw_config_${CHIP}_$PROFILE

MACSW_LIB_DIR=$(dirname "$(find "$BUILD_DIR" -name "libmacsw_$CHIP.a" -print -quit)")
[ "$MACSW_LIB_DIR" != . ] || fail "libmacsw_$CHIP.a not found in $BUILD_DIR"

STAGE=$(mktemp -d)
mkdir -p "$STAGE/macsw/lib" "$STAGE/wl80211/lib"

git -C "$MACSW_SRC" archive HEAD inc | tar -x -C "$STAGE/macsw"
git -C "$WL80211_PUBLIC" archive HEAD include macsw/wl80211_mac.h \
  wl80211_platform.h wl80211_async_event.h wifi_mgmr.c country.c supplicant.c \
  nuttx.c rtos_al_nuttx.c | tar -x -C "$STAGE/wl80211"

# The LTO link builds the debug info of the cores from the early debug info
# (.gnu.debuglto_*), so drop only the debug info of the fat code, which an
# LTO link never uses. --strip-debug would remove both.
strip_late_debug()
{
  "$OBJCOPY" -R '.debug_*' -R '.rela.debug_*' "$1" "$2"
}

for lib in libmacsw_$CHIP.a libmacsw_config_${CHIP}_$PROFILE.a; do
  strip_late_debug "$MACSW_LIB_DIR/$lib" "$STAGE/macsw/lib/$lib"
done
strip_late_debug "$WL80211_BUILD/libwl80211_$CHIP.a" \
  "$STAGE/wl80211/lib/libwl80211_$CHIP.a"

TOOLCHAIN=$("$CC" --version | head -n 1)
MACSW_SHA=$(git -C "$MACSW_SRC" rev-parse HEAD)

cat > "$STAGE/macsw/VERSION" <<EOF
component: macsw
chip: $CHIP
profile: $PROFILE
macsw: $MACSW_SHA
toolchain: $TOOLCHAIN
objects: fat LTO, -fshort-enums, early LTO debug info only
EOF

cat > "$STAGE/wl80211/VERSION" <<EOF
component: wl80211
chip: $CHIP
profile: $PROFILE
public: $(git -C "$WL80211_PUBLIC" rev-parse HEAD)
private: $(git -C "$WL80211_PRIVATE" rev-parse HEAD)
macsw: $MACSW_SHA
toolchain: $TOOLCHAIN
objects: fat LTO, -fshort-enums, early LTO debug info only
EOF

for component in macsw wl80211; do
  rm -rf "$WIFI_DIR/$component/prebuilt"
  mv "$STAGE/$component" "$WIFI_DIR/$component/prebuilt"
done

ls -l "$WIFI_DIR"/macsw/prebuilt/lib "$WIFI_DIR"/wl80211/prebuilt/lib
printf 'exported %s/{macsw,wl80211}/prebuilt\n' "$WIFI_DIR"
