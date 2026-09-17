# 先抢占 COM3，再烧录，再板测（避免烧录后监视器抢端口）。
# 用法：先关闭 Cursor/SSCOM 等 COM3 监视器，然后：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_flash_and_accept.ps1

param(
  [string]$ComPort = "COM3",
  [string]$ContestRoot = ""
)

$ErrorActionPreference = "Stop"
if ($ContestRoot -eq "") {
  $ContestRoot = Split-Path -Parent $PSScriptRoot
}

$flashPs1 = Join-Path $ContestRoot "scripts\flash.ps1"
$acceptPs1 = Join-Path $ContestRoot "scripts\stage1_agent_accept.ps1"

Write-Host "[1/3] Opening $ComPort (close other serial monitors first)..."
$port = New-Object System.IO.Ports.SerialPort
$port.Encoding = [System.Text.Encoding]::UTF8
$port.PortName = $ComPort
$port.BaudRate = 115200
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 500
while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 80 }

Write-Host "[2/3] Flashing via $flashPs1 ..."
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $flashPs1
if ($LASTEXITCODE -ne 0) {
  $port.Close()
  throw "Flash failed exit=$LASTEXITCODE"
}

Write-Host "[3/3] Waiting boot on held $ComPort ..."
Start-Sleep -Seconds 6
$boot = ""
$deadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $deadline) {
  if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
  if ($boot -match "nsh>") { break }
  Start-Sleep -Milliseconds 200
}
Write-Host $boot

$port.Close()

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $acceptPs1 -ComPort $ComPort
exit $LASTEXITCODE
