#!/usr/bin/env bash
#
# Phase 1 link check: compile every .c to a real object file, then work out
# which symbols nothing in the tree defines.
#
# Unlike survey.sh (syntax only) this runs full code generation, which catches
# things a syntax pass cannot -- notably PowerPC inline assembly, which only
# fails once the compiler tries to emit instructions for it.
#
# The output is the porting TODO: every symbol referenced by the game that no
# source file in this repo provides.
#
# Usage: tools/phase0/linkcheck.sh [-m32|-m64]
set -uo pipefail

CC="${CC:-gcc}"
BITS="${1:--m64}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${OUT:-$ROOT/build/phase1}"

cd "$ROOT"
rm -rf "$OUT"; mkdir -p "$OUT/obj"

# MWCC builds with -cwd source, so a file's own directory is searched for
# quoted includes. GCC does that too, but the Dolphin sources also reach
# sideways -- vi.c includes "__gx.h" from the gx directory -- so every source
# subdirectory goes on the path.
INCLUDES="-I src -I extern/dolphin/include -I extern/dolphin/include/libc -I extern/dolphin/src"
for d in $(find extern/dolphin/src -type d); do INCLUDES="$INCLUDES -I $d"; done
# src/MSL holds the Metrowerks standard library headers the game includes
# directly -- <printf.h>, <setjmp.h>, <wchar.h>. It goes last so its math.h and
# ctype.h do not shadow the decomp's own under extern/dolphin/include/libc.
INCLUDES="$INCLUDES -I src/MSL"

compile_one() {
  local f="$1" o l
  o="$OUT/obj/${f//\//_}.o"; l="$OUT/obj/${f//\//_}.log"
  # shellcheck disable=SC2086
  if "$CC" "$BITS" -w -c -O0 -fno-strict-aliasing \
       -include tools/phase0/compat.h $INCLUDES \
       -DVERSION_GALE01 -DBUILD_VERSION=0 "$f" -o "$o" 2>"$l"; then
    echo "OK   $f"
  else
    echo "FAIL $f"
  fi
}
export -f compile_one
export CC BITS OUT INCLUDES

echo "Compiling with $CC $BITS ..."
find src extern -name '*.c' \
  | xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {} \
  > "$OUT/results.txt" 2>&1

ok=$(grep -c '^OK' "$OUT/results.txt" || true)
total=$(wc -l < "$OUT/results.txt")
echo "  objects built: $ok / $total"
echo

cd "$OUT/obj"
nm -g --defined-only ./*.o 2>/dev/null | awk '{print $NF}' | sort -u > "$OUT/defined.txt"
nm -g -u          ./*.o 2>/dev/null | awk '{print $NF}' | sort -u > "$OUT/undefined.txt"
comm -23 "$OUT/undefined.txt" "$OUT/defined.txt" > "$OUT/todo.txt"

cd "$ROOT"
# A symbol is only a genuine gap if NO source file defines it. Anything defined
# in a file that merely failed to compile resolves itself once that file builds.
: > "$OUT/real_gaps.txt"
while read -r s; do
  grep -rqlE "^[a-zA-Z_].*\b${s}\s*\(" src extern --include=*.c 2>/dev/null \
    || echo "$s" >> "$OUT/real_gaps.txt"
done < "$OUT/todo.txt"

echo "======================================================"
printf "  unresolved symbols      : %s\n" "$(wc -l < "$OUT/todo.txt")"
printf "  ...defined in a file that failed to build : %s\n" \
  "$(( $(wc -l < "$OUT/todo.txt") - $(wc -l < "$OUT/real_gaps.txt") ))"
printf "  ...GENUINELY absent      : %s\n" "$(wc -l < "$OUT/real_gaps.txt")"
echo "======================================================"
echo
echo "Genuinely absent symbols ($OUT/real_gaps.txt):"
sed 's/^/  /' "$OUT/real_gaps.txt"
