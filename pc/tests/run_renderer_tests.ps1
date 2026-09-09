param([switch]$Gpu)
$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compilerPath = 'C:/Users/Owner/msys64/mingw32/bin/gcc.exe'
$env:PATH = (Split-Path $compilerPath) + ';' + $env:PATH
$common = @('-m32', '-w', '-O0', '-g', '-fgnu89-inline', '-std=gnu17',
    '-DPC_GX_RENDERER', '-include', 'tools/phase0/compat.h', '-I', 'pc/src',
    '-I', 'src', '-I', 'extern/dolphin/include', '-I', 'extern/dolphin/src',
    '-DVERSION_GALE01', '-DBUILD_VERSION=0', '-Wl,--large-address-aware')
Push-Location $repoPath
try {
    New-Item -ItemType Directory -Force -Path 'build/renderer-tests' | Out-Null
    $tests = @(
        @{Name='os_time'; Sources=@('pc/src/pc_bootinfo.c','pc/src/pc_os_time.c'); Libs=@()},
        @{Name='gx_material'; Sources=@(); Libs=@()},
        @{Name='gx_immediate'; Sources=@(); Libs=@('-lvulkan-1')}
    )
    if ($Gpu) {
        $tests += @{Name='tev_vulkan'; Sources=@('pc/src/pc_vulkan.c',
            'pc/src/pc_texture_decode.c','pc/src/pc_gx_texture.c','pc/src/pc_sys_windows.c');
            Libs=@('-lvulkan-1','-lgdi32','-luser32')}
    }
    foreach ($test in $tests) {
        $output = "build/renderer-tests/$($test.Name)_test.exe"
        & $compilerPath @common "pc/tests/$($test.Name)_test.c" @($test.Sources) @($test.Libs) -o $output
        if ($LASTEXITCODE) { throw "Compilation failed: $($test.Name)" }
        & ".\$output"
        if ($LASTEXITCODE) { throw "Test failed: $($test.Name)" }
    }
} finally { Pop-Location }
