# Debug ai_agent daemon + attach on COM3
param([string]$ComPort = "COM3")
$ErrorActionPreference = "Stop"
$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 500

function Read-Serial([int]$sec) {
  $b = ""
  $dl = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) { $b += $port.ReadExisting() }
    Start-Sleep -Milliseconds 100
  }
  return $b
}
function Cmd([string]$c, [int]$sec) {
  $port.DiscardInBuffer()
  $port.Write("$c`r")
  Start-Sleep -Milliseconds 400
  $b = Read-Serial $sec
  Write-Host "`n=== $c === len=$($b.Length)"
  Write-Host $b
  return $b
}

$boot = Read-Serial 15
Write-Host "=== boot len=$($boot.Length) ==="
Write-Host $boot

Cmd "?" 8 | Out-Null
Cmd "vgprovision status" 10 | Out-Null
Cmd "ls /data/agent/config" 10 | Out-Null
$d = Cmd "ai_agent --daemon &" 30
Start-Sleep -Seconds 5
Cmd "ps" 12 | Out-Null
$a = Cmd "ai_agent" 60
if ($a -notmatch "vela>") {
  Write-Host "=== attach failed, try dmesg ==="
  Cmd "dmesg" 15 | Out-Null
}
$port.Close()
