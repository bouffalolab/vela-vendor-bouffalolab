#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$script_dir/../../.." && pwd)
compile_db=${1:-"$root/cmake_out/ai-m64l-32s-kit_nsh/compile_commands.json"}
header="$root/vendor/bouffalolab/chips/bl616cl/bl616cl_lhal.h"
chip_dir="$root/vendor/bouffalolab/chips/bl616cl"

if [[ ! -f "$compile_db" ]]; then
  echo "ERROR: compile database not found: $compile_db" >&2
  exit 1
fi

compile_command=$(jq -r \
  '.[] | select(.file | endswith("/bl616cl_lowputc.c")) | .command' \
  "$compile_db" | head -n 1)
if [[ -z "$compile_command" ]]; then
  echo "ERROR: bl616cl_lowputc.c command not found in $compile_db" >&2
  exit 1
fi

compile_prefix=${compile_command%% -o *}
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

dump_macros()
{
  local output=$1
  local includes=$2

  printf '%b' "$includes" | \
    eval "$compile_prefix -dM -E -x c -" | \
    awk '$1 == "#define" && $2 ~ /^E[A-Z0-9_]+$/ {
           name = $2
           $1 = $2 = ""
           sub(/^  */, "")
           print name "\t" $0
         }' | sort -k1,1 > "$output"
}

dump_macros "$tmpdir/nuttx" '#include <errno.h>\n'
dump_macros "$tmpdir/newlib" \
  '#include <errno.h>\n#include <sys/errno.h>\n'
dump_macros "$tmpdir/wrapped" '#include "bl616cl_lhal.h"\n'

join -t $'\t' "$tmpdir/nuttx" "$tmpdir/newlib" | \
  awk -F '\t' '$2 != $3 {print $1}' > "$tmpdir/expected"
rg 'push_macro\("E[A-Z0-9_]+"\)' "$header" | \
  sed -E 's/.*"(E[A-Z0-9_]+)".*/\1/' | sort > "$tmpdir/actual"

if [[ $(wc -l < "$tmpdir/expected") -ne 40 ]] ||
   ! diff -u "$tmpdir/expected" "$tmpdir/actual"; then
  echo "ERROR: LHAL errno preservation list is stale" >&2
  exit 1
fi

if ! join -t $'\t' "$tmpdir/nuttx" "$tmpdir/wrapped" |
     awk -F '\t' '$2 != $3 {exit 1}'; then
  echo "ERROR: an existing NuttX errno changed after LHAL import" >&2
  exit 1
fi

wrapped_count=$(join -t $'\t' "$tmpdir/nuttx" "$tmpdir/wrapped" | wc -l)
nuttx_count=$(wc -l < "$tmpdir/nuttx")
if [[ "$wrapped_count" -ne "$nuttx_count" ]]; then
  echo "ERROR: an existing NuttX errno disappeared after LHAL import" >&2
  exit 1
fi

printf '%s\n' \
  '#include "bl616cl_lhal.h"' \
  '_Static_assert(BL616CL_LHAL_ETIMEDOUT == 116, "LHAL timeout");' \
  '_Static_assert(ETIMEDOUT == 110, "NuttX timeout");' \
  '_Static_assert(ENOSYS == 38, "NuttX ENOSYS");' | \
  eval "$compile_prefix -Werror -fsyntax-only -x c -"

while IFS= read -r source; do
  first_lhal=$(rg -n '^#include "bflb_' "$source" | head -n 1 | cut -d: -f1)
  wrapper=$(rg -n '^#include "bl616cl_(lhal|sdk)\.h"' "$source" |
            head -n 1 | cut -d: -f1)
  if [[ -z "$wrapper" || "$wrapper" -ge "$first_lhal" ]]; then
    echo "ERROR: wrapper must precede LHAL headers: $source" >&2
    exit 1
  fi

  eval "$compile_prefix -dM -E '$source'" | \
    awk '$1 == "#define" && $2 ~ /^E[A-Z0-9_]+$/ {
           name = $2
           $1 = $2 = ""
           sub(/^  */, "")
           print name "\t" $0
         }' | sort -k1,1 > "$tmpdir/consumer"
  consumer_count=$(join -t $'\t' "$tmpdir/nuttx" "$tmpdir/consumer" |
                   wc -l)
  if [[ "$consumer_count" -ne "$nuttx_count" ]] ||
     ! join -t $'\t' "$tmpdir/nuttx" "$tmpdir/consumer" |
       awk -F '\t' '$2 != $3 {exit 1}'; then
    echo "ERROR: NuttX errno mismatch: $source" >&2
    exit 1
  fi
done < <(rg -l '^#include "bflb_' "$chip_dir"/*.c | sort)

echo "BL616CL errno compatibility checks passed"
