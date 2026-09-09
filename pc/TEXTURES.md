# Native texture upload

Latest checkpoint: UVs and immutable sampled-image descriptors are wired into a
constrained single-stage TEV shader. Visual verification is still blocked by SIS
glyph byte order producing an invalid font pointer; see the top of HANDOFF.md.
Native font bytes are loaded from the user's disc data, never committed.

`__wrap_GXLoadTexObj` now decodes I4, I8, IA4, IA8, RGB565,
RGB5A3, RGBA8 and CMPR into `VK_FORMAT_R8G8B8A8_UNORM` images.
Mips use complete GX tiles, including the smallest levels. The bridge reads
native GXTexObj metadata, restores the cached RAM address from its physical
image field, and decodes the payload as big-endian tiled bytes. It checks
the complete source span against the mapped 24 MiB RAM region.

Samplers implement clamp/repeat/mirror, nearest/linear, mip filtering and LOD
limits/bias. Paletted/depth/copy formats are rejected with diagnostics and
unbind the affected slot. Anisotropy and GX bias-clamp are diagnosed but not
implemented. Texture payloads must not be byte-swapped by the archive loader.

## Renderer integration

`pc_gx_texture_get(slot, &binding)` exposes an image view, sampler, image
layout and dimensions for each of the eight GX slots. The next shader step
must bind these to **immutable per-draw descriptor sets** and supply the real
GX texture coordinates/TEV state. Upload alone does not make the existing
position/color-only pipeline draw textured geometry.

GXLoadTexObj can occur during a render pass, so uploads use a separate
command pool and synchronous submissions on the graphics queue. Each upload
finishes its transfer-to-fragment-read barrier before it returns. Only the
renderer thread may submit to this queue. Images are never overwritten:
rebinding preserves images already referenced by recorded draws. Cache hits
compare decoded pixels, dimensions/mips and sampler state, so pointer reuse
and changes to dynamic texture data do not return stale images.

`pc_gx_textures_begin_frame()` runs after the previous frame fence and frees
unbound entries; bound slots persist. CPU decoded data and image allocations
have separate 64 MiB bounds, with at most 1024 entries. Exhaustion is reported
and unbinds the slot; resources referenced by the current frame are never
evicted. Call `pc_gx_textures_shutdown()` after frame completion and before
destroying the Vulkan device if a renderer shutdown path is added.

This conservative implementation needs later upload batching and a smarter
cross-frame cache for performance. Direct GXLoadTexObjPreLoaded calls and
texture state encoded in BP display-list registers are not hooked here yet.

## Verification

From PowerShell with MinGW GCC on PATH:

```powershell
./pc/tests/run_texture_tests.ps1 -Gpu
```

Outputs go to this worktree's `build/texture-tests`; this never runs
`tools/phase0/linkexe.sh` or touches `build/phase2`.

CPU tests verify known texels in all eight formats, CMPR palette rules and
quadrants, cropped tile edges, source/destination bounds and output guards.
The headless Vulkan test uploads all eight formats with four mip levels,
reads all levels back and checks bytes, cache sharing, changed data,
rebinding lifetime and invalid-input unbinding. It enables the Khronos
validation layer if installed and reports whether it was available.

Local result: CPU and GPU tests passed on the 32-bit MinGW toolchain.
Khronos validation layer was unavailable, so this is a real GPU readback
test, not a validation-layer pass. No gameplay verification is claimed.

Texture-format reference: Aurora's MIT-licensed `texture_convert.cpp`;
attribution is retained in `pc/licenses/aurora-textures.txt`.
