#!/usr/bin/env bash
#
# Phase 0 survey: how much of the decomp compiles with a host compiler?
#
# This does NOT build anything runnable. It runs a syntax-only pass over every
# .c file to measure how far the source is from compiling off PowerPC/MWCC,
# then groups the failures by root cause.
#
# Usage:
#   tools/phase0/survey.sh              # host word size (64-bit typically)
#   tools/phase0/survey.sh -m32         # 32-bit, the real port target
#   CC=clang tools/phase0/survey.sh     # try a different compiler
#
set -uo pipefail

CC="${CC:-gcc}"
BITS="${1:--m64}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${OUT:-$ROOT/build/phase0}"

cd "$ROOT"
rm -rf "$OUT"
mkdir -p "$OUT/logs"

# Note: kept as a flat string, not an array -- bash cannot export arrays into
# the subshells that xargs spawns, and the decomp's own libc must precede the
# host's on the include path or its math.h loses to glibc's.
INCLUDES="-I src -I extern/dolphin/include -I extern/dolphin/include/libc -I extern/dolphin/src"

# -m32 is the port's real target (ACGC-PC-Port builds mingw-w64-i686), but it
# needs 32-bit libc headers that a plain x86-64 box may not have. Rather than
# require gcc-multilib just to measure, fall back to freestanding: the decomp
# pulls in only string.h, stdio.h, ctype.h and stdint.h from the host, and
# tools/phase0/freestanding supplies declaration-only versions of each. The
# survey never links, so declarations are enough, and GCC's own stddef.h /
# stdarg.h / stdbool.h are already width-correct under -m32.
if [ "$BITS" = "-m32" ] && ! echo '#include <stdio.h>' \
     | "$CC" -m32 -x c -fsyntax-only - 2>/dev/null; then
  echo "note: no 32-bit libc headers; using tools/phase0/freestanding"
  # src/MSL precedes the freestanding headers deliberately. It is the decomp's
    # own standard library -- string.c, printf.c, strtoul.c, math.c and trigf.c
    # implement what the game calls -- and those sources only compile against
    # their own declarations. tools/phase0/freestanding then fills the gaps MSL
    # does not cover, rather than shadowing what it does.
    INCLUDES="-nostdinc -I src/MSL -I tools/phase0/freestanding -I $("$CC" -print-file-name=include) $INCLUDES"
fi
# The Dolphin sources reach sideways for private headers -- vi.c includes
# "__gx.h" from the gx directory -- which MWCC resolves via -cwd source.
for d in $(find extern/dolphin/src -type d); do INCLUDES="$INCLUDES -I $d"; done
# src/MSL is added only alongside the freestanding headers. Its stddef.h types
# intptr_t as `int`, which is right for the 32-bit ABI the decomp targets and
# collides with the host's 64-bit definition when glibc's headers are also
# visible. Reaching MSL therefore requires -nostdinc, i.e. the -m32 path.

compile_one() {
  local f="$1"
  local log="$OUT/logs/${f//\//_}.log"
  # shellcheck disable=SC2086  # INCLUDES is intentionally word-split
  if "$CC" "$BITS" -w -fsyntax-only \
       -include tools/phase0/compat.h \
       $INCLUDES -DVERSION_GALE01 -DBUILD_VERSION=0 \
       "$f" 2>"$log"; then
    echo "OK   $f"
  else
    echo "FAIL $f"
  fi
}
export -f compile_one
export CC BITS OUT ROOT INCLUDES

echo "Compiler: $CC $BITS"
echo "Surveying $(find src extern -name '*.c' | wc -l) source files..."
echo

find src extern -name '*.c' \
  | xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {} \
  > "$OUT/results.txt" 2>&1

total=$(wc -l < "$OUT/results.txt")
ok=$(grep -c '^OK' "$OUT/results.txt" || true)
fail=$(grep -c '^FAIL' "$OUT/results.txt" || true)

echo "======================================================"
printf "  compiles clean : %s / %s (%s%%)\n" "$ok" "$total" "$((ok * 100 / total))"
printf "  fails          : %s\n" "$fail"
echo "======================================================"
echo
echo "Failures by area (SDK/libc rows get replaced by a port, not fixed):"
{
  printf "  %-34s %s\n" "game code (src/melee)"       "$(grep '^FAIL' "$OUT/results.txt" | grep -c 'src/melee/'   || true)"
  printf "  %-34s %s\n" "HSD engine (src/sysdolphin)" "$(grep '^FAIL' "$OUT/results.txt" | grep -c 'src/sysdolphin/' || true)"
  printf "  %-34s %s\n" "SDK/libc (replaced by port)" "$(grep '^FAIL' "$OUT/results.txt" | grep -cE 'extern/dolphin|src/MSL|src/MetroTRK|src/Runtime' || true)"
}
echo
echo "Top error root causes:"
grep -h "error:" "$OUT"/logs/*.log 2>/dev/null \
  | sed 's/.*error: //' \
  | sed "s/'[^']*'/'X'/g; s/[0-9]\+/N/g" \
  | sort | uniq -c | sort -rn | head -12 \
  | sed 's/^/  /'
echo
echo "Full results: $OUT/results.txt"
echo "Per-file logs: $OUT/logs/"
