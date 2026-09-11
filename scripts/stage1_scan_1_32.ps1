# Board-side vgdiscover scan 1-32 (COM3 NSH) while MThings mock on COM6.
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_scan_1_32.ps1

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$Range = "1-32"
)

$ErrorActionPreference = "Stop"

function Wait-Prompt {
  param([string]$Buf, [int]$TimeoutSec = 3)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($Buf -match "nsh>") { return $true }
    Start-Sleep -Milliseconds 100
  }
  return $false
}

$port = New-Object System.IO.Ports.SerialPort
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 8000
$port.NewLine = "`n"

try {
  $port.Open()
  Start-Sleep -Seconds 2
  $port.DiscardInBuffer()
  $port.WriteLine("")
  Start-Sleep -Milliseconds 500

  $port.DiscardInBuffer()
  $port.WriteLine("vgdiscover scan -a $Range")
  $buf = ""
  $deadline = (Get-Date).AddSeconds(180)
  while ((Get-Date) -lt $deadline) {
    if ($port.BytesToRead -gt 0) { $buf += $port.ReadExisting() }
    if ($buf -match "found \d+ slave" -and (Wait-Prompt $buf 2)) { break }
    if ($buf -match "nsh>" -and $buf -match "vgdiscover: found") { break }
    Start-Sleep -Milliseconds 150
  }

  Write-Output $buf

  if ($buf -notmatch "vgdiscover") {
    Write-Output "[FAIL] vgdiscover not in firmware (flash velaguard-lvgl or enable VG_BUS_DISCOVER)"
    exit 2
  }

  if ($buf -match "found (\d+) slave") {
    $n = [int]$Matches[1]
    Write-Output "[INFO] hit count = $n"
    $addrs = [regex]::Matches($buf, "addr=(\d+)") | ForEach-Object { [int]$_.Groups[1].Value }
    Write-Output ("[INFO] addrs = " + ($addrs -join ","))
    if ($n -ge 32) { exit 0 }
    exit 1
  }

  Write-Output "[FAIL] no scan result in output"
  exit 1
}
catch {
  Write-Error $_.Exception.Message
  exit 2
}
finally {
  if ($port.IsOpen) { $port.Close() }
}
