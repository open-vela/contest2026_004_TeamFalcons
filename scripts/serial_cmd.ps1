# Run a list of NSH commands on COM3 and print each response.
#
# ASCII-only on purpose: use serial_ask_probe.ps1 for anything that needs a
# Chinese question, because a command line handed to powershell.exe from WSL
# does not survive the trip.
#
# 用法（命令用分号分隔；-File 不会把逗号分隔的参数拆成数组）：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/serial_cmd.ps1 `
#     -Commands "vgagent status;vgagent ask hello;vgagent status"
param(
  [string]$ComPort = "COM3",
  [string]$Commands = "",
  [int]$BootSec = 12,
  [int]$PerCmdSec = 20
)
$ErrorActionPreference = "Stop"

if ($Commands.Trim() -eq "") { throw "需要至少一条 -Commands" }

$cmdList = $Commands.Split(";") | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }

$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()

function Drain([int]$sec) {
  $b = ""
  $dl = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) { $b += $port.ReadExisting() }
    Start-Sleep -Milliseconds 100
  }
  return $b
}

$settle = Drain $BootSec
Write-Host "########## boot settle len=$($settle.Length) ##########"
Write-Host $settle

foreach ($c in $cmdList) {
  $port.DiscardInBuffer()
  $port.Write("$c`r")
  Start-Sleep -Milliseconds 400
  $b = Drain $PerCmdSec
  Write-Host "`n########## $c ########## len=$($b.Length)"
  Write-Host $b
}

$port.Close()
