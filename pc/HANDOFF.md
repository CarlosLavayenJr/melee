# Handoff — September 10, 2026 checkpoint

## Current blocker: menu draws rejected by raster channel, depth and blend

The menu is still almost entirely black. What changed this session is that the
reason is now measured rather than guessed, and the largest single cause is
fixed. **No new menu graphics appeared** -- see "honest visual status".

### Draw rejection is now counted, not just logged

`pc_gx_material_report()` and `pc_gx_texture_report()` tally accepted draws and
each skipped draw by reason; `pc/tests/menu_smoke.gdb` calls both at the end. One
log line per reason was enough to notice a gap but not to rank it: a reason
that kills every draw on screen looked exactly like one that kills a single
stray draw. Before this, 74% of menu draws were being discarded with no way to
tell which check mattered.

### Fixed: post-transform texture matrices (the largest cause)

Measured before: 20743 of 23713 skipped menu draws failed one lumped check,
"texture order or non-identity texgen". Splitting it three ways showed the
whole 20743 was specifically **non-identity texgen**, and recording the actual
texgen configurations showed 17782 of them were one shape:

    type=GX_TG_MTX2x4 src=GX_TG_TEX0 mtx=GX_IDENTITY norm=0 post=GX_PTTEXMTX0

That is sysdolphin's normal textured material. `tobj.c:492`
(`setupTextureCoordGen`) sets an identity 2x4 generator, and `tobj.c:488` loads
the texture's entire scale/rotate/translate matrix as the **post-transform**
matrix via `HSD_TexMapID2PTTexMtx`. Ignoring the post-transform was therefore
not a small approximation -- it discarded the whole UV transform of every
ordinary textured draw.

`pc_gx_material.c` now tracks post-transform matrices through a
`GXLoadTexMtxImm` wrapper and applies them to the vertex TEX0 in
`pc_gx_fifo.c`'s `emit_vertex`. Doing it on the CPU is exact here, not a
shortcut: the generator feeding the post-transform is the identity, so the
result is a pure function of the vertex texcoord and the matrix. Two stages
sampling through texgens with different post-transforms are rejected with a
new diagnostic, because only one uv per vertex reaches the shader.

After: non-identity texgen fell from **20743 to 671**.

### Honest visual status: the capture is unchanged

`build/menu-smoke.bmp` after menu frame 120 still shows only "Solo Smash!" on
black. Accepted draws barely moved, 7952 to 7876, because the 20743 draws that
stopped failing the texgen check now fail the checks *behind* it instead. The
texgen fix is a prerequisite -- without it those draws would be textured wrongly
even once the rest passes -- but on its own it added no visible graphics. Do
not describe it as a menu rendering improvement.

### Measured priority for the next session

Skips after the fix, in order (total 21179 against 7876 accepted):

    7600  raster channel other than COLOR0 or ZERO
    7033  depth comparison needs depth attachment
    4280  blend factors other than source-alpha/inverse-source-alpha
     867  texcoord index beyond enabled texgen count
     671  non-identity texgen
     602  more than four TEV stages
     126  logic/subtract blending
      95  CI4 and 95 CI8 texture loads

1. **Raster channel.** `GXTev.c:376`'s `c2r[] = {0,1,0,1,0,1,7,5,6}` maps
   GXChannelID to the BP field, so the rejected value is 1: channel 1
   (`GX_COLOR1`/`GX_ALPHA1`/`GX_COLOR1A1`). Needs `GX_VA_CLR1` decoded into a
   second vertex colour attribute -- `pc_gx_fifo.c` currently reads and
   discards it to keep the stream aligned -- plus the channel control from
   `GXSetChanCtrl` to know whether it comes from the vertex or from lighting,
   plus shader work.
2. **Depth attachment.** There is no depth image in the framebuffer at all.
3. **Blend factors.** Only source-alpha/inverse-source-alpha are mapped.

Items 1 and 3 need the fragment shader regenerated (`pc/shaders/regenerate.ps1`,
which needs a 64-bit glslc; no 32-bit shaderc exists). Budget for that.

`pc/tests/bmp_to_png.py` converts a capture for inline viewing. Captures stay
under gitignored `build/` and are never committed.

## Latest checkpoint: main menu runs for 240 frames; rendering incomplete

This section supersedes all checkpoints below. Opening-movie playback was
already working; this session fixes the normal Start-to-menu path after it.
No movie decoder or GX renderer runtime implementation changed in this session.

### Three diagnosed and fixed failures

1. Menu light descriptors were still big-endian. The ambient light arrived as
   flags `0x2400`, and both point lights as `0x0e00`, so `HSD_LObjLoadDesc`
   classified all three as ambient. The menu's point-light search then walked
   off the list at `mnmain.c:1789`. Added provenance-checked conversion for
   light flags, attenuation descriptors, and WObj positions. Also wrapped
   `HSD_WObjLoadDesc`: it calls the class load method directly, bypassing the
   existing WObjInit wrapper. Runtime now has flags `0x24/0x0e/0x0e`, valid
   point-light positions and attenuation, and passes this lookup.
2. Once light loading worked, `mn_8022BE34_OnEnter` overwrote its saved stack
   frame/return address. Its console-specific expression writes a Vec3 20
   bytes beyond the local Vec3; GCC returned to address zero, with camera Z
   (`0x424c0000`, 51.0) visible on the corrupted stack. The native-only branch
   now passes `&pos`; the console build retains the original expression.
3. Menu initialization then completed, but drawing asserted at `tobj.c:1246`
   within a few frames. Texture animation selected an unconverted image:
   width `0x4000`, height `0x3000`, format `0x02000000` (actually 64x48 IA4).
   Added texture-animation id/count conversion and image/TLUT table traversal
   through the existing shared-descriptor converters. Both TObjAddAnim and
   TObjAddAnimAll are wrapped because the same-TU call bypasses linker wrapping.
   Pointer tables and image/palette payload bytes stay untouched. Invalid
   archive table extents still abort loudly; provenance rules are not relaxed.

### Verification and honest visual status

- `pc/tests/run_renderer_tests.ps1 -Gpu`: all eight CPU executables and the
  Vulkan TEV GPU readback test pass. New regression cases cover light types,
  shared attenuation/positions, texture-animation chains and shared image/TLUT
  descriptors, repeat conversion, and native descriptors remaining unchanged.
- Native 32-bit incremental compilation of `pc_hsd_swap.c` and `mnmain.c`,
  followed by relinking with the new wrappers, passed. No clean full rebuild
  was performed in this session. Full build invocation remains below.
- New repeatable integration test, run from the repository root:
  `gdb -batch -x pc/tests/menu_smoke.gdb build/phase2/melee_host.exe`.
  It deliberately skips the intro and pulses Start through PADRead, with no
  scene override. It reached `mnMain_Scene_OnFrame`, `cur_menu=0`,
  `hovered_selection=0`, and stopped successfully after 240 menu frames.
  The debugger then intentionally terminates its test process.
- Inspected ignored `build/menu-smoke.bmp`, captured after menu frame 120:
  "Solo Smash!" is visible near the bottom, but the rest is black. This proves
  menu code runs and some text draws, NOT a complete or playable menu.
- Existing diagnostics still reject missing depth attachments, non-identity
  texgen/texture order, unsupported palette/depth/copy formats, more than four
  TEV stages, and unsupported blend modes/factors. Do not silence these or
  replace missing visuals with mocks. Logs/captures/binaries remain ignored.
- Full movie completion, interactive submenu navigation, and gameplay were
  not retested this session. Earlier frame-1000 movie evidence is historical.

### Next work

Prioritize the mostly missing menu rendering: inspect rejected menu draw state
and identify which unsupported GX feature blocks its actual panel/background
geometry. Use the repeatable menu test/capture to compare real rendering.
Also test normal keyboard navigation and submenu transitions before claiming
the menu is usable. Enter is Start (required at the title); J is A, K is B,
WASD is the analog stick. Announce deliberate movie skips and test stops before
launching visible tests so they are not mistaken for playback regressions.
The separate randomized attract-mode stage stops below have not been rerun
with these changes; texture-animation conversion may affect the Stadium stop,
but that has not been verified.

## Historical September 9 checkpoint: stage fixes; main menu light crash

This section supersedes the historical checkpoints below. Full opening-movie
playback was already working before this session; no movie decoder or renderer
runtime code was changed here. A fresh no-skip regression run reached movie frame
1000 in `lbMthp_8001F67C` / `__wrap_GXInitTexObj`, without an assert or crash,
then ended at an intentional debugger breakpoint. That is a bounded regression
check, not a new full-movie-completion claim.

### Changes in this checkpoint

- Fixed the native `StageCallbacks` flags aliases under `PC_GX_RENDERER`.
  Tables initialize a numerical u32 mask such as `0xc0000000`, but the old
  byte bitfields read the wrong bits on little-endian GCC. `flags_b0/b1` now
  map to bits 31/30, preserving the 20-byte structure and offset-16 flags.
  Ground now selects the archive light entry instead of the native fallback
  that caused the `ground.c:2526` assert. Console declarations are unchanged.
- Restored `map_ptcl` / `map_texg` conversion through the public-symbol hook.
  Preloaded stage archives call `psInitDataBankLoad` without Locate, so those
  banks need conversion before the public pointer reaches the caller. Existing
  provenance bookkeeping prevents double conversion on the normal Locate path.
- Diagnosed the conflicting-schema abort independently of the particle change.
  Kongo and Corneria requested `PC_HSD_MOBJ` (6) at an address previously marked
  `PC_HSD_STAGEENTRY` (22). `UnkStageDat::unk28` entries are shallow views of
  material descriptors: `unk4` aliases `HSD_MObjDesc::rendermode`. Stage setup
  converts and modifies that word before MObj loading. Both consumers now use
  one field-level `PC_HSD_MOBJ_FLAGS` claim, preserving the stage mutation and
  preventing a second swap. The full descriptor still gets its normal MObj
  claim; conflicting-schema rejection has NOT been weakened. Diagnostics now
  report the address and both schema kinds.
- Added flag-layout and both-material-load-order regression tests, public-symbol
  dispatch checks, and integrated stage/archive/particle/endian tests into
  `pc/tests/run_renderer_tests.ps1`.

### Priority blocker: actual main menu, not the attract-mode match

The title screen accepts **Start**, mapped to **Enter**, for the normal menu.
J maps to A: it can skip the opening, but at retail DbLevel 0 it does not enter
the normal menu from the title. Without Start, the title times out into the
attract-mode demo. Previous stage tests intentionally pressed A after movie
frame 10 to reach this demo quickly; this was the reason the user saw the
movie disappear, not evidence of a playback regression. Announce deliberate
skips and debugger stops before launching visible tests.

A targeted PADRead test sent Start pulses after title entry, through the game's
normal input path (no scene override). It reached `mnMain_Scene_OnEnter`, then:

```
SIGSEGV mn_8022C068(lobj=NULL, unused=0, div=0) mnmain.c:1789
  while (!(lobj->flags & LOBJ_POINT))
  mn_8022DDA8_inline -> mnMain_Scene_OnEnter (mnmain.c:2939)
  gm_801A4014 -> runGameMode(mode_kind=1) -> main
```

The point-light search reaches NULL. Next inspect `MenMain_lights`,
`lb_80011AC4`, and `HSD_LObjLoadDesc`: distinguish an empty list from incorrect
descriptor flags or a missing expected point light. No fix for this crash is
implemented or verified yet; do not hide it with a null guard. Menu rendering
also remains unverified. Runtime diagnostics still report unsupported depth,
texgen/texture formats, blending, and more-than-four-TEV-stage configurations.

### Separate, later attract-mode stage stops

With all three stage fixes above, Castle reached `grCastle_801CD658` and stopped
in `lb_8000FD48(jobj=NULL, max_count=100663296)` at `lb_00F9.c:171`, called by
`grLib_801C9B20`. The count is `0x06000000`, suggesting an unconverted big-endian
6 in `arg1->count`; this hypothesis is not yet fixed or proven.

An earlier flags-only test reached Stadium initialization and stopped in
`HSD_TlutLoadDesc(tlutdesc=0x09000000)` from `HSD_TObjAddAnim`, via
`grAnime_801C8138`. Audit the texture-animation descriptor/table/count conversion
if this recurs. This Stadium run preceded the shared-material-field fix.

Attract-mode stages are randomized. These observations show progress past the
previous stops, not that every stage loads or gameplay works.

### Verification and build

`powershell -File pc/tests/run_renderer_tests.ps1 -Gpu` passes the eight CPU
test executables plus the Vulkan TEV GPU readback test. Runtime checks above
used an incremental rebuild of the changed host objects and `ground.c`, then
relinked the native executable; no clean full rebuild was performed this session.

Full build from MSYS, from the repository root:

```sh
export PATH="/c/Users/Owner/msys64/mingw32/bin:$PATH"
CC=/c/Users/Owner/msys64/mingw32/bin/gcc.exe bash tools/phase0/linkexe.sh -m32 --renderer
```

Run `build/phase2/melee_host.exe` from the repository root. The user's ignored
`game.iso` must be there. Keep native recovered C, 32-bit pointers, real GX
interpretation, and strict unsupported-feature diagnostics. Do not commit game
assets, extracted data, binaries, or framebuffer captures. Do not run concurrent
builds against `build/phase2`.

## Historical checkpoint: two known stops in VS stage setup, plus one open question

Runs are NOT deterministic -- the attract-mode demo picks a stage at random,
and different stages take different paths -- so a single clean run proves
nothing. Sample several. Across the last set, three outcomes appeared:

**1. `ground.c:2526`, the most common.** `Ground_801C43C4` cannot find a
natively declared `HSD_LightAnim` in the stage's shadow-entry array:

```
HSD_ASSERT(3652, 0)  Ground_801C43C4 (arg0=Ground_803E069C) ground.c:2526
  Ground_801C466C () ground.c:2726 -> Ground_801C0800 ground.c:508
  Stage_8022524C () stage.c:520 -> fn_8016E730 gm_16AE.c:2007
```

Read this one before writing a schema for it, because it may not be one.
`arg0` is `Ground_803E069C`, a native `HSD_LightAnim[]` in ground.c reached
through the native fallback light list `Ground_803E06C8`. `Ground_801C466C`
picks that fallback only when no entry in `stage_datas[grkind]->callbacks` has
`flags_b0 == 1`, which is native game data, not archive data. So the question
is whether that selection is correct, and whether `UnkStageDat::unk24` -- the
shadow-entry count the search runs over, which only became a real number with
the map_head schema -- is right for this stage. A native fallback pointer will
never be in an archive-sourced array, so if both are correct then the assert
is reachable on console too and something further upstream is wrong.

**2. `particle.c:207`, "psInitDataBanks: unknown version".** A real gap, with a
known fix that is NOT in this commit. `grdatfiles.c` has two branches: the
normal one calls `psInitDataBank`, which calls `psInitDataBankLocate` (wrapped,
so the banks get converted) and then `psInitDataBankLoad`. The preloaded-
archive branch calls `psInitDataBankLoad` DIRECTLY, so the banks were never
converted and Load panics on a byte-reversed version.

Routing `map_ptcl` and `map_texg` through the `HSD_ArchiveGetPublicAddress`
symbol hook fixes it -- two `else if` arms calling
`pc_hsd_particle_banks_to_native(p, NULL, NULL)` and `(NULL, p, NULL)` -- and
that was tried and did remove this panic. It was reverted before committing
because it introduced item 3 below and there was no time to tell whether the
two are related. Redo it, then diagnose 3 with it in place.

**3. `pc_hsd_endian: descriptor outside archive or conflicting schema`.**
Appeared only in runs with the item-2 change applied, so it is probably caused
by it, but that is not established -- the run-to-run variation is large enough
that it could have been present already. This abort comes from `pc_hsd_claim`
finding a first word already claimed under a DIFFERENT schema kind, which
means two schemas disagree about what lives at one address. Get a backtrace on
the abort and print the address and both kinds before changing anything; do
not widen the claim rules to make it go away.

## What is verified this session

Archive conversion moved from DVD-read time to consumption time
(`pc/src/pc_hsd_archive.c`). This is the fix for the title staging blocker
recorded in the previous handoff, and it removes three separate defects that
all came from converting too early:

- A large archive arrives in several reads. Only the first saw `rel == 0`, and
  since its length was far below the archive's stated size the body-size check
  tripped and the relocation, public and extern tables were never converted at
  all. The recurring `pc_dvd: truncated HSD archive body` line was exactly
  this. It no longer appears in a full run.
- `devcom.c` does not read into final memory: it stages DVD chunks through two
  16 KB relay buffers and ARAM before copying them where the archive will live.
  Conversion at read time therefore ran against a relay buffer, and
  `pc_hsd_archive_body` correctly refused an address outside the game's RAM
  window. That refusal was the abort entering `gm_Scene_Title_OnEnter`. The
  buffer address was never the bug; the timing was.
- Provenance recorded against a staging buffer does not follow the bytes to
  their destination, so descriptor schemas would not have applied even if the
  address had been accepted.

An archive is whole, contiguous and final in exactly one place: when a consumer
parses it. `HSD_ArchiveParse` and `lbArchiveRelocate` are those places, and
every `.dat` consumer reaches one of them — `lbArchive_InitializeDAT`,
`efAsync_OnLoad` and `grDatFiles_801C5FC0` all funnel into the first. Both are
now wrapped.

Doing it exactly once needs no extra bookkeeping, because both callees already
compare the archive's own `file_size` field against the caller's size as a
byte-order check. An archive whose stated size already equals the caller's is
in host order and is left alone, so re-parsing a converted buffer — or
relocating a copy of one, which `ftdata.c` does to fighter animation data — is
a no-op rather than a second swap. That same property is why the DVD-time path
had to be removed rather than kept as a fast path for small files: a partially
converted header passes this test while its tables are still big-endian.

`__DVDLongFileNameFlag` is now published by `pc_os.c`'s `OSInit`, as the real
`OSInit` does unconditionally (OS.c:168). `dvdfs.c` enforces 8.3 filenames when
it is zero, and Melee ships names that do not fit — `PlKbNrCpDk.dat` among
them — so the SDK's own diagnostic panic was the stopping point the first time
Kirby's copy-ability data was opened. Same class as the `__OSBusClock` fix: a
console-init global nothing on the host was writing. Worth grepping for others.

The movie's frame-size guard moved from `fn_8001E910` to the two
`HSD_DevComRequest` calls that actually use the value. It read the next frame's
size out of the front of the buffer just filled, and at the end of the movie —
or once `lbMthp_8001F800` clears `unk_70` — there is no next frame, so the word
is whatever the last frame left behind. The game never uses it in that case.
Observed firing at `unk_70 = 0`, i.e. during shutdown, on a value that was
about to be discarded. Validating at the point of use keeps the real
protection: an oversized value there would overrun a frame buffer.

## Tests

`pc/tests/hsd_archive_test.c` — conversion happens exactly once and only when
it should: header, tables and only relocation-named body words converted;
`version[4]` and symbol strings left as bytes; a second call a no-op; a size
matching neither byte order leaving the archive completely untouched; a short
(partial) read refused rather than half-converted; and provenance recorded on
the body, at the parsed address, and forgotten on re-read.

```
gcc -m32 -w -O0 -g -std=gnu17 -include tools/phase0/compat.h \
    -I pc/src -I src -I extern/dolphin/include \
    pc/tests/hsd_archive_test.c pc/src/pc_hsd_archive.c \
    pc/src/pc_hsd_endian.c -Wl,--large-address-aware -o t.exe
```

Note for every test here: `-I extern/dolphin/include/libc` must be left out.
Its `assert.h` shadows the host's and defines no `assert`.

## Still not done — do not claim any of this

- No VS match, gameplay, title visuals, audio or saves. Reaching
  `gm_Scene_Vs_OnEnter` is a scene entry point, not a playable match.
- Audio remains entirely unimplemented and is independent of this track:
  `pc_ai` reports playback disabled and `pc_dsp` completes tasks immediately.
  HPS stream metadata converts correctly, but nothing decodes or outputs.
- Renderer still reports these during the opening scene, each skipping a draw:
  depth comparison without a depth attachment, texture order or non-identity
  texgen, more than four TEV stages, logic/subtract blending, and
  palette/depth/copy texture formats.
- The run is timing-dependent. Two runs from the same binary stopped in
  different places, so a single clean run is not evidence a path is fixed.

---

# Earlier handoffs — history, not current status

## Latest verification: movie frame 1000; next blocker is title asset staging

The longer run reached frame=1000, counter=2001, buffered=31, alarm.period=675000
without a crash/assert. This is ~33 seconds of source movie content, not full
intro completion. PADStatus was held neutral by GDB after MoviePlayer.power
became nonzero to prevent test-time button presses from skipping the movie.
The normal executable retains its keyboard input; there is no scene bypass.

Clean renderer rebuild linked successfully: 1141 objects, 34 existing placeholder
symbols, 92MB EXE. Renderer CPU/GPU, texture CPU/GPU, THP kernel and HSD descriptor
tests all pass. The unresolved placeholders are still a port-completeness limit,
not game-code recompilation or evidence of a finished executable.

Skipping normally with confirm input enters gm_Scene_Title_OnEnter and aborts:
`pc_hsd_archive_body(data=HSD_DevCom_804C6330_bufs+32, size=6960)`.
Call chain: gm_PreloadTitleDemo -> lbDvd_80018254 -> DVD completion ->
DVDLowRead -> swap_contents("PlKbAJ.dat", rel=0, length=16384) ->
swap_hsd_archive -> pc_hsd_archive_body. The two 16KB relay buffers are native
static storage outside 0x80000000..0x81800000, not malformed archive pointers.

Do NOT merely expand the provenance bounds or drop the assertion. devcom.c
stages DVD chunks through these buffers into ARAM, then copies them to final
memory. The current DVD-read-time archive conversion assumes a whole archive
in its final RAM location: it both converts too early and loses provenance
across transfers; larger split archives also leave tables unswapped. Audit
moving archive conversion to HSD_ArchiveParse / lbArchiveRelocate consumption
or explicitly tracking byte-order/provenance across staged transfers. Preserve
header-only consumers and ensure relocation words are converted exactly once.
This title-path fix is not implemented. Full opening completion, title visuals,
gameplay, sound and saves are still unverified/unimplemented as noted below.

---

## Latest renderer milestone: real opening-movie frame in Vulkan

The game-issued movie quad now renders its three decoded I8 planes through the
actual four-stage TEV program from sobjlib.c. An application framebuffer capture
at movie frame 90 visibly shows the blue sky/trophy scene. The user also saw the
intro in the window. The short appearance ended because the GDB capture test
finished, not because that test crashed. No external video player, fake frame,
scene bypass or preconverted game asset is involved.

Renderer changes: up to four ADD/SUB TEV stages, PREV/REG0/1/2 inputs/outputs,
per-stage konst selectors with separate konst storage, eight sampler bindings,
208-byte std140 material snapshots per draw, and front/back/all culling pipeline
variants. Texgens may select any generated coordinate that is identity TEX0;
arbitrary texture matrices and independent TEX1+ vertex coordinates still fail.
RGB565/RGBA4/RGBA6 vertex colors now decode rather than substituting white.

`pc/tests/run_renderer_tests.ps1 -Gpu` runs clocks/alarms, material/BP state,
immediate/packed-color decoding, and actual Vulkan readback of four synthetic
YUV triples plus same-frame material/texture lifetime and sprite culling tests.
GPU values agree with the TEV arithmetic reference within 1 byte for those
samples. Arithmetic remains floating-point, not bit-exact GX fixed-point;
rounding, unclamped register feedback/wrapping and broader materials need audit.
No Vulkan validation-layer coverage is claimed (layer unavailable here).

Next: longer uninterrupted opening playback (frame-90 capture is the current
verified endpoint), then remaining depth attachments/state and nonidentity
texgen support. An earlier run was interrupted around an archive-provenance
abort after user input; if this recurs, capture the full caller and archive
address before changing provenance bounds. No title-screen/gameplay/audio/saves
claim. All movie dumps and framebuffer captures remain ignored under build/.

---

## Latest: clock initialization fixed; content-bearing movie frames verified

`pc_bootinfo_init` now publishes the IPL bus/core clock words (162/486 MHz).
`pc_clock.h` shares those values with the host timebase. The nanosecond-to-tick
conversion also no longer overflows after ~455 seconds, and its epoch handles
a monotonic source initially returning zero.

Real runs reached decoded frames 10 and 90. GDB at the movie's GXInitTexObj:
alarm.period=675000, handler=fn_8001F2A4, frame=90, counter=180, buffered=31.
The user provided confirm input during these runs; no scene-state override.
Frame 90's dumped Y/U/V planes were untiled and converted for diagnostic viewing:
recognizable blue sky/trophy imagery, not a flat frame. This confirms real
content decoding, NOT bit-exact agreement with console output. Dumps/previews
stay in ignored build/ and are not committed. Still no movie pixels on-screen:
the next task is interpreting the four-stage TEV program in sobjlib.c.

`pc/tests/os_time_test.c` passes startup-word/SDK conversion tests, a simulated
24-hour timebase, repeated 60 Hz periodic callbacks, catch-up, cancellation and
one-shot delivery. Link pc_bootinfo.c and pc_os_time.c with the usual 32-bit host
test flags (no SDK libc include), compat.h, and --large-address-aware.
Clock consumers audited include lb_0195.c frame/pad pacing, lbtime.c, gmopening.c
performance reporting, and SDK device timeouts. Calendar dates are still the
existing approximate host implementation; not fixed or claimed here.

---

## Previous handoff (clock blocker below is now resolved)

## Current blocker: the movie decodes but never advances past frame 0

`__OSBusClock` at 0x800000F8 is zero. Nothing in the host layer ever writes it
(the console's bootrom does), so `OS_TIMER_CLOCK` — defined as
`__OSBusClock / 4` — is zero too, and every `OSSecondsToTicks(x)` returns 0.
`lbMthp_8001F410` arms the movie's frame-pacing alarm with
`OSSetPeriodicAlarm(&alarm, OSSecondsToTicks(1.0f/60), OSSecondsToTicks(1.0f/60),
fn_8001F2A4)`, so it arrives with `period == 0`. `pc_alarm_poll` treats a zero
period as one-shot: it fires the handler once and calls `OSCancelAlarm`.

Verified in gdb at `lbmthp.c:637`: `__OSBusClock=0`, `alarm.period=0`,
`alarm.fire=0`, `alarm.handler=(nil)`, `MoviePlayer.unk_80=1` (so the handler
did run exactly once), `unk_78=0` (the frame index never advanced),
`unk_108=31` — thirty-one frames are buffered and ready, so this is not a
streaming or DVD problem. Dumping the decoded planes at the 91st call of
`lbMthp_8001F67C` gives a plane byte-identical to the first: the decoder is
re-decoding frame 0 forever.

Fix `__OSBusClock` (and `__OSCoreClock` beside it at 0xF8/0xFC) at host init —
GameCube values are a 162 MHz bus and a 486 MHz core, giving the 40.5 MHz timer
the whole OSTime layer already assumes. Audit what else silently divides by
`OS_TIMER_CLOCK`: every `OSTicksToSeconds`/`OSMillisecondsToTicks` user has been
running against zero, so other timing that looked "close enough" may also be
wrong rather than merely coarse. Do NOT special-case a zero period inside
`pc_alarm_poll` — that hides the real defect and leaves the rest of the timing
layer broken.

**A black screen is currently the correct output and not evidence of anything
working.** Frame 0 of MvOpen.mth genuinely decodes to black, and nothing draws
YUV to the framebuffer yet (see "still not done" below).

## What is verified this session

Native THP decoding, replacing kernels that existed only as MWCC assembly:

- `pc/src/pc_thp_kernels.h` holds native C for the Huffman/receive entropy
  decoder and the AAN inverse DCT. `extern/dolphin/src/dolphin/thp/THPDec.c`
  calls them from `#else` branches of the same `#ifdef __MWERKS__` guards that
  select the assembly, so the MWCC paths are byte-for-byte unchanged.
- The IDCT is libjpeg-6b's `jidctflt.c`. That is not a design choice — reading
  the paired-single kernel out shows identical AAN constants and an identical
  `__THPAANScaleFactor` table. libjpeg's 1/8 is absent from the quantisation
  table because the quantised store recovers it (GQR6 = 0x3D04, type u8, scale
  −3), which is also where the level shift lands: the column pass adds 1024 and
  1024/8 is JPEG's +128.
- The IDCT writes GX_TF_I8 tile order directly (8x4 tiles; rows 8 bytes apart;
  a 32-byte group every 8 columns). That is why `lbMthp_8001F67C` can hand the
  plane straight to `GXInitTexObj` as `GX_TF_I8` — nothing re-tiles it.

Three further defects were in the way, each of which alone produces a
plausible-looking failure:

- **Bitstream byte order.** The decoder caches a 32-bit *big-endian* word in
  `info->currByte` and consumes it MSB first; every console refill is a `lwz`,
  and `__THPPrepBitStream`'s plain `info->currByte = *ptr` compiled to a
  host-order load. A correct Huffman decoder alone would still have decoded
  every code from reversed bytes. Audited the rest of the THP path: markers,
  quantisation tables and Huffman bit counts are all read a byte at a time and
  were already correct either way, so this word cache was the only exposure.
- **Adjacent globals.** `THPInit` writes `__THPLC` and `__THPLCWork672` through
  one `struct THPInitWork`, which works only because the console linker placed
  them next to each other — `work672` lands past the end of `__THPLC`. A native
  linker orders independent globals freely, so that store went somewhere else
  and `__THPLCWork672`, the base every decoded MCU row is written through,
  stayed null. Now declared as one object, the same fix `card_host_storage.h`
  makes for the memory-card context. The locked cache itself became ordinary
  memory rather than the hardcoded 0xE0000000.
- **`LCStoreData` was a no-op**, on the grounds that x86 caches are coherent.
  It is a DMA, not cache maintenance: `__THPDecompressiMCURow*` decodes a whole
  MCU row into locked-cache scratch and stores it into the frame plane, so the
  stub discarded every decoded pixel. It and the other LC transfers now copy.

Against the real `MvOpen.mth`, with no controlled input: the decoder runs to
completion, `__THPBitStreamFailed` never fires, and execution reaches
`GXInitTexObj`. Frame 0 decodes to Y=16, U=V=128 — black — with a Y=0 border
whose edges fall on rows 19 and 462. Those rows are **not** tile-aligned, so
those pixels came from decoded coefficients rather than unwritten memory. That
is the strongest evidence available that the decode is real, and it is not the
same thing as having seen a correct picture: no frame with actual content has
been decoded yet, because of the pacing blocker above.

Material and texture descriptor byte order (`pc_hsd_swap.c`):

- `DObjLoad` panicked at `dobj.c:312` on `rendermode & 0x60000000` hitting
  `default` with 0x31001060, which byte-swaps to a valid 0x60100031. New
  schemas convert `HSD_MObjDesc`, `HSD_Material`, `HSD_TObjDesc`,
  `HSD_ImageDesc`, `HSD_TlutDesc`, `HSD_TexLODDesc` and `HSD_TObjTevDesc`,
  wrapped at `HSD_MObjLoadDesc` and `HSD_TObjLoadDesc` (both, because texture
  animation reaches the latter without a material; `pc_hsd_claim` makes
  whichever runs second a no-op).
- Byte fields are deliberately untouched: `HSD_PEDesc`, the repeat flags, every
  `GXColor`, and all of `HSD_TObjTevDesc` except `active` have no byte order to
  get wrong. Pointer fields were already converted by `pc_dvd.c`'s relocation
  pass.
- The panic is gone and the opening scene now runs continuously for at least 90
  seconds without halting.

## Tests

- `pc/tests/thp_kernel_test.c` — standalone, 32-bit, known answers throughout:
  big-endian word loads, MSB-first receive across word boundaries, sign
  extension against JPEG Table F.2, codes of 1..7 bits through both the quick
  table and the canonical fallback, a code starting at every bit offset from 26
  to 32 so each word-boundary branch of the console decoder is covered, an
  unmatchable code reported rather than guessed, zig-zag placement and DC
  prediction, GX I8 tile addressing, and the IDCT against a double-precision
  textbook transform (exact — worst deviation 0 counts over 64 random blocks).
  It found a real bug: the canonical search terminated on the `maxCode[17]`
  sentinel and then indexed `Vij` with a 17-bit code.

  ```
  gcc -m32 -std=c11 -Wall -Wextra -Werror -O2 -I pc/src \
      -include tools/phase0/compat.h pc/tests/thp_kernel_test.c -o t.exe
  ```

- `pc/tests/hsd_endian_test.c` — extended with the material graph, including
  the real 0x31001060 rendermode, and asserting that byte and pointer fields
  come through untouched. Note the include list: `-I extern/dolphin/include/libc`
  must be **left out**, because its `assert.h` shadows the host's and defines no
  `assert`.

  ```
  gcc -m32 -w -O0 -g -fgnu89-inline -std=gnu17 \
      -Wno-error=implicit-function-declaration \
      -Wno-error=incompatible-pointer-types -include tools/phase0/compat.h \
      -I src -I extern/dolphin/include -I extern/dolphin/src -I pc/src \
      -DVERSION_GALE01 -DBUILD_VERSION=0 -DPC_GX_RENDERER \
      pc/tests/hsd_endian_test.c pc/src/pc_hsd_endian.c pc/src/pc_hsd_swap.c \
      -Wl,--large-address-aware -o t.exe
  ```

- `pc/tests/thp_plane_dump.py` — untiles a plane dumped from a running process
  and reports its histogram, row means and optionally a PPM. Diagnostic only.
  Dump with gdb into `build/` (gitignored); never commit a decoded frame.

## Still not done — do not claim any of this

- **No movie pixels reach the screen.** `lbMthp_8001F67C` binds three
  `GX_TF_I8` texobjs — Y at full resolution on TEXMAP0, U and V at half on
  TEXMAP1/TEXMAP2 — and the renderer has no YUV→RGB path across three texmaps.
  This is the third task from the original brief and is still entirely ahead.
- No frame with real picture content has been decoded, so the decoder is
  verified against synthetic known answers and against frame 0's structure,
  **not** against a photograph. Re-dump a mid-movie frame once pacing works and
  check it is not flat before trusting it.
- The renderer still reports these unsupported cases during the opening scene,
  each of which skips a draw: face culling, RGB565/RGBA4/RGBA6 vertex colour
  formats, TEV PREV/REG1/REG2/konst inputs, multiple TEV stages, logic/subtract
  blending, and palette/depth/copy texture formats.
- `pc_dvd: truncated HSD archive body` still appears three times during the
  opening scene. Unaudited.
- No audio, no saves, no gameplay, no title screen.

## Recent additions

The window title now carries the presented frame rate, counted from real
`vkQueuePresentKHR` calls rather than the game's internal frame counter.
`tools/phase0/linkcheck.sh` and `survey.sh` gained `-I pc/src` so they still
compile `THPDec.c`.

---

# Earlier handoffs — history, not current status

## Latest: HPS stream metadata fixed; opening movie header is next

Audio metadata now converts at the three stream-header consumption callbacks.
Observed raw HPS header magic was " HALPST", rate=32000, channels=2 (the earlier
one-channel hypothesis below was not correct). Native code had truncated
0x02000000 to an 8-bit voice_count=0. Only header u32 fields and AX 16-bit
records are swapped; ADPCM payloads remain raw. `hps_endian_test.c` passes.
The controlled input run now gets beyond AXSetVoiceMix into MvOpen.mth loading.

Next crash: lbmthp.c fn_8001ECF4 receives NULL allocation because movie header
width/height/buffer sizes are still big-endian. Convert the MTH metadata and
frame-size words with validated bounds. THPDec.c also contains MWCC-only
paired-single assembly with missing native paths: audit before treating decoded
movie output as valid. Do not silently substitute blank/video placeholders.

---

## Beyond the text prompt: card-context layout fixed

Controlled PADStatus A-button pulses through the real menu code now reach
`gm_Scene_Opening_OnEnter`. This is not physical-keyboard verification or a
game-state bypass. Keyboard mappings: J=A, K=B, Enter=Start, WASD=stick,
arrows=D-pad, L=X, I=Y, U=Z, Space=L-trigger. Run from repo root:
`./build/phase2/melee_host.exe` (PowerShell uses `.\build\phase2\melee_host.exe`).

The next crash after confirming no-card was card-context storage, not card I/O:
the code assumes three independently declared globals occupy one contiguous
0x1510-byte context. The native linker put the command array BEFORE its header,
so base+0x10 read unrelated `hsd_804D799C=2` as a command with a NULL state.
`card_host_storage.h` now defines a single aggregate with asserted offsets
0x10 and 0x1210, retaining the original console definitions outside renderer
builds. `card_storage_test.c` exercises all 128 command positions.

Next observed crash is opening-scene streaming audio: HSD_SynthPStreamHeaderCallback
reads raw big-endian HPS metadata as native (`voice_count` becomes zero from
0x01000000; sample ratio becomes 256). A later AXSetVoiceMix receives NULL voice[1].
Fix the stream metadata schema, not a NULL-voice guard or fake audio output.
Some `.dat` header-only reads also log "truncated HSD archive body"; audit partial
archive reads separately. The initial text remains visually verified, but no
opening scene or gameplay is verified yet.

---

## Latest VERIFIED checkpoint: readable native menu text

The SIS endian blocker below is fixed. A Vulkan framebuffer readback was
visually inspected and displays **"There is no Memory Card in Slot A."** in
the actual game's font, with transparent glyph backgrounds and readable
two-line layout. This is game-issued geometry and runtime-loaded font data,
not mock visuals. The fourth frame-end breakpoint reached the normal memory-card
menu loop without the invalid-source exit. No gameplay or broader menus verified.

`sis_byteorder.h` provides renderer-host big-endian reads while retaining the
original console dereferences. All 18 SIS 16-bit loads in hsd_3A76.c now use
these readers: glyph rendering AND measurement, scales, signed positions and
spacing, delays and saved-state restoration. Saved pointers are read in the
same big-endian byte order that the existing stack writer emits. Archive-
relocated jump pointers remain native and are deliberately not swapped again.
Producer audit: hsd_3A64.c writes coordinates/scales explicitly high byte first;
hsd_3A76.c does the same for saved values/pointers. Texture bytes stay raw.

Regression: `pc/tests/sis_byteorder_test.c` covers odd alignment, glyph 0x201d,
negative spacing, fixed-point scale and saved-pointer round trips. Compile with
32-bit GCC, `-DPC_GX_RENDERER`, compat.h and normal host/game include paths.

Next work: test continuation/input through the actual memory-card prompt and
audit newly encountered material/descriptor state. Do not assume correct
gameplay from a correctly rendered text message. Texture/TEV subset limitations
documented below still apply. No extracted assets or frame captures committed.

---

## Latest WIP: texture/shader wiring; current blocker supersedes status below

New code loads the native font atlas from the user's GALE01 revision-2 disc
DATA section only (no executable-code translation or extraction). It preserves
bounded native GXTexObj sources, decoded UVs, immutable per-draw descriptors,
and BP register state. The new shader implements a validated single-stage TEV
subset, alpha comparison and source-alpha blending/write masks. Regenerate
SPIR-V and embedded headers with `pc/shaders/regenerate.ps1`. No assets committed.

**Readable text is NOT verified.** The latest build now fails explicitly at
`pc_gx_render: unknown native texture source`, called from
`HSD_SisLib_803A84BC`, hsd_3A76.c:896. GDB: glyph_idx=0x1d20,
tex_offset=0xfffffd20, source=font_base+0x1fa4000. At line 830,
`glyph_idx = *(u16*)sis_cursor` reads the SIS byte stream in native little-endian;
the expected glyph ID is 0x201d. Do NOT widen source bounds to accept this pointer.

Next: audit SIS reader AND producer byte order, including text measurement/layout
and inline signed/unsigned 16-bit parameters; fix consistently, then rebuild and
capture the actual menu. Avoid fixing only one glyph read while leaving spacing
and text-control parameters backward. The earlier white boxes remain the last
visually confirmed checkpoint, not evidence that this shader already works.

Clean renderer build linked: 1141 objects, 34 placeholders. Startup confirmed
`font atlas loaded from user disc data`, then the source-bounds failure above.
Texture CPU/GPU readback, direct-UV decoder, and material-state tests pass.
The material-state guards were tightened/unit-tested after the full build.
Vulkan validation layer unavailable. New `gx_material_test.c` tests BP masks,
konst/TEV register separation, menu state and unsupported-state rejection.

This is NOT complete/bit-exact GX: extra TEV stages, PREV/REG1/REG2/konst inputs,
compare ops, nonidentity texgen/swap, fog and most blend/depth states remain
unsupported and diagnosed. Preserve this checkpoint before further changes.

---

Checkout: `C:\Users\Owner\melee`, branch `claude/sync-branch-update-pr033u`.
The texture branch `codex/gx-texture-upload` was merged in `08c5ba7b7`.
The older handoff below is retained as history, **not current blocker/status**.

## Current evidence

- Native Windows EXE initializes Vulkan and reaches the memory-card menu loop.
  The former unresolved-joint assertion and subsequent GX finish wait are fixed.
- Real menu GX immediate geometry now reaches Vulkan. Application framebuffer
  readback was visually inspected: white glyph-sized rectangles on black. These
  are game-issued geometry, **not legible text, a correct menu, or gameplay**.
- At the fourth frame-end breakpoint, three frames had been submitted and the
  current frame contained 162 expanded vertices. Stack:
  `HSD_VICopyXFBAsync -> gm_801A4D34 -> runGameMode`.
- Texture CPU decode and Vulkan upload/readback tests pass for I4/I8/IA4/IA8,
  RGB565/RGB5A3/RGBA8/CMPR, four mip levels, cache and rebinding lifetime.
  The Vulkan validation layer was unavailable; do not claim a validation pass.
- The basic shader still uses position/color only. Texture upload is implemented
  but texture sampling/TEV/depth state are not. First draw logs this limitation.
- No gameplay, audio, or correct textured rendering has been verified.

## Changes since the previous handoff

`6db4637ea`: tiled texture decoders, immutable Vulkan image uploads/cache/samplers,
tests, `pc/TEXTURES.md`, MIT reference license under `pc/licenses`.

`7b5c87fc5`: archive-provenance/once-per-schema endian conversion for PObj chains,
vertex metadata, envelope weights, joint trees and matrices. The missing joint
was a misread union: observed flags `0x01a0` swap to `0xa001` (envelope), with an
envelope pointer array at the supposed joint address. Relocated pointers must
NOT be swapped again. The next stall was GXWaitDrawDone; native completion now
waits for the real Vulkan queue and delivers SDK interrupt 19, preserving the
SDK FinishQueue/callback mechanism.

Latest checkpoint:

- Renderer-only hooks capture release GX inline vertex MMIO stores and feed
  their actual values into the shared display-list decoder.
- Draws use separate vertex-buffer ranges until the frame fence, so later
  uploads cannot overwrite earlier queued geometry.
- Corrected color enums and GX-to-Vulkan clip-space conversion; converted
  archive WObj camera positions once.
- Restored existing MSL float/math tables to the Windows build and called the
  retained trig initializer. Excluding them had created zero-filled stubs,
  collapsing geometry and producing NaN projection values. Projection is now
  finite, and real first-quad positions span approximately (-13.921, 9, -64)
  to (-11.393, 6.472, -64).
- Optional application-only GPU readback:
  `PC_CAPTURE_FRAME=build/phase2/first-frame.bmp ./build/phase2/melee_host.exe`
  from repo root in MSYS2. Captures the first frame containing a draw, skipping
  clear-only frames. Uses Win32 I/O to avoid game MSL/host CRT stdio conflicts.
  Do not commit captures or other game data. Full builds recreate build/phase2.

## Next work

1. Preserve texture coordinates in decoded vertices and bind uploaded images
   and samplers to shaders. Implement the GX/TEV state actually issued by the
   menu, not a hardcoded appearance or mock visuals.
2. Resolve `GXLoadTexObj: source outside RAM`: native font symbols currently use
   placeholder data when generated headers are absent, and GX packed physical
   addresses cannot round-trip arbitrary host pointers. Load required font data
   from the user's disc at runtime and retain valid pointer mappings; never
   commit extracted assets.
3. Implement viewport/scissor, blend/alpha/depth/cull, matrix/texgen and broader
   TEV support with tests and actual frame checks. Keep unsupported cases loud.
4. Audit remaining material/image/animation descriptor byte order and provenance
   invalidation on freed/reused archive memory. One menu loop does not establish
   that all game modes work.

Build invocation and constraints below still apply. Use mandatory `-m32`; run
from repo root. Never run two linkexe builds against this checkout at once.
A timeout in the menu loop is not by itself a boot failure: inspect logs and
backtrace. Do not trust stale incremental object lists after source inclusion
changes. No recompilation/emulation runtime is permitted.

Tests: `pc/tests/run_texture_tests.ps1 -Gpu`, plus standalone
`pc/tests/hsd_endian_test.c` (link pc_hsd_endian.c, pc_hsd_swap.c and
`--large-address-aware`) and `pc/tests/gx_immediate_test.c` (link Vulkan).
Use 32-bit GCC, normal game include paths, `-fgnu89-inline`, and compat.h.
Synthetic decoder tests do not substitute visuals in the running game.

---

# Historical handoff — superseded status below

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
