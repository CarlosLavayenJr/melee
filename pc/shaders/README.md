# Shaders

Latest checkpoint: UVs and up to four ADD/SUB TEV stages, including register
feedback, konst selectors, eight texture bindings and alpha comparison. The
game's four-stage YUV movie program renders a verified real opening frame.
Vertex push constants contain a 64-byte projection matrix. Fragment state is
an immutable 208-byte std140 uniform block per draw (see pc_gx_material.h).
Unsupported states are diagnosed by pc_gx_material.c. Arithmetic is still
floating-point, not bit-exact GX fixed-point; rounding and unclamped-register
feedback need further audit. Broader materials remain unverified; see HANDOFF.md.
Regenerate binaries AND embedded aligned headers with `./pc/shaders/regenerate.ps1`.
The original design notes below predate this implementation.

`gx_basic.vert`/`gx_basic.frag` are the first milestone's fixed pipeline:
position + vertex color, no texturing or TEV (see `pc/GX_RENDERER.md`). No
32-bit `glslc`/`shaderc` package exists (checked: not in MSYS2's mingw-w64-i686
repo), so these are compiled ahead of time with a 64-bit `glslc`
(`mingw-w64-x86_64-shaderc`) and the resulting SPIR-V is checked in as a C
byte array (`pc/src/gx_basic_{vert,frag}_spv.h`), not compiled as part of the
normal build.

Regenerate after editing either `.vert`/`.frag`:

```sh
glslc gx_basic.vert -o gx_basic.vert.spv
glslc gx_basic.frag -o gx_basic.frag.spv
# then re-embed as a C array -- see the python snippet in the commit that
# added this file, or write your own; it's a 16-bytes-per-line hex/decimal
# dump with a leading length constant.
```

Runtime TEV-stage-configuration-to-shader compilation (task 10 in
`pc/GX_RENDERER.md`) will need an actual runtime compiler, not this
ahead-of-time approach -- either a subprocess call to a 64-bit `glslc.exe`,
or a small SPIR-V emitter written directly against the TEV stage math,
skipping GLSL text entirely. Not decided yet.
