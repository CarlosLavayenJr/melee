# Melee, running as a native program

This directory is a host port of the Super Smash Bros. Melee decompilation. The
game's own C code is compiled for x86 and the GameCube hardware underneath it is
replaced, file by file, with implementations that run on a PC.

It **boots**. It initialises video, audio, input and the disc drive, runs its
own frame loop, and asks for game data. It does **not** draw anything yet —
there is no renderer, which is the next large piece of work.

No game data is included here and none can be. You need a disc image dumped
from your own copy.

---

## What you need

| | |
|---|---|
| A 32-bit C compiler | `gcc-multilib` on Linux, `mingw-w64-i686-gcc` for Windows |
| A Melee disc image | NTSC 1.02, dumped from your own disc |
| Python 3 | used by the build scripts |

**32-bit is not optional.** The decomp defines `u32` as `unsigned long`, which
is four bytes on the GameCube and on any 32-bit host, and eight on 64-bit Linux.
A 64-bit build compiles and even runs, but every struct containing a `u32` has
the wrong layout, so nothing it does can be trusted. See
`tools/phase0/README.md` for the full story.

---

## Build and run

### Linux

```sh
sudo apt install gcc-multilib          # if you have it; see the note below
tools/phase0/linkexe.sh -m32
cp /path/to/your/melee.iso game.iso
./build/phase2/melee_host
```

If 32-bit libc libraries are missing, the script says so and links
**freestanding** instead — `-nostdlib`, with its own entry point and raw
syscalls. That path needs nothing installed and is what most of this was
developed against, so a bare machine works fine.

### Windows

Install [MSYS2](https://www.msys2.org/), then from the **MINGW32** shell:

```sh
pacman -S mingw-w64-i686-gcc python
CC=i686-w64-mingw32-gcc tools/phase0/linkexe.sh -m32
cp /path/to/your/melee.iso game.iso
./build/phase2/melee_host.exe
```

Cross-compiling from Linux works too, with the same `CC=` line.

> **Not yet run on real Windows.** It compiles, links, and carries the right PE
> flag, but no Windows machine has executed it. If it dies immediately, see
> *It fails on Windows before printing anything* below.

### WSL2

Follow the Linux instructions. This is the least troublesome option on a
Windows machine.

### Where the disc image goes

Beside the binary you run, named `game.iso`. `melee.iso`, `disc.iso`,
`game.gcm` and `melee.gcm` also work. Without one the game still boots — it
just finds an empty file system, which is what a console with a blank disc
would do.

---

## What you should see

```
pc_memory: mapped 24 MB RAM and 64 KB MMIO
pc_dvd: mounted game.iso
pc_dvd: file system table, 1247 entries
pc_os: arena set, 23 MB
app booted from bootrom
pc_dsp: no DSP; audio tasks complete immediately
pc_ar: 16 MB ARAM (host memory)
pc_ai: audio interface present, playback disabled
```

`app booted from bootrom` is the game's own message, not the port's. Reaching
it means every subsystem `main()` sets up has returned.

The entry count tells you the disc parsed correctly — a real Melee disc has
roughly a thousand. `2 entries` means it is reading a synthetic test image, and
`file system table is malformed` means the header offsets did not make sense.

After that it will try to load real assets and stop, because nothing converts
them yet. See *Where this actually is* below.

---

## Seeing what it tries to draw

```sh
tools/phase0/linkexe.sh -m32 --trace-gx
```

Intercepts a curated set of graphics calls at link time and reports the shape
of each frame:

```
gx: GXCallDisplayList 2048 bytes
gx: frame 0 -- 12 prims (96 verts), 34 display lists (61440 bytes),
              8 textures, 16 tev stages, 41 matrices
```

This exists because there is no renderer, and writing one blind means guessing
which of the graphics API's ~114 entry points matter. The tally answers that
from what the game actually does. The display-list counter is the interesting
one: Melee draws nearly everything by replaying command streams precompiled
into the `.dat` files, so a large number there means the renderer's main job is
interpreting those, not immediate-mode drawing.

---

## Troubleshooting

**It fails on Windows before printing anything.**
The port maps memory at `0x80000000` and above, because the game hardcodes
those addresses. A 32-bit Windows process is normally limited to the low 2 GB,
so the binary must be marked large-address-aware. The build passes
`-Wl,--large-address-aware`; confirm it survived:

```sh
python3 -c "import struct,sys; d=open(sys.argv[1],'rb').read(); \
  pe=struct.unpack_from('<I',d,0x3C)[0]; \
  print('aware' if struct.unpack_from('<H',d,pe+22)[0]&0x20 else 'NOT AWARE')" \
  build/phase2/melee_host.exe
```

**`pc_memory: could not map cached RAM`.**
Something already occupies those addresses, or the process cannot reach them.
On Linux this usually means a hardened kernel restricting low/high mappings; on
Windows, the flag above.

**`pc_dvd: file system table is malformed`.**
The image is not a plain GameCube disc image. Compressed formats (CISO, NKit,
RVZ) are not supported — decompress to a raw `.iso`/`.gcm` first.

**It hangs instead of crashing.**
Some hardware the port replaces is polled in a loop rather than waited on, so a
missing piece stalls rather than faults. Attach a debugger and get a backtrace;
the top frame names what is missing.

---

## Where this actually is

| | |
|---|---|
| Game source compiling | 1138 of 1182 files |
| Unimplemented symbols | 33 |
| Boot | completes; reaches asset loading |
| Graphics | initialises; no renderer |
| Audio | initialises; silent |
| Input | accepted; no device is read |

**The next real milestone** is loading one asset archive far enough to reach the
title screen. That means byte-swapping its contents: the disc is big-endian
because the console is, and the file system table was the easy case. Every
`.dat` is full of big-endian floats and pointers, and converting them needs a
schema per format.

After that, a renderer — with the trace above as its specification.

---

## What is in here

| File | Replaces | |
|---|---|---|
| `pc_memory.c` | — | maps RAM and hardware registers at the console's addresses |
| `pc_bootinfo.c` | the boot loader | publishes the low-memory block the SDK reads |
| `pc_dvd.c` | `dvdlow.c` | serves the disc from an image file |
| `pc_sys_*.c` | — | host services: Linux syscalls, POSIX, or Win32 |
| `pc_os*.c` | `OS*.c` | arena, interrupts, threads, timers, contexts, reset |
| `pc_ar.c` `pc_dsp.c` `pc_ai.c` `pc_axfx.c` | the audio hardware | |
| `pc_pad.c` | `pad.c` | controllers |
| `pc_exi.c` | `exi`/`si` | peripheral buses |
| `pc_mtx.c` `pc_vec.c` | `mtx.c` `vec.c` | maths, replacing paired-single assembly |
| `pc_libc.c` `pc_printf.c` `pc_ppc.c` | — | the gaps a freestanding build leaves |
| `pc_gx_trace.c` | — | the graphics recorder |

Two of these are **not bit-identical** to the originals and are marked as such
in the source: `atanf` in `pc_libc.c`, and the matrix and vector routines. The
console's fused multiply-add rounds once where a separate multiply and add
round twice. Melee is deterministic and its physics run through these, so
anything depending on frame-exact behaviour needs them matched first.

`tools/phase0/README.md` is the engineering log — how each conclusion was
reached, and the traps that cost time.
