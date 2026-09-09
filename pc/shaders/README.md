# Shaders

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
