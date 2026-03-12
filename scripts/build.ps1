$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RootDir

$CMake = Join-Path $RootDir ".venv\Scripts\cmake.exe"
$Ninja = Join-Path $RootDir ".venv\Scripts\ninja.exe"

if (!(Test-Path $CMake)) { throw "missing $CMake; run: scripts\install_toolchain.ps1" }
if (!(Test-Path $Ninja)) { throw "missing $Ninja; run: scripts\install_toolchain.ps1" }

& $CMake -S . -B build -G Ninja `
  -DCMAKE_CXX_COMPILER=(Join-Path $RootDir "scripts\zig-cxx.cmd") `
  -DCMAKE_MAKE_PROGRAM=$Ninja

& $CMake --build build
& (Join-Path $RootDir "build\bmcli.exe") --help | Out-Null
Write-Host "ok: build\bmcli.exe"

