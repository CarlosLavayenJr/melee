#!/usr/bin/env bash
#
# Phase 2 smoke test: link a host executable and see how far it gets.
#
# Compiles what compiles, generates a placeholder for every symbol nothing in
# the tree defines, links the result against the game's own main(), runs it,
# and reports where it dies.
#
# The binary is NOT a playable game -- the hundreds of placeholders make sure
# of that. Its value is the crash point: as the port replaces GameCube
# implementations with host ones, the crash moves further into boot. That
# position is the progress metric.
#
# Usage: tools/phase0/linkexe.sh [-m32|-m64]
set -uo pipefail

CC="${CC:-gcc}"
BITS="${1:--m64}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${OUT:-$ROOT/build/phase2}"

cd "$ROOT"
rm -rf "$OUT"; mkdir -p "$OUT/obj"

# -fgnu89-inline matches how MWCC treats the `extern inline` math helpers;
# without it GCC emits one copy per translation unit and they collide.
CFLAGS="$BITS -w -c -O0 -fgnu89-inline -fno-strict-aliasing"
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

# stub.c / amcstubs / odemustubs are alternative implementations the real build
# chooses between (see the Object() list in configure.py); MetroTRK is the
# debug monitor. Linking all of them at once produces duplicate symbols.
# OS.c is excluded too: its exception vectors and context switching are MWCC
# `asm void` bodies with no host equivalent. pc/src/pc_os.c replaces it.
EXCLUDE='dolphin/stub\.c|amcstubs|odemustubs|MetroTRK|dolphin/os/OS\.c'

compile_one() {
  local f="$1" o
  o="$OUT/obj/${f//\//_}.o"
  # shellcheck disable=SC2086
  "$CC" $CFLAGS -include tools/phase0/compat.h $INCLUDES \
    -DVERSION_GALE01 -DBUILD_VERSION=0 "$f" -o "$o" 2>/dev/null
}
export -f compile_one
export CC CFLAGS INCLUDES OUT

echo "Compiling ($CC $BITS)..."
find src extern pc/src -name '*.c' | grep -vE "$EXCLUDE" \
  | xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {} >/dev/null 2>&1
echo "  objects: $(find "$OUT/obj" -name '*.o' | wc -l)"

# Ask the linker which symbols are genuinely unresolved. Deriving this from nm
# alone over-reports: it does not know the host libc supplies sinf, printf,
# mmap and friends, and stubbing those shadows the real implementations.
# shellcheck disable=SC2086
"$CC" $BITS -o /dev/null "$OUT"/obj/*.o -lm 2>&1 \
  | grep -oE "undefined reference to \`[^']*'" \
  | sed "s/.*\`//; s/'//" | sort -u > "$OUT/todo.txt"

python3 - "$OUT" <<'PY'
import sys
out = sys.argv[1]
syms = [l.strip() for l in open(f'{out}/todo.txt') if l.strip()]
with open(f'{out}/stubs.c', 'w') as f:
    f.write('/* Placeholders for symbols nothing provides -- neither the tree nor\n'
            '   the host libc. Sized generously and untyped, since the linker\n'
            '   cannot say what is code and what is data. Calling one crashes,\n'
            '   which is the point: that is where boot stops. */\n#include <stddef.h>\n')
    for s in syms:
        f.write(f'__attribute__((aligned(16))) char {s}[4096];\n')
print(f'  placeholders: {len(syms)}')
PY

cd "$ROOT"
# shellcheck disable=SC2086
"$CC" $BITS -w -c "$OUT/stubs.c" -o "$OUT/stubs.o"
echo "Linking..."
# shellcheck disable=SC2086
if "$CC" $BITS -o "$OUT/melee_host" "$OUT"/obj/*.o "$OUT/stubs.o" -lm 2>"$OUT/link.log"; then
  echo "  linked: $(du -h "$OUT/melee_host" | cut -f1)"
else
  echo "  LINK FAILED — see $OUT/link.log"; head -5 "$OUT/link.log"; exit 1
fi

echo
echo "Running (expect a crash — that is the point)..."
if command -v gdb >/dev/null; then
  timeout 30 gdb -batch -ex run -ex bt "$OUT/melee_host" 2>&1 \
    | grep -E "SIGSEGV|SIGABRT|^#[0-9]|exited" | head -10 | sed 's/^/  /'
else
  timeout 15 "$OUT/melee_host"; echo "  exit: $?"
fi
