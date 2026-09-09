# A real renderer for the GX layer

`pc_gx_trace.c` answered "what does the game try to draw" without drawing
anything. This is the plan for actually drawing it — a native Vulkan
renderer, keeping the whole-program architecture this port has used since
`pc_memory.c`: replace the console's hardware, not the game's source.

Status: work in progress, started after `--trace-gx` had already reported a
real frame's shape (~114 GX entry points fan in to a much smaller working
set, dominated by display lists — see below). This document describes the
target architecture; `pc/GX_RENDERER_PROGRESS.md` (once created) tracks what
of it is actually built.

---

## Why not a recompiler-based renderer

The obvious shortcut is [Aurora](https://github.com/encounter/aurora) (MIT
licensed): a GX compatibility layer built on WebGPU/Dawn, already used by 50+
decompilation-based native ports, several shipped. It is a serious, proven
piece of engineering and this design borrows from its architecture
throughout. It could not be linked in directly, for one concrete reason:

**Dawn has no 32-bit build.** `cmake/AuroraDawnProvider.cmake` lists prebuilt
packages for `windows-{amd64,arm64}`, `linux-{x86_64,aarch64}`,
`darwin-{arm64,x86_64}` — no `i686`, and Dawn's own upstream (it is a large
slice of Chromium) does not target 32-bit x86 in any real capacity. This
port's game code is 32-bit for a reason that has nothing to do with the
renderer: `u32` reaching the decomp as `unsigned long` needs pointer-sized
fields inside `ASSERT_SIZE`-checked structs to stay 4 bytes, matching the
GameCube's own 32-bit pointers. (On Windows specifically, `unsigned long`
itself is 4 bytes in a 64-bit process too — confirmed empirically, since
Windows uses LLP64, not Linux's LP64 — but *pointers* are not, and the decomp
embeds real pointers inside those structs. That is what actually forces
32-bit here, not the `u32` typedef alone.)

A 64-bit build was ruled out rather than worked around. `jonrosner/melee-native`
(the other public prior art, also decomp-based, also using Aurora) solves
this the other way: it is 64-bit, with a substantial bridging layer
(`native/*.c` runs to ~4800 lines) that materializes archive data into
native-width structures instead of trusting the GameCube's raw in-struct
pointers. That is a legitimate design, but it is a different one — it
changes the game code's own memory model, where this port has held to
"replace the console's hardware, keep the game's source untouched" as far as
`extern/dolphin/` and `src/` are concerned. Redoing that bridging layer from
scratch was judged larger than building a thinner renderer directly, and it
would touch code this session already spent considerable effort getting to
boot correctly.

So: raw Vulkan, in the existing 32-bit process. The 32-bit Vulkan loader and
headers exist for MinGW (`mingw-w64-i686-vulkan-{headers,loader}`) and are
confirmed working — `vkCreateInstance` succeeds in a real 32-bit process on
this machine. Aurora's own `lib/dolphin/gx/*.cpp` (MIT licensed) is used
throughout as the reference for *how* GX state maps to a modern API — vertex
descriptor decoding, TEV-stage-to-shader translation, display list opcodes —
adapted into new, 32-bit-native C/C++ rather than linked as a dependency.
Where a chunk of logic is close enough to Aurora's own to call it a port
rather than an independent implementation, the source comment says so and
names the file, per MIT's attribution requirement.

## Why not intercept lower, or higher

Three layers were available to hook into:

1. **The write-gather FIFO itself** (`pc_ppc.c`'s stand-in, `GXFIFO_ADDR`).
   This is what real hardware reads, and what Dolphin's own video backend
   reads. Correct in principle, but it means writing a full binary
   FIFO-command decoder *and* not having any of the SDK's own bookkeeping
   (matrix stacks, TEV stage state, vertex descriptors) to lean on — GX.c's
   real implementation already maintains all of that correctly, in
   `gxData` (`GXInit.c`), entirely for free.

2. **The individual GX\* entry points** — `GXSetTevOrder`, `GXSetProjection`,
   `GXLoadTexObj`, and so on. This is what `pc_gx_trace.c` already hooks via
   `ld --wrap`, and it is where this renderer hooks too: the arguments
   arrive already typed and decoded (a `f32 mtx[4][4]`, not four bytes to
   reinterpret), and `__real_GX*` keeps running underneath, so the SDK's own
   state (which the game's *other* code paths read back, e.g.
   `GXGetTexObj*`) stays consistent whether or not this renderer understood
   a given call.

3. **`GXCallDisplayList`'s contents.** This is the one place layer 2 is not
   enough: a display list is a *pre-recorded* FIFO command stream, baked
   into the `.dat` files at asset-build time, and it arrives at the wrapper
   as an opaque `(void* list, u32 nbytes)` — not as a sequence of GX\* calls.
   `--trace-gx`'s own numbers said why this matters: display lists dominate
   how Melee draws, by a wide margin over immediate-mode `GXBegin`/`GXEnd`
   geometry. So the renderer needs a real decoder for exactly this one
   binary format (opcodes: `NOP`, `LOAD_CP_REG`, `LOAD_XF_REG`,
   `LOAD_INDX_{A,B,C,D}`, `LOAD_BP_REG`, `DRAW_*`), even though it does not
   need one for the write-gather FIFO in general. Aurora's
   `lib/dolphin/gx/GXFifo.cpp` is the reference for this decoder.

## Layout

| File | Role |
|---|---|
| `pc_vulkan.c` | Win32 window, Vulkan instance/device/swapchain, frame present. No GX knowledge. |
| `pc_gx_render.c` | The `__wrap_GX*` implementations: shadow state (matrices, TEV, vertex descriptors), translated into Vulkan calls via `pc_vulkan.c`. |
| `pc_gx_fifo.c` | The display-list decoder: walks a raw command stream, decoding vertices per the active `GXSetVtxDesc`/`GXSetVtxAttrFmt` state, and feeds the same draw path as immediate-mode geometry. |
| `pc_gx_shader.c` | TEV stage configuration → shader. Fixed, build-time-compiled SPIR-V for the first milestone (a handful of common configurations); runtime generation is later work (no 32-bit `shaderc`/`glslang` package exists yet, so this needs either a subprocess to a 64-bit compiler or a from-scratch tiny GLSL/SPIR-V emitter). |

`pc_gx_trace.c` is untouched and still selected by `--trace-gx`. The renderer
is a new `--renderer` link mode in `tools/phase0/linkexe.sh`, mutually
exclusive with tracing (both wrap the same symbols; only one wrapper set can
own them). Headless boot (neither flag) is unaffected either way.

## Frame pacing

`pc_os_thread.c` already explains this well for the boot-only case: the game
*asks* to wait (`OSSleepThread`/`OSYieldThread`) and the host advances one
frame inside that call, rather than the host driving frames on its own
clock. `pc_vulkan.c`'s present call and Win32 message pump go in `pc_vi_tick()`
alongside the existing `pc_ar_poll()` — the same "safe point" the comment
there already identifies, extended to cover presenting a frame and keeping
the window responsive, not just delivering deferred DMA completions.

## Milestone sequence

1. Window opens, Vulkan swapchain presents a clear color, every `pc_vi_tick`.
   No GX state read yet.
2. `GXSetCopyClear`, viewport/scissor, depth/blend/cull state tracked and
   applied.
3. Matrices (`GXSetProjection`, `GXLoadPosMtxImm`) and vertex formats;
   immediate-mode `GXBegin`/vertex-attribute calls draw untextured geometry.
4. Textures: `GXLoadTexObj` uploads GX texture data (its own format/swizzle)
   to Vulkan images; texture-coordinate generation wired in.
5. The FIFO decoder (`pc_gx_fifo.c`): `GXCallDisplayList` content becomes
   real geometry, through the same draw path as step 3.
6. TEV: stage configuration compiles to a fragment shader (fixed set first,
   dynamic generation later) instead of flat-shading everything.
7. Remainder: fog, alpha test, multiple render targets, whatever
   `--trace-gx`'s own tallies show matters and step 1-6 didn't cover.

Unsupported GX state fails loud — an `OSReport` naming the exact call and
argument, not a silent wrong frame. That is the same rule the byte-order
schemas in `pc_dvd.c` already follow, for the same reason: a renderer that
stops is diagnosable, one that runs on and is quietly wrong is not.
