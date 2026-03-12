$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RootDir

$MockPort = if ($env:MOCK_PORT) { [int]$env:MOCK_PORT } else { 18080 }
$MockUser = if ($env:MOCK_USER) { $env:MOCK_USER } else { "admin" }
$MockPass = if ($env:MOCK_PASS) { $env:MOCK_PASS } else { "admin" }

Write-Host "[smoke] starting mock redfish server on port $MockPort"
$p = Start-Process -FilePath "python" -ArgumentList @("mock/redfish/server.py","--listen","127.0.0.1","--port","$MockPort","--user","$MockUser","--password","$MockPass") -PassThru -WindowStyle Hidden
try {
  Start-Sleep -Milliseconds 200

  Write-Host "[smoke] build"
  powershell -ExecutionPolicy Bypass -File .\scripts\install_toolchain.ps1 | Out-Null
  powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 | Out-Null

  Write-Host "[smoke] single target"
  .\build\bmcli.exe --host "127.0.0.1:$MockPort" --user $MockUser --password $MockPass --protocol redfish power status -o json | Out-Null

  Write-Host "[smoke] multiple targets + cmd file"
  .\build\bmcli.exe --targets examples\targets.txt --cmd-file examples\commands.txt -o json | Out-Null

  Write-Host "[smoke] repeat"
  .\build\bmcli.exe --targets examples\targets.txt --cmd "power status" --every 100ms --repeat 3 -o table | Out-Null

  Write-Host "ok: smoke tests passed"
} finally {
  if ($p -and !$p.HasExited) { $p.Kill() | Out-Null }
}
