#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Release,
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$AppDir    = Resolve-Path (Join-Path $ScriptDir '..')
$Config    = if ($Release) { 'release' } else { 'debug' }
$BuildDir  = Join-Path $AppDir "build/msvc-$Config"
$BinDir    = Join-Path $BuildDir 'bin'

$QtDir      = 'C:/Qt/6.8.3/msvc2022_64'
$VsWhere    = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path $QtDir)) { throw "Qt not found at $QtDir" }

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[build] cleaning $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

# Locate MSVC via vswhere, import its dev environment
if (-not (Test-Path $VsWhere)) { throw "vswhere not found at $VsWhere" }
$VsPath = & $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VsPath) { throw 'MSVC toolchain not found' }

$VcVars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $VcVars)) { throw "vcvars64.bat not found at $VcVars" }

# Pull env from vcvars into this process
$envDump = cmd /c "`"$VcVars`" > nul && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$CMakeBuildType = if ($Release) { 'Release' } else { 'Debug' }

Write-Host "[build] configure ($CMakeBuildType) -> $BuildDir"
$env:CMAKE_PREFIX_PATH = $QtDir
& cmake -G Ninja -S "$AppDir" -B "$BuildDir" "-DCMAKE_BUILD_TYPE=$CMakeBuildType" "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

Write-Host "[build] compile"
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

# Deploy Qt runtime DLLs next to the exe (first build only, or after clean)
$Exe = Join-Path $BinDir 'LabsEngine.exe'
if (Test-Path $Exe) {
    $WinDeploy = Join-Path $QtDir 'bin\windeployqt.exe'
    if (Test-Path $WinDeploy) {
        Write-Host "[build] windeployqt"
        & $WinDeploy --no-translations --no-compiler-runtime $Exe | Out-Null
    }
}

if ($Run) {
    if (-not (Test-Path $Exe)) { throw "LabsEngine.exe not found at $Exe" }
    Write-Host "[build] run $Exe"
    & $Exe
}
