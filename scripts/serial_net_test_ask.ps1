# Test net_test and ask at vela> after daemon start
param([string]$ComPort = "COM3")
$ErrorActionPreference = "Stop"
$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep 1

function Read-Serial([int]$sec) {
  $b = ""
  $dl = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) { $b += $port.ReadExisting() }
    Start-Sleep -Milliseconds 100
  }
  return $b
}
function Cmd([string]$c, [int]$sec, [string]$want = "") {
  $port.DiscardInBuffer()
  $port.Write("$c`r")
  Start-Sleep -Milliseconds 400
  $b = ""
  $dl = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) { $b += $port.ReadExisting() }
    if ($want -eq "vela" -and $b -match "vela>") { break }
    if ($want -eq "nsh" -and $b -match "nsh>") { break }
    Start-Sleep -Milliseconds 100
  }
  Write-Host "`n=== $c === len=$($b.Length)"
  Write-Host $b
  return $b
}

Read-Serial 10 | Out-Null
Cmd "vgprovision status" 10 | Out-Null
Cmd "ai_agent --daemon &" 60 "nsh" | Out-Null
Start-Sleep 5
Cmd "ai_agent" 30 "vela" | Out-Null
Cmd "net_status" 15 | Out-Null
Cmd "config_show" 15 | Out-Null
Cmd "net_test" 90 | Out-Null
Cmd "ask 读取从站1温湿度，请用 run_shell 执行 vgmodbus -a 1 -r 0 -c 2 -n 1 -i 0" 120 | Out-Null
$port.Close()
