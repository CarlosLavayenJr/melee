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
LDFLAGS="-lm"
FREESTANDING=0
# MWCC builds with -cwd source, so a file's own directory is searched for
# quoted includes. GCC does that too, but the Dolphin sources also reach
# sideways -- vi.c includes "__gx.h" from the gx directory -- so every source
# subdirectory goes on the path.
INCLUDES="-I src -I extern/dolphin/include -I extern/dolphin/include/libc -I extern/dolphin/src -I pc/src"

# -m32 is the real target. Compiling there works anywhere via the freestanding
# fallback (see survey.sh), but LINKING additionally needs 32-bit crt and libc
# *libraries*, which headers alone cannot supply -- install gcc-multilib. Fail
# early and say so rather than emitting a wall of ld errors.
if [ "$BITS" = "-m32" ]; then
  if ! echo '#include <stdio.h>' | "$CC" -m32 -x c -fsyntax-only - 2>/dev/null; then
    # src/MSL precedes the freestanding headers deliberately. It is the decomp's
    # own standard library -- string.c, printf.c, strtoul.c, math.c and trigf.c
    # implement what the game calls -- and those sources only compile against
    # their own declarations. tools/phase0/freestanding then fills the gaps MSL
    # does not cover, rather than shadowing what it does.
    # extern/dolphin/include/libc precedes src/MSL. Both ship a math.h, but
    # MSL's pulls in Runtime/platform.h, which deliberately #undefs BOOL, TRUE
    # and FALSE -- and SDK sources like gx/GXDraw.c still use them. MSL's own
    # sources are unaffected by the order because they include their headers
    # with quotes, which searches their directory first.
    INCLUDES="-nostdinc -I extern/dolphin/include/libc -I src/MSL -I tools/phase0/freestanding -I $("$CC" -print-file-name=include) $INCLUDES"
  fi
  # Where 32-bit crt and libc libraries are missing, link freestanding instead
  # of giving up: -nostdlib with our own _start and raw syscalls. src/MSL
  # already supplies memcpy, printf, the string routines and most of the math,
  # so pc_libc.c only has to add sqrt, sqrtf, floor and atanf.
  if ! echo 'int main(void){return 0;}' | "$CC" -m32 -x c -o /dev/null - 2>/dev/null; then
    echo "note: no 32-bit libc; linking freestanding (-nostdlib)"
    FREESTANDING=1
    CFLAGS="$CFLAGS -fno-stack-protector -DPC_FREESTANDING"
    LDFLAGS="-nostdlib -static"
  fi
fi
for d in $(find extern/dolphin/src -type d); do INCLUDES="$INCLUDES -I $d"; done
# src/MSL is added only alongside the freestanding headers. Its stddef.h types
# intptr_t as `int`, which is right for the 32-bit ABI the decomp targets and
# collides with the host's 64-bit definition when glibc's headers are also
# visible. Reaching MSL therefore requires -nostdinc, i.e. the -m32 path.

# stub.c / amcstubs / odemustubs are alternative implementations the real build
# chooses between (see the Object() list in configure.py); MetroTRK is the
# debug monitor. Linking all of them at once produces duplicate symbols.
# OS.c is excluded too: its exception vectors and context switching are MWCC
# `asm void` bodies with no host equivalent. pc/src/pc_os.c replaces it.
# MSL/printf.c is excluded too: it compiles but is written against MWCC's
# varargs intrinsics (__builtin_va_info), which have no host counterpart.
# pc/src/pc_printf.c replaces it.
EXCLUDE='dolphin/stub\.c|amcstubs|odemustubs|MetroTRK|dolphin/os/OS(Interrupt|Alarm|Time|Cache|Context|Reset|ResetSW|Thread)?\.c|MSL/printf\.c|dolphin/pad/pad\.c'

compile_one() {
  local f="$1" o
  o="$OUT/obj/${f//\//_}.o"
  # shellcheck disable=SC2086
  "$CC" $CFLAGS -include tools/phase0/compat.h $INCLUDES \
    -DVERSION_GALE01 -DBUILD_VERSION=0 "$f" -o "$o" 2>/dev/null
}
export -f compile_one
export CC CFLAGS INCLUDES OUT FREESTANDING

echo "Compiling ($CC $BITS)..."
find src extern pc/src -name '*.c' | grep -vE "$EXCLUDE" \
  | xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {} >/dev/null 2>&1
echo "  objects: $(find "$OUT/obj" -name '*.o' | wc -l)"

# Ask the linker which symbols are genuinely unresolved. Deriving this from nm
# alone over-reports: it does not know the host libc supplies sinf, printf,
# mmap and friends, and stubbing those shadows the real implementations.
# shellcheck disable=SC2086
"$CC" $BITS -o /dev/null "$OUT"/obj/*.o $LDFLAGS 2>&1 \
  | grep -oE "undefined reference to \`[^']*'" \
  | sed "s/.*\`//; s/'//" | sort -u > "$OUT/todo.txt"

python3 - "$OUT" <<'PY'
import sys
out = sys.argv[1]
syms = [l.strip() for l in open(f'{out}/todo.txt') if l.strip()]
with open(f'{out}/stubs.c', 'w') as f:
    # No includes: these are plain char arrays, and a freestanding build has
    # no header search path to satisfy one with.
    f.write('/* Placeholders for symbols nothing provides -- neither the tree nor\n'
            '   the host libc. Sized generously and untyped, since the linker\n'
            '   cannot say what is code and what is data. Calling one crashes,\n'
            '   which is the point: that is where boot stops. */\n')
    for s in syms:
        f.write(f'__attribute__((aligned(16))) char {s}[4096];\n')
print(f'  placeholders: {len(syms)}')
PY

cd "$ROOT"
# shellcheck disable=SC2086
"$CC" $BITS -w -c ${FREESTANDING:+-nostdinc -fno-stack-protector} "$OUT/stubs.c" -o "$OUT/stubs.o"
echo "Linking..."
# shellcheck disable=SC2086
if "$CC" $BITS -o "$OUT/melee_host" "$OUT"/obj/*.o "$OUT/stubs.o" $LDFLAGS 2>"$OUT/link.log"; then
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
