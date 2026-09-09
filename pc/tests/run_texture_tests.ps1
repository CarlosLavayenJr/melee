param([switch]$Gpu)
$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$testOutput = Join-Path $repoPath 'build/texture-tests'
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$compiler = Get-Command gcc -ErrorAction SilentlyContinue
if ($compiler) { $compilerPath = $compiler.Source }
else { $compilerPath = 'C:/Users/Owner/msys64/mingw32/bin/gcc.exe' }
if (!(Test-Path -LiteralPath $compilerPath)) { throw 'Put MinGW GCC on PATH.' }
$env:PATH = (Split-Path $compilerPath) + ';' + $env:PATH
$common = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-g', '-I', (Join-Path $repoPath 'pc/src'))
$decoder = Join-Path $repoPath 'pc/src/pc_texture_decode.c'
$exe = Join-Path $testOutput 'texture_decode_test.exe'
& $compilerPath @common $decoder (Join-Path $PSScriptRoot 'texture_decode_test.c') -o $exe
if ($LASTEXITCODE) { throw 'Decoder test compilation failed.' }
& $exe
if ($LASTEXITCODE) { throw 'Decoder tests failed.' }
if ($Gpu) {
    $exe = Join-Path $testOutput 'texture_vulkan_test.exe'
    & $compilerPath @common $decoder (Join-Path $repoPath 'pc/src/pc_gx_texture.c') (Join-Path $PSScriptRoot 'texture_vulkan_test.c') -lvulkan-1 -o $exe
    if ($LASTEXITCODE) { throw 'Vulkan test compilation failed.' }
    & $exe
    if ($LASTEXITCODE) { throw 'Vulkan tests failed (a Vulkan device is required).' }
}
