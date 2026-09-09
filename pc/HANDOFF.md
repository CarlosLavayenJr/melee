# Handoff — native renderer milestone

Branch: `claude/sync-branch-update-pr033u` · last commit `a276d7fbd`

## Goal (unchanged)

Replace the trace-only GX layer with a real native Vulkan renderer, compiling
the recovered doldecomp C source directly. No DolRecomp, no static
recompilation, no CPU emulation, no Dolphin as a runtime, no generated
PowerPC-to-C game code. No game data committed — the user supplies their own
legally dumped GALE01 1.02 image at runtime (`game.iso` in the repo root, now
gitignored).

## Build and run

The toolchain is MSYS2's **32-bit** MinGW gcc. It is not on `PATH` by default,
and its location is not the usual `C:\msys64`:

```sh
export PATH="/c/Users/Owner/msys64/mingw32/bin:$PATH"
cd ~/melee
CC=/c/Users/Owner/msys64/mingw32/bin/gcc.exe \
  bash tools/phase0/linkexe.sh -m32 --renderer
```

`-m32` is mandatory. The script defaults to `-m64`, `mingw32`'s cc1 has no
64-bit codegen, and the port must stay 32-bit regardless: the decomp's
`ASSERT_SIZE`-checked structs contain pointers and have to match GameCube's
32-bit layout.

**Run from the repo root**, not from `build/phase2` — `pc_dvd.c` looks up
`game.iso` by relative path, and running elsewhere silently mounts nothing and
fails much later with a confusing `DVDReadAsync` range assert:

```sh
cd ~/melee && ./build/phase2/melee_host.exe
# or, for a backtrace:
cd ~/melee && gdb -batch -ex run -ex "bt full" build/phase2/melee_host.exe
```

To inspect a live spin rather than a crash, launch the exe in the background,
find its pid with `tasklist`, and `gdb -batch -ex "thread apply all bt" -p
<pid>`. `gdb`'s own `-ex interrupt` does not work here — it breaks into a
Windows debug-break thread, not the game thread.

## Where boot currently stops

```
assertion "pobj->u.jobj" failed in src/sysdolphin/baselib/pobj.c on line 411.
```

`HSD_PObjResolveRefs` takes the `POBJ_SKIN` branch and calls
`HSD_IDGetData((u32) pdesc->u.joint, NULL)`, which returns NULL, so the
assert fires. Established so far:

- The failing descriptor is at `0x80308790` with `u.joint == 0x80308780`,
  `flags == 128 (0x0080)`, `n_display == 256 (0x0100)`.
- Joints register themselves in the ID table from `JObjLoad`
  (`src/sysdolphin/baselib/jobj.c:662`, `HSD_IDInsertToTable(NULL, (u32) joint,
  jobj)`), keyed by the joint's own address. The lookup misses, so *that*
  joint was never passed through `JObjLoad`.
- A breakpoint conditioned on `id == 0x80308780` in `HSD_IDInsertToTable`
  never fires, confirming the miss rather than a hash/table bug.

**Two live hypotheses, not yet distinguished** — this is exactly where to pick
up:

1. **Byte order.** `n_display == 256` byte-swaps to 1, which is a far more
   plausible display-list count than 256, and `flags == 0x0080` swaps to
   `0x8000`. Both `pobj_type()` readings happen to yield `POBJ_SKIN`
   (`flags & 0x3000 == 0`) either way, so the type is not proof of correct
   order. If `HSD_PObjDesc` bodies are still big-endian, `u.joint` may be a
   relocated-but-misread pointer. Note `u.joint` (`0x80308780`) is only 16
   bytes below the descriptor itself — too close for a real `HSD_Joint` to fit
   without overlapping, which is *suggestive* of a bad pointer but not
   conclusive, since the joint could legitimately live elsewhere in the
   archive.
   The fix, if this is it, follows the established pattern in
   `pc/src/pc_hsd_swap.c`: add a `swap_pobj_desc()` with a self-consistency
   guard (as `swap_cobj_desc()` uses `projection_type`'s valid range) and wrap
   `HSD_PObjLoadDesc`. Add the `--wrap` flag next to the existing
   `HSD_CObjInit`/`HSD_CObjLoadDesc`/`HSD_PadGetRawQueueCount` ones in
   `tools/phase0/linkexe.sh` (~line 188).

2. **Load ordering.** `HSD_JObjLoadJoint` loads the whole joint tree (which
   registers every joint) *before* `HSD_JObjResolveRefsAll`. If this
   particular pobj is resolved from a different entry point, its joint may
   genuinely not be loaded yet. Get the backtrace at the assert to see which
   top-level call this resolve sits under.

The fastest way to separate the two: dump memory at `0x80308780` and check
whether it looks like a plausible `HSD_Joint` in host order, in big-endian
order, or neither. A gdb Python script over `default_table` (in
`src/sysdolphin/baselib/id.c`) enumerating the registered ids and comparing
their range against the target will also say quickly whether the target is
merely absent or wildly out of range. An attempt at this script is in
`/tmp/dumpids.py`; it was written but its output was never successfully
captured, so re-run it rather than trusting it.

## What was fixed this session (all in `a276d7fbd`)

Three distinct host-layer gaps, each blocking boot before any GX drawing call:

1. **OSAlarms were never delivered.** `pc_os_time.c` recorded them and relied
   on a decrementer interrupt that has no host equivalent. Added
   `pc_alarm_poll()`, called from `pc_vi_tick()` in `pc_os_thread.c` alongside
   the ARAM completions already delivered there. `OSSetPeriodicAlarm` now
   walks its first fire past `now` by whole periods, matching the real
   `InsertAlarm`.

2. **The pad queue had no writer.** `gm_801A4D34`'s boot loop spins on
   `HSD_PadGetRawQueueCount()` without ever sleeping or yielding, so no frame
   tick runs during it. The count's only writer, `HSD_PadRenewRawStatus`, is
   driven on hardware by the SI poll-complete interrupt — and
   `dolphin/pad/pad.c` is excluded from this build while `pc_pad.c`'s
   `PADRead` is synchronous, so neither driver existed. New file
   `pc/src/pc_pad_alarm.c` wraps the entry point and renews raw status there.
   Verified via gdb that `lb_80019628`'s alarm never arms on this path, so
   fixing the alarm queue alone was *not* sufficient — worth knowing before
   assuming (1) covers (2).

3. **`sqrtf` recursed into itself.** At `-O0` GCC lowers
   `__builtin_sqrt(f)` to a *call* to the libc function of that name rather
   than to an instruction; since `pc_libc.c` defines that function, it called
   itself until the stack died. Replaced with inline `fsqrt`; `floor` moved to
   portable C for the same reason (`frndint` follows the FPU rounding-control
   word, not round-toward-negative-infinity).

The general shape — a deferred-completion or interrupt-driven mechanism that
the host layer records but never delivers — has now accounted for the ARAM,
DVD, shared-guard, alarm, and pad-queue blockers. It is the first thing to
suspect on the next stall (as opposed to crash).

## Renderer status

Built and infrastructure-validated, but **not yet visually confirmed against
real game content**, because boot has never reached a drawing call.

- `pc/src/pc_vulkan.c` — Win32 window + instance/device/swapchain/render
  pass/framebuffers/command buffer/sync. Validated standalone (120 presented
  frames, no validation errors) and confirmed initializing during real boot
  (`pc_vulkan: window and swapchain ready` prints every run).
- `pc/src/pc_gx_render.c` — the `__wrap_GX*` entry points; opens a frame
  lazily, clears to the game's `GXSetCopyClear` color, presents on
  `GXCopyDisp`.
- `pc/src/pc_gx_fifo.c` — display-list decoder: CP/XF/BP register loads,
  vertex descriptors and attribute formats, direct and indexed position and
  color0, into a fixed position+vertex-color Vulkan pipeline. Unsupported
  cases log once each rather than guessing or crashing.
- `pc/GX_RENDERER.md` — the design note; read this before extending any of
  the above.

Only `__wrap_GXCopyDisp` (from `HSD_VIInit`'s initial framebuffer clear) has
ever fired. Breakpoints on `__wrap_GXBegin`, `__wrap_GXCallDisplayList` and
`pc_gx_fifo_exec` confirm none of them are reached before the pobj assert.
**The renderer is not the bottleneck; boot progress is.**

## Remaining plan

Steps 6–11 of the original brief, in order: finish GX state tracking
(viewport/scissor/depth/blend/cull — the pipeline currently hardcodes
`VK_CULL_MODE_NONE` and has no depth test), then texture upload and texcoord
generation, then TEV combiner stages as shaders, then fog/alpha test/render
targets.

Texture upload (step 8) is the one piece that is genuinely independent of the
boot blocker and can be worked in parallel — GameCube texture format decoding
(I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/CMPR) into Vulkan images behind
`__wrap_GXLoadTexObj`, which currently only passes through. If two agents work
at once, use a separate branch or `git worktree`, and do not run
`tools/phase0/linkexe.sh` concurrently — it writes to a shared `build/phase2`.

Shaders are compiled ahead of time and embedded as C arrays (`pc/shaders/`,
`pc/src/gx_basic_*_spv.h`) because no 32-bit `glslc`/`shaderc` exists. Whether
TEV becomes runtime-compiled shaders or a pre-generated permutation set is
still an open decision.

## Constraints to keep

- Never commit or distribute the ISO, DOL, extracted assets, fonts, textures,
  or any copyrighted game data. `*.iso`/`*.gcm` are gitignored as of this
  commit; do not add exceptions.
- Do not replace the game's rendering with mock visuals — interpret the actual
  GX commands and state the game issues.
- Unsupported GX features must fail visibly and diagnostically, never silently
  produce wrong output. `pc_gx_fifo.c`'s one-shot `static int warned` logging
  is the established pattern.
- Aurora (`encounter/aurora`, MIT) is an algorithmic and format reference
  only, credited in `pc_gx_fifo.c`'s header. It is never linked — it needs
  Dawn/WebGPU, which has no 32-bit build.
