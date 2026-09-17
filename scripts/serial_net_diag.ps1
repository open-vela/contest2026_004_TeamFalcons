# NSH network diagnostics after boot
param([string]$ComPort = "COM3")
$ErrorActionPreference = "Stop"
$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep 2
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
  Write-Host "`n=== $c ==="
  Write-Host $b
  return $b
}
Read-Serial 20 | Out-Null
foreach ($c in @("ifconfig","ping -c 2 223.5.5.5","nslookup www.baidu.com","vgnet status","ps")) { Cmd $c 20 | Out-Null }
$port.Close()
