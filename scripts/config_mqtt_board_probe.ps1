param(
  [string]$ComPort = "COM3"
)

$ErrorActionPreference = "Stop"
$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.ReadTimeout = 10000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 400
while ($port.BytesToRead -gt 0) {
  [void]$port.ReadExisting()
  Start-Sleep -Milliseconds 40
}

function Send-Command {
  param([string]$Command)

  $port.DiscardInBuffer()
  $port.Write("$Command`r")
  Start-Sleep -Milliseconds 300
  $buffer = ""
  $deadline = (Get-Date).AddSeconds(8)
  while ((Get-Date) -lt $deadline) {
    if ($port.BytesToRead -gt 0) {
      $buffer += $port.ReadExisting()
    }
    if ($buffer -match "nsh>") {
      break
    }
    Start-Sleep -Milliseconds 80
  }
  Write-Host "=== $Command ==="
  Write-Host $buffer
}

Send-Command "vgconfigmqtt status"
Send-Command "vgpoint list"
Send-Command "vgpoint get"
Send-Command "ps"
Send-Command "free"
$port.Close()
