# Recover HMI board: build velaguard-lvgl, flash, run stage1 LVGL accept.
# Usage: powershell.exe -ExecutionPolicy Bypass -File scripts/recover-hmi.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$wsl = "wsl.exe"
$distro = "Debian"

Write-Host "[recover-hmi] build velaguard-lvgl (作品主线)..."
& $wsl -d $distro bash -lc "cd '$($root -replace '\\','/')' && bash scripts/build.sh"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "[recover-hmi] flash..."
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "scripts\flash.ps1")
if ($LASTEXITCODE -ne 0) { throw "flash failed" }

Start-Sleep -Seconds 12
Write-Host "[recover-hmi] accept..."
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "scripts\stage1_lvgl_hmi_accept.ps1")
exit $LASTEXITCODE
