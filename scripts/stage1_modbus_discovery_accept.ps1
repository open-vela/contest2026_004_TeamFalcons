# Stage1 modbus-discovery 板端验收（COM3 NSH；从站经 RS485）。
# 前置：Modbus Slave @COM6 9600 8N1（推荐）
#   .\scripts\build_velaguard_mbslave.ps1 -OpenConnection -KeepOpenSeconds 900
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_modbus_discovery_accept.ps1

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$Log = ""
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

function Wait-Prompt {
  param([string]$Buf, [int]$TimeoutSec = 12)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($Buf -match "nsh>") { return $true }
    Start-Sleep -Milliseconds 120
  }
  return $false
}

function Send-Serial {
  param(
    [System.IO.Ports.SerialPort]$Port,
    [string]$Cmd,
    [int]$WaitSec = 20
  )
  $Port.DiscardInBuffer()
  $Port.WriteLine($Cmd)
  Start-Sleep -Milliseconds 800
  $buf = ""
  $deadline = (Get-Date).AddSeconds($WaitSec)
  while ((Get-Date) -lt $deadline) {
    if ($Port.BytesToRead -gt 0) { $buf += $Port.ReadExisting() }
    if (Wait-Prompt $buf 1) { break }
    Start-Sleep -Milliseconds 120
  }
  $block = "`n=== $Cmd ===`n$buf"
  Write-Output $block
  return @{ Text = $buf; Block = $block }
}

function Assert-Match {
  param([string]$Name, [string]$Hay, [string]$Pattern)
  if ($Hay -match $Pattern) {
    Write-Output "[PASS] $Name"
    $script:pass++
  } else {
    Write-Output "[FAIL] $Name (pattern: $Pattern)"
    $script:fail++
  }
}

$port = New-Object System.IO.Ports.SerialPort
$port.Encoding = [System.Text.Encoding]::UTF8
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 8000
$port.NewLine = "`n"
$logLines = New-Object System.Collections.Generic.List[string]

try {
  $port.Open()
  # Wait for NSH after flash/reset (agent boot ~3s; allow margin).
  Start-Sleep -Seconds 8
  $boot = ""
  $bootDeadline = (Get-Date).AddSeconds(25)
  while ((Get-Date) -lt $bootDeadline) {
    if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
    if ($boot -match "nsh>|AI Agent ready") { break }
    Start-Sleep -Milliseconds 200
  }
  while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 80 }

  $r = Send-Serial $port "?" 8
  Assert-Match "vgdiscover in help" $r.Text "vgdiscover"

  $r = Send-Serial $port "vgdiscover scan -a 1-8" 45
  if ($r.Text -notmatch "addr=1|found [1-9] slave") {
    Write-Output $r.Block
  }
  Assert-Match "scan found slave" $r.Text "addr=1|found 1 slave|found [1-9] slave"

  $r = Send-Serial $port "vgdiscover probe -a 1" 60
  Assert-Match "probe blocks" $r.Text "blocks=[1-9]|sample\[0\]=[0-9]+"

  $r = Send-Serial $port "vgdiscover dump" 15
  Assert-Match "dump points" $r.Text "wrote [1-9] points|point_table_candidate"

  $r = Send-Serial $port "vgdiscover test-read -a 1 -r 0 -c 2" 20
  Assert-Match "test-read values" $r.Text "\[0\]=[0-9]+"

  $r = Send-Serial $port "vgdiscover apply --confirm" 15
  Assert-Match "apply confirm" $r.Text "applied [1-9] points|vg_config_commit"

  $r = Send-Serial $port "vgcfg dump" 10
  Assert-Match "vgcfg after apply" $r.Text "discovered|OK|seq="

  $r = Send-Serial $port "cat /data/velaguard/config/points.json" 10
  Assert-Match "points.json" $r.Text "schema_version|points"

  Write-Output "`n[stage1_modbus_discovery_accept] pass=$($script:pass) fail=$($script:fail)"
  if ($Log -ne "") {
    $logLines | Out-File -FilePath $Log -Encoding utf8
  }
  if ($script:fail -gt 0) { exit 1 }
}
catch {
  Write-Error "stage1_modbus_discovery_accept failed: $($_.Exception.Message)"
  exit 1
}
finally {
  if ($port.IsOpen) { $port.Close() }
}
