# Wait for DHCP then check network
param([string]$ComPort = "COM3", [int]$WaitSec = 60)
$ErrorActionPreference = "Stop"
$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.Open()
Start-Sleep 3
while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 80 }
Write-Host "Waiting ${WaitSec}s for network..."
Start-Sleep -Seconds $WaitSec
function Cmd([string]$c, [int]$sec) {
  $port.DiscardInBuffer()
  $port.Write("$c`r")
  Start-Sleep -Milliseconds 400
  $b = ""
  $dl = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) { $b += $port.ReadExisting() }
    Start-Sleep -Milliseconds 100
  }
  Write-Host "`n=== $c ==="
  Write-Host $b
}
Cmd "ifconfig" 15
Cmd "vgnet status" 15
Cmd "ping -c 2 223.5.5.5" 25
$port.Close()
