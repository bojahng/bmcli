$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RootDir

Write-Host "[bmcli] Creating venv (.venv)"
python -m venv .venv

$Py = Join-Path $RootDir ".venv\Scripts\python.exe"
if (!(Test-Path $Py)) {
  throw "missing venv python at $Py"
}

Write-Host "[bmcli] Bootstrapping pip"
$GetPip = Join-Path $env:TEMP "get-pip.py"
Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile $GetPip
& $Py $GetPip | Out-Null

Write-Host "[bmcli] Installing cmake + ninja + ziglang (user-space)"
& (Join-Path $RootDir ".venv\Scripts\pip.exe") install --no-cache-dir cmake ninja ziglang

Write-Host "[bmcli] Toolchain ready"
& (Join-Path $RootDir ".venv\Scripts\cmake.exe") --version | Select-Object -First 1
& (Join-Path $RootDir ".venv\Scripts\ninja.exe") --version
& $Py -c "import ziglang, pathlib; p=pathlib.Path(ziglang.__file__).resolve().parent/'zig'; print(getattr(p,'with_suffix',lambda s:s)('.exe') if (p.parent/'zig.exe').exists() else p)"

