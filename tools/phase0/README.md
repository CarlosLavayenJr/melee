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
