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
  objects: 1047
  placeholders: 787
  linked: 14M
  Program received signal SIGSEGV
  #0  OSInit ()
  #1  main ()
```

A host executable builds and reaches the game's real entry point. It then
segfaults inside `OSInit()` — the first thing `main()` calls — because that
function programs GameCube hardware registers that do not exist here.

**This is the shape of the whole port in one backtrace.** Nothing is missing;
`OSInit` is present and decompiled. It simply does something only a GameCube
can do. The work is replacing implementations, not supplying absent ones.

## Use the crash point as the progress metric

The binary is not a game and cannot become one by adding placeholders. Its
value is the position of that crash. Implement a host `OSInit`, and boot moves
to whatever runs next. That backtrace walking further forward is the most
honest progress signal available before there is anything to look at.

## Build notes worth keeping

- `-fgnu89-inline` is required. The `extern inline` math helpers are emitted
  once by MWCC but once *per translation unit* by GCC, which collide at link.
- `stub.c`, `amcstubs` and `odemustubs` are alternative implementations the
  real build picks between (see the `Object()` list in `configure.py`).
  Compiling all of them together produces duplicate symbols.
- Do not supply your own `main()`. The game has one, in
  `src/melee/gm/gmmain.c`.
