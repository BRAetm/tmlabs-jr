# Builds labs.dll (native engine) for Windows x64.
# Uses clang-cl (for C99 VLAs) inside the MSVC developer environment
# (for Windows SDK headers + libs + linker), with Ninja as the generator.
#
# Requires:
#   - Visual Studio 2022 Build Tools ("Desktop development with C++")
#   - LLVM (clang-cl) at C:\Program Files\LLVM\bin
#   - Python 3 on PATH (nanopb code-gen)
#   - Git
# Bootstraps vcpkg in-tree.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $root 'chiaki-ng'
$build = Join-Path $engine 'build'
$vcpkg = Join-Path $root '.vcpkg'

# --- Locate tools ------------------------------------------------------------
$vsBuildTools = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$vsCmake      = Join-Path $vsBuildTools 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$vsNinja      = Join-Path $vsBuildTools 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$llvmBin      = 'C:\Program Files\LLVM\bin'
$vcvars       = Join-Path $vsBuildTools 'VC\Auxiliary\Build\vcvars64.bat'

foreach ($p in @($vsBuildTools, $vcvars, $llvmBin)) {
    if (-not (Test-Path $p)) { throw "Missing prerequisite: $p" }
}

# Order matters: LLVM first so clang-cl resolves before MSVC cl.exe
$env:PATH = "$llvmBin;$vsCmake;$vsNinja;$env:PATH"

if (-not (Test-Path $engine)) { throw "Native engine not found at $engine" }

# --- vcpkg bootstrap ---------------------------------------------------------
if (-not (Test-Path $vcpkg)) {
    Write-Host "Cloning vcpkg into $vcpkg ..." -ForegroundColor Cyan
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkg
}
if (-not (Test-Path (Join-Path $vcpkg 'vcpkg.exe'))) {
    Write-Host "Bootstrapping vcpkg ..." -ForegroundColor Cyan
    & (Join-Path $vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
}
$toolchain = Join-Path $vcpkg 'scripts\buildsystems\vcpkg.cmake'

# --- Import the MSVC dev environment into this PowerShell session ------------
# vcvars64.bat sets INCLUDE, LIB, PATH so clang-cl can find Windows SDK headers
# and the MSVC linker.
Write-Host "Importing MSVC dev environment ..." -ForegroundColor Cyan
$envDump = cmd /c "`"$vcvars`" >NUL && set"
foreach ($line in $envDump) {
    if ($line -match '^(?<k>[^=]+)=(?<v>.*)$') {
        [Environment]::SetEnvironmentVariable($Matches['k'], $Matches['v'])
    }
}
# Re-prepend LLVM so clang-cl wins over any cl.exe on PATH.
# Also re-prepend real Python so vcpkg/meson don't pick the WindowsApps stub.
$realPython = @(
    'C:\Program Files\Python311',
    'C:\Program Files\Python312',
    "$env:LOCALAPPDATA\Programs\Python\Python311",
    "$env:LOCALAPPDATA\Programs\Python\Python312"
) | Where-Object { Test-Path (Join-Path $_ 'python.exe') } | Select-Object -First 1
if (-not $realPython) { throw "No real Python 3.11/3.12 found" }
$env:PATH = "$llvmBin;$realPython;$realPython\Scripts;$env:PATH"
# Strip WindowsApps so the python.exe stub never wins
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'WindowsApps' }) -join ';'
# Add vcpkg-installed protoc.exe so nanopb code-gen can find it
$protocDir = Join-Path $build 'vcpkg_installed\x64-windows\tools\protobuf'
if (Test-Path $protocDir) { $env:PATH = "$protocDir;$env:PATH" }

# --- Configure + build -------------------------------------------------------
Write-Host "Configuring CMake (Ninja + clang-cl) ..." -ForegroundColor Cyan
cmake -S $engine -B $build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER=clang-cl `
    -DCMAKE_CXX_COMPILER=clang-cl `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows

Write-Host "Building labs.dll ..." -ForegroundColor Cyan
cmake --build $build --parallel

# --- Stage outputs where the C# solution can find them -----------------------
$out = Join-Path $engine 'build\bin'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$dll = Get-ChildItem -Recurse -Path $build -Filter 'labs.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($dll) {
    Copy-Item $dll.FullName -Destination (Join-Path $out 'labs.dll') -Force
    $runtime = Join-Path $vcpkg 'installed\x64-windows\bin'
    if (Test-Path $runtime) {
        Get-ChildItem $runtime -Filter '*.dll' | Copy-Item -Destination $out -Force
    }
    Write-Host "`nDone. labs.dll is at $out\labs.dll" -ForegroundColor Green
} else {
    throw "labs.dll was not produced - check the build log above."
}
