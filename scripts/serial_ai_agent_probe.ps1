# Capture full ai_agent startup output
param([string]$ComPort = "COM3", [int]$Baud = 115200)
$ErrorActionPreference = "Stop"
$port = New-Object System.IO.Ports.SerialPort
$port.Encoding = [System.Text.Encoding]::UTF8
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 500
$boot = ""
$dl = (Get-Date).AddSeconds(15)
while ((Get-Date) -lt $dl) {
  if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
  if ($boot -match "nsh>") { break }
  Start-Sleep -Milliseconds 120
}
Write-Host "=== BOOT tail ==="
if ($boot.Length -gt 400) { Write-Host $boot.Substring($boot.Length - 400) } else { Write-Host $boot }

$port.DiscardInBuffer()
$port.Write("ai_agent`r")
Start-Sleep -Milliseconds 500
$buf = ""
$dl = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $dl) {
  if ($port.BytesToRead -gt 0) { $buf += $port.ReadExisting() }
  if ($buf -match "vela>") { Write-Host "=== GOT vela> at $(((Get-Date) - $dl.AddSeconds(-90)).TotalSeconds)s ==="; break }
  if ($buf -match "panic|Assertion|fault|FATAL|error:") { Write-Host "=== ERROR DETECTED ==="; break }
  Start-Sleep -Milliseconds 120
}
Write-Host "=== ai_agent output len=$($buf.Length) ==="
Write-Host $buf
$port.Close()
