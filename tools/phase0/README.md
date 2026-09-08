# Phase 0 — host-compiler survey

Measures how much of the decomp compiles with a normal host compiler
(GCC/Clang, x86) instead of MWCC/PowerPC. This is the first checkpoint for any
native-port effort: it answers "how far is this source from building off the
GameCube toolchain?" before any porting work is committed to.

It runs a **syntax-only** pass (`-fsyntax-only`) over every `.c` file and groups
the failures by root cause. Nothing is linked and nothing runnable is produced.

## Usage

```sh
tools/phase0/survey.sh          # host word size
tools/phase0/survey.sh -m32     # 32-bit — the real port target
CC=clang tools/phase0/survey.sh # different compiler
```

Results land in `build/phase0/` (`results.txt` plus a per-file log directory).

## Baseline

As of the first run, on `gcc -m64` (Ubuntu 13.3):

| | |
|---|---|
| Compiles clean | **1069 / 1182 (90%)** |
| Fails | 113 |

Of the 113 failures: 67 are SDK/libc code (`extern/dolphin`, `MSL`, `MetroTRK`)
that a port *replaces* rather than fixes — you do not port `__init_hardware`
to x86, you delete it and let the host layer take over. The genuine remainder
is ~46 files, concentrated in `gm/` (boot and scene management, which talks to
OS/DVD/CARD directly) plus 9 files containing inline PowerPC assembly.

Getting from 37% to 90% took three configuration fixes, all in
`compat.h` — no source changes:

1. Let `platform.h`'s `ssize_t` win over glibc's (they agree on 32-bit).
2. Put the decomp's bundled `libc/` headers ahead of the host's on the include
   path, and pull in `stdint.h` for `intptr_t`/`uintptr_t`.
3. Define `M_PI` / `M_PI_2`.

Those three accounted for roughly 1,500 of the original errors: 565 were a
single symbol (`fabsf`) losing to glibc's declaration, and 746 were `M_PI`.

## Caveats

- **Syntax-only is necessary, not sufficient.** Code generation and linking
  will surface more.
- **The baseline above is `-m64`.** A real port targets 32-bit. The
  `ToyEDNData` `offsetof` assertion failures are artifacts of 64-bit pointers
  and should resolve under `-m32` (which needs 32-bit libc headers installed —
  `gcc-multilib` on Debian/Ubuntu).
- **Compiling is not working.** MWCC's `-align powerpc` struct packing versus
  the host's rules is something a syntax check cannot detect. The `offsetof`
  static assertions in this codebase exist precisely because layout matters;
  treat them as the real risk, not the compile errors.
- Warnings are suppressed (`-w`) to keep the signal on hard errors. Drop that
  flag when you start caring about correctness rather than reachability.

---

# Phase 1 — link check

`tools/phase0/linkcheck.sh` goes a step further: it compiles every file to a
real object file (full code generation, not just parsing) and then reports
which symbols nothing in the tree defines.

```sh
tools/phase0/linkcheck.sh [-m32|-m64]
```

## Why code generation matters

A syntax pass and a real compile disagree. Before the `math.h` fix below, 1069
files passed `-fsyntax-only` but only **403** produced object files. The
difference was one header: `sqrtf`/`sqrt` in `extern/dolphin/include/libc/math.h`
were implemented with the PowerPC `frsqrte` instruction and PPC float-register
constraints. GCC accepts that text while parsing and only rejects it when it
tries to emit instructions — so it fails 666 files at codegen and none at
syntax check. That header now has a host fallback guarded on `__PPC__`.

## Result

| | |
|---|---|
| Objects built | 1069 / 1182 |
| Unresolved symbols | 790 |
| ...defined in a file that merely failed to build | 758 |
| **...genuinely absent from the tree** | **32** |

The 32 fall into four groups:

- **Compiler intrinsics** MWCC provides and GCC does not: `__cntlzw`, `__dcbz`,
  `__sync`, `__fpclassifyf`, `floor`. Small host equivalents.
- **Hardware register blocks**: `__cpReg`, `__memReg`, `__peReg`, `__piReg`,
  `gx`. These are memory-mapped I/O — the actual silicon. This is where a port
  does its real work.
- **Data extracted from the retail DOL**: `HSD_DebugFontAtlas`,
  `HSD_SisLib_FontAtlas`, `stage_info`, and the `un_*` / `gmClassic_*` symbols.
  See the `extract:` section of `config/GALE01/config.yml` — these come from
  the original binary, which is why a build needs your own copy of the game.
- **Toolchain-provided**: `_GLOBAL_OFFSET_TABLE_`, `__stack_chk_fail`.

## The important caveat

**Linking is not working.** Because the Dolphin SDK is itself decompiled, the
game is nearly self-contained: fix the 113 files and almost everything
resolves. But those SDK implementations write to GameCube hardware addresses.
An exe built this way links and then dies the moment it touches `__piReg`.

The porting job is therefore *replacing* implementations, not supplying missing
ones — which is a different and larger task than this symbol count suggests.

---

# Phase 2 — link an executable

`tools/phase0/linkexe.sh` compiles what compiles, generates a placeholder for
every symbol nothing in the tree defines, links against the game's own
`main()`, runs the result, and reports where it dies.

```sh
tools/phase0/linkexe.sh [-m32|-m64]
```

## Result

```
  objects: 1049
  placeholders: 765
  linked: 14M

  pc_memory: mapped 24 MB at 0x80000000 and 0xc0000000
  pc_os: arena 0x80100000 .. 0x81800000 (23 MB)

  Program received signal SIGSEGV
  #0  VIInit ()
  #1  main ()
```

`OSInit()` completes against the host layer in `pc/` and boot reaches
`VIInit()`, the next call in `main()`.

## Use the crash point as the progress metric

The binary is not a game. Its value is where it stops. `main()` in
`src/melee/gm/gmmain.c` opens with

```c
OSInit(); VIInit(); DVDInit(); PADInit(); CARDInit(); OSInitAlarm();
```

so the crash walks that list as each subsystem gets a host implementation.
That is the most honest progress signal available before there is anything to
look at.

## Build notes worth keeping

- `-fgnu89-inline` is required. The `extern inline` math helpers are emitted
  once by MWCC but once *per translation unit* by GCC, which collide at link.
- `stub.c`, `amcstubs` and `odemustubs` are alternative implementations the
  real build picks between (see the `Object()` list in `configure.py`).
  Compiling all of them together produces duplicate symbols.
- `OS.c` cannot build off PowerPC -- exception vectors, FPR setup and context
  switching are MWCC `asm void` bodies. `pc/src/pc_os.c` replaces it.
- Do not supply your own `main()`. The game has one, in
  `src/melee/gm/gmmain.c`.
- Derive the placeholder list from **linker** output, not from `nm`. `nm` does
  not know the host libc already supplies `sinf`, `cosf`, `printf`, `mmap` and
  17 others; stubbing those shadows the real implementations and crashes in
  confusing places.

## The memory map is the load-bearing trick

Melee addresses memory by absolute GameCube addresses -- `OSPhysicalToCached(0)`
expands to the literal pointer `0x80000000`. Rather than rewrite every such
site, `pc/src/pc_memory.c` maps real pages at those addresses before `main()`
runs, so the game's own pointer arithmetic stays valid. It maps main RAM at
`0x80000000` and `0xC0000000` and the hardware register range at `0xCC000000`,
because `VIInit` reads `__VIRegs[1]` almost immediately.

Backing MMIO with ordinary pages only stops the fault. Reads return whatever
was last written rather than real device state, so code that polls a status bit
will spin rather than crash. Devices get intercepted individually as boot
reaches them.

This works on Linux x86-64 today; a Windows port needs the equivalent
`VirtualAlloc(MEM_RESERVE)` at the same base.

---

# Why -m32 is mandatory, not a preference

`extern/dolphin/include/dolphin/types.h` defines the base widths as

```c
typedef signed long s32;
typedef unsigned long u32;
```

`long` is 4 bytes in the GameCube's 32-bit ABI, so this is correct there and on
any ILP32 host. On an **LP64** host `long` is 8 bytes, and the consequences are
not subtle:

- Every `u32`/`s32` **struct field doubles in width**, so every layout that the
  200 `ASSERT_SIZE`/`offsetof` guards describe is wrong.
- `(u32)` casts stop truncating. `OS_BASE_CACHED` is `(0x8000 << 16)`, which is
  a *negative* `int`; widening it to 64 bits sign-extends, so
  `OSPhysicalToCached(0xCC)` yields `0xffffffff800000cc` instead of
  `0x800000cc`. That is the crash at `vi.c:294`.

Pinning `s32`/`u32` to `int` for non-PowerPC targets fixes the sign extension
but does not fix the codebase: the Dolphin headers also spell the same types
as bare `unsigned long` in a dozen places (`OSRtc.h`, `OSThread.h`, `pad.h`,
`perf.h`, `demo.h`, ...), which then conflict with the `u32` spellings in
`os.h`. Trying it drops the build from 1062 objects to 81. It was reverted.

**So the `-m64` numbers in this document prove the pipeline, not the port.**
The build compiles, links, and boots into real game code, which is what they
were for. Anything depending on struct layout or pointer width is invalid until
the same run happens under `-m32` -- which is the real target anyway, and what
ACGC-PC-Port uses (`mingw-w64-i686`).

## Result under -m32: struct layout is compatible

`tools/phase0/survey.sh -m32` now runs without `gcc-multilib`. When 32-bit libc
headers are missing it falls back to `tools/phase0/freestanding/`, which
supplies declaration-only `string.h`, `stdio.h`, `ctype.h` and `stdint.h` --
the only host headers the decomp reaches for. The survey never links, so
declarations suffice, and GCC's own `stddef.h`/`stdarg.h`/`stdbool.h` are
already width-correct under `-m32`.

| | `-m64` | `-m32` |
|---|---|---|
| Compiles clean | 1080 / 1182 (91%) | **1103 / 1182 (93%)** |
| Game-code failures | 40 | **11** |
| **Layout assertion failures** | **~180** | **0** |

**All 200 `ASSERT_SIZE`/`offsetof` guards pass under `-m32`.** GCC's x86 32-bit
struct layout matches what MWCC produced for PowerPC, for every structure the
codebase checks. Struct packing was the one risk a compiler could not be
trusted to catch and the one most likely to produce silent garbage rather than
errors; it is now measured rather than assumed.

The remaining 79 failures are MWCC assembly syntax -- `asm void` bodies,
register parameters, `@ha`/`@l` relocation operators -- and 59 of them are in
SDK/libc files a port replaces anyway. Only 11 game-code files remain:
`lbcardnew.c`, seven under `gr/`, the two `textlib` files, and `efalt.c`
(which uses MWCC's `__va_arg` idiom from `src/MSL/stdarg.h`).

