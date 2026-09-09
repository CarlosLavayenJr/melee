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

# --trace-gx links pc/src/pc_gx_trace.c over a curated set of GX entry points,
# using ld --wrap so neither the SDK nor the game is modified. It reports the
# shape of each frame -- primitives, display lists, textures, TEV stages --
# which is the specification a renderer has to satisfy.
#
# --trace-dvd reports every disc read that resolves to a file, as the file's
# name, the offset within it and the length, and whether a byte-order schema
# claimed it. Writing a schema per format means knowing which files the boot
# actually touches and in what order; this is how that list is obtained.
#
# --renderer links pc/src/pc_vulkan.c and pc/src/pc_gx_render.c over the same
# curated set of GX entry points --trace-gx used to find, in place of the
# recorder: a real Win32 window and Vulkan swapchain instead of a log line.
# See pc/GX_RENDERER.md. Mutually exclusive with --trace-gx -- both wrap the
# same symbols, so only one wrapper set can own them -- and Windows-only for
# now, since it links against vulkan-1/gdi32/user32 directly.
TRACE_GX=0
TRACE_DVD=0
RENDERER=0
ARGS=""
for a in "$@"; do
  case "$a" in
    --trace-gx) TRACE_GX=1 ;;
    --trace-dvd) TRACE_DVD=1 ;;
    --renderer) RENDERER=1 ;;
    *) ARGS="$ARGS $a" ;;
  esac
done
# shellcheck disable=SC2086
set -- $ARGS

CC="${CC:-gcc}"
BITS="${1:--m64}"

# A Windows target is chosen by pointing CC at a MinGW compiler:
#   CC=i686-w64-mingw32-gcc tools/phase0/linkexe.sh -m32
# It needs a different host backend, a .exe suffix, and above all the
# large-address-aware flag: a 32-bit Windows process is otherwise limited to
# the low 2 GB, and pc_memory.c maps 0x80000000 and above, so every mapping
# fails without it.
WINDOWS=0
case "$CC" in *mingw*) WINDOWS=1 ;; esac
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${OUT:-$ROOT/build/phase2}"

cd "$ROOT"
rm -rf "$OUT"; mkdir -p "$OUT/obj"

# -fgnu89-inline matches how MWCC treats the `extern inline` math helpers;
# without it GCC emits one copy per translation unit and they collide.
# -std=gnu17 pins the language dialect: GCC 15+ defaults toward C23, where
# `bool` is a keyword, and src/MSL/stdbool.h's own `typedef int bool;` (which
# MWCC never objected to) fails to compile under that default.
# -Wno-error=implicit-function-declaration: GCC 14+ made implicit declarations
# a hard error where every older GCC just warned. The decomp calls PPC
# intrinsics like __cntlzw that MWCC provided as compiler builtins with no
# header to declare them; pc_ppc.c defines host equivalents, but nothing
# declares them before use. Compiling stopped there rather than linking fine,
# same as it always did -- this just un-upgrades the diagnostic.
CFLAGS="$BITS -w -c -O0 -g -fgnu89-inline -fno-strict-aliasing -std=gnu17"
CFLAGS="$CFLAGS -Wno-error=implicit-function-declaration"
# Same story for incompatible-pointer-types: also promoted to a hard error in
# GCC 14+. Interrupt handlers get registered against `struct Foo*` in one
# header and the typedef'd `Foo*` in another -- the same type, but stricter
# checking now refuses the mismatch outright instead of warning.
CFLAGS="$CFLAGS -Wno-error=incompatible-pointer-types"
if [ "$TRACE_DVD" = 1 ]; then
  CFLAGS="$CFLAGS -DPC_DVD_TRACE"
fi
if [ "$RENDERER" = 1 ]; then
  CFLAGS="$CFLAGS -DPC_GX_RENDERER"
fi
LDFLAGS="-lm"
FREESTANDING=0
STUBFLAGS=""
OUTBIN="melee_host"
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
  if [ "$WINDOWS" = 1 ]; then
    : # MinGW ships a complete 32-bit libc; the freestanding fallback is for
      # Linux hosts that lack one.
  elif ! echo '#include <stdio.h>' | "$CC" -m32 -x c -fsyntax-only - 2>/dev/null; then
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
  if [ "$WINDOWS" = 1 ]; then
    LDFLAGS="-Wl,--large-address-aware"
    OUTBIN="melee_host.exe"
    # MinGW brings a complete C library, and src/MSL is the decomp's own copy
    # of one: its ctype.c, string.c and friends define the same symbols and
    # collide at link. The freestanding Linux build needs MSL because there is
    # no libc there at all; Windows does not.
    # MSL's headers are needed -- sysdolphin/baselib/debug.c and sislib.c are
    # written against them -- but they assume they are the only libc headers
    # present and collide with MinGW's on size_t and friends. So the path is
    # ordered explicitly, as the Linux freestanding build does: the decomp's
    # own headers first, MinGW's behind them for <windows.h> and the rest.
    INCLUDES="-nostdinc -I pc/src -I extern/dolphin/include/libc -I src/MSL"
    INCLUDES="$INCLUDES -I tools/phase0/freestanding"
    INCLUDES="$INCLUDES -I $("$CC" -print-file-name=include)"
    INCLUDES="$INCLUDES -I /usr/$(echo "$CC" | sed 's/-gcc$//')/include"
    INCLUDES="$INCLUDES -I src -I extern/dolphin/include -I extern/dolphin/src"
    # pc/src is host code, not console code, and wants the opposite priority:
    # <windows.h> needs MinGW's real stdlib.h and stddef.h -- for malloc/free
    # (windows.h drags in xmmintrin.h, which needs them declared) and size_t --
    # and extern/dolphin/include/libc's own shadow copies must not pre-empt
    # them. -idirafter, unlike -I, is searched after the compiler's built-in
    # system directories rather than before, so it still resolves headers the
    # system doesn't have (dolphin/os/OS.h and friends) without shadowing the
    # ones it does.
    INCLUDES_PC="-I pc/src -I src -I extern/dolphin/include"
    INCLUDES_PC="$INCLUDES_PC -idirafter extern/dolphin/include/libc -I extern/dolphin/src"
    # MSL's implementations still duplicate msvcrt's, so those sources go.
    # float.c and math_data.c are MSL-private tables, not host libc symbols.
    # Excluding them silently replaced masks/polynomials with zeroed stubs.
    EXCLUDE_HOSTLIBC="src/MSL/(ctype|string|mem|mem_funcs|errno|rand|misc_io|abort_exit|uart_console_io|mbstring)\\.c|"
  elif ! echo 'int main(void){return 0;}' | "$CC" -m32 -x c -o /dev/null - 2>/dev/null; then
    echo "note: no 32-bit libc; linking freestanding (-nostdlib)"
    FREESTANDING=1
    CFLAGS="$CFLAGS -fno-stack-protector -DPC_FREESTANDING"
    STUBFLAGS="-nostdinc -fno-stack-protector"
    LDFLAGS="-nostdlib -static"
  fi
fi
# @file response files (below) are read by the linker itself, not by MSYS's
# exec wrapper, so the usual POSIX-to-Windows path rewrite that happens for
# plain argv never applies to them; ld.exe then can't resolve `/c/...` paths.
# cygpath -m fixes that up front. On real Linux there's no cygpath and none
# of this applies, so the paths pass through unchanged.
if command -v cygpath >/dev/null; then
  rsp_paths() { cygpath -m -f -; }
else
  rsp_paths() { cat; }
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
# Always wrapped, independent of --trace-gx/--renderer: pc_hsd_swap.c fixes
# up HSD_CObjDesc's still-big-endian non-pointer fields (flags, viewport,
# scissor, roll, near/far, and the projection-specific tail) before CObjLoad
# reads them -- a correctness fix, not a renderer feature, so it applies to
# every build the same way pc_dvd.c's archive schema does.
#
# pc_pad_alarm.c wraps HSD_PadGetRawQueueCount for the same reason: it pumps
# pc_os_time.c's OSAlarm queue (see pc_pad_alarm.c) so gm_801A4D34's
# pad-queue wait loop -- which polls this entry point directly and never
# sleeps or yields -- doesn't spin forever waiting on an alarm nothing else
# would ever deliver.
LDFLAGS="$LDFLAGS -Wl,--wrap=HSD_CObjInit -Wl,--wrap=HSD_CObjLoadDesc -Wl,--wrap=HSD_PadGetRawQueueCount -Wl,--wrap=HSD_PObjLoadDesc"
LDFLAGS="$LDFLAGS -Wl,--wrap=GXSetDrawDone -Wl,--wrap=GXDrawDone"
LDFLAGS="$LDFLAGS -Wl,--wrap=HSD_JObjLoadJoint -Wl,--wrap=HSD_WObjInit"
LDFLAGS="$LDFLAGS -Wl,--wrap=HSD_MObjLoadDesc -Wl,--wrap=HSD_TObjLoadDesc"
LDFLAGS="$LDFLAGS -Wl,--wrap=HSD_ArchiveParse -Wl,--wrap=lbArchiveRelocate"
LDFLAGS="$LDFLAGS -Wl,--wrap=psInitDataBankLocate"

WRAPPED="GXBegin GXCallDisplayList GXLoadTexObj GXSetTevOrder GXSetProjection GXLoadPosMtxImm GXCopyDisp"
# pc_gx_trace.c implements only the list above. pc_gx_render.c implements
# that same list plus these -- init and the clear color have no trace-mode
# equivalent, since a recorder has no swapchain to clear. The vertex-format
# calls are pc_gx_fifo.c's shadow state (pc/GX_RENDERER.md): a recorder has
# no use for them either, since it never decodes what a display list's
# bytes mean.
RENDERER_ONLY="GXInit GXInitTexObj GXSetCopyClear GXSetVtxDesc GXClearVtxDesc GXSetVtxAttrFmt GXSetArray GXSetTexCoordGen2"
if [ "$TRACE_GX" = 1 ] || [ "$RENDERER" = 1 ]; then
  for w in $WRAPPED; do LDFLAGS="$LDFLAGS -Wl,--wrap=$w"; done
fi
if [ "$RENDERER" = 1 ]; then
  for w in $RENDERER_ONLY; do LDFLAGS="$LDFLAGS -Wl,--wrap=$w"; done
  EXCLUDE_TRACE='pc/src/pc_gx_trace\.c|'
  LDFLAGS="$LDFLAGS -lvulkan-1 -lgdi32 -luser32"
elif [ "$TRACE_GX" != 1 ]; then
  # Without the flags the wrappers are dead weight and their __real_ references
  # would not resolve, so the recorder is left out of the build entirely.
  EXCLUDE_TRACE='pc/src/pc_gx_trace\.c|'
fi
if [ "$RENDERER" != 1 ]; then
  EXCLUDE_TRACE="${EXCLUDE_TRACE:-}"'pc/src/pc_vulkan\.c|pc/src/pc_gx_render\.c|pc/src/pc_gx_fifo\.c|pc/src/pc_gx_texture\.c|'
fi

EXCLUDE="${EXCLUDE_TRACE:-}${EXCLUDE_HOSTLIBC:-}"'dolphin/stub\.c|amcstubs|odemustubs|MetroTRK|dolphin/os/OS(Interrupt|Alarm|Time|Cache|Context|Reset|ResetSW|Thread)?\.c|MSL/printf\.c|dolphin/pad/pad\.c|dolphin/ar/ar\.c|dolphin/dsp/dsp(_task)?\.c|dolphin/dvd/dvdlow\.c'

compile_one() {
  local f="$1" o inc
  o="$OUT/obj/${f//\//_}.o"
  # Files under pc/src are the port layer: host code, compiled against host
  # headers. Everything else is the decomp, compiled against its own.
  case "$f" in
    pc/src/*) inc="${INCLUDES_PC:-$INCLUDES}" ;;
    *)        inc="$INCLUDES" ;;
  esac
  # shellcheck disable=SC2086
  "$CC" $CFLAGS -include tools/phase0/compat.h $inc \
    -DVERSION_GALE01 -DBUILD_VERSION=0 "$f" -o "$o" 2>/dev/null
}
export -f compile_one
export CC CFLAGS INCLUDES INCLUDES_PC OUT FREESTANDING STUBFLAGS

echo "Compiling ($CC $BITS)..."
find src extern pc/src -name '*.c' | grep -vE "$EXCLUDE" \
  | xargs -P "$(nproc)" -I{} bash -c 'compile_one "$@"' _ {} >/dev/null 2>&1
echo "  objects: $(find "$OUT/obj" -name '*.o' | wc -l)"

# Ask the linker which symbols are genuinely unresolved. Deriving this from nm
# alone over-reports: it does not know the host libc supplies sinf, printf,
# mmap and friends, and stubbing those shadows the real implementations.
# The object list goes through an @file: with hundreds of objects, the plain
# argv form overflows MSYS's exec argument limit on Windows ("Argument list
# too long") well under the nominal Win32 command-line size.
printf '%s\n' "$OUT"/obj/*.o | rsp_paths > "$OUT/objs.rsp"
# shellcheck disable=SC2086
"$CC" $BITS -o /dev/null "@$OUT/objs.rsp" $LDFLAGS 2>&1 \
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
# STUBFLAGS rather than ${FREESTANDING:+...}: that form expands whenever the
# variable is set and non-empty, and FREESTANDING is always set -- to 0 in the
# hosted case -- so it applied the freestanding flags to both.
# shellcheck disable=SC2086
"$CC" $BITS -w -c $STUBFLAGS "$OUT/stubs.c" -o "$OUT/stubs.o"
echo "Linking..."
printf '%s\n' "$OUT"/obj/*.o "$OUT/stubs.o" | rsp_paths > "$OUT/objs.rsp"
# shellcheck disable=SC2086
if "$CC" $BITS -o "$OUT/$OUTBIN" "@$OUT/objs.rsp" $LDFLAGS 2>"$OUT/link.log"; then
  echo "  linked: $(du -h "$OUT/$OUTBIN" | cut -f1)"
else
  echo "  LINK FAILED — see $OUT/link.log"; head -5 "$OUT/link.log"; exit 1
fi

echo
echo "Running (expect a crash — that is the point)..."
if command -v gdb >/dev/null; then
  timeout 30 gdb -batch -ex run -ex bt "$OUT/$OUTBIN" 2>&1 \
    | grep -E "SIGSEGV|SIGABRT|^#[0-9]|exited" | head -10 | sed 's/^/  /'
else
  timeout 15 "$OUT/$OUTBIN"; echo "  exit: $?"
fi
