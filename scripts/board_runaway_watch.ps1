# Hold COM3 and watch for HARDFAULT / silent NSH.
# Agent loop ticks every -TickSec so a long session can catch 跑飞.
param(
  [string]$ComPort = "COM3",
  [int]$TickSec = 120,
  [string]$LogPath = ""
)
$ErrorActionPreference = "Stop"
if ($LogPath -eq "") {
  $root = Split-Path -Parent $PSScriptRoot
  $LogPath = Join-Path $root ".debug\board_watch.log"
}
New-Item -ItemType Directory -Force -Path (Split-Path $LogPath) | Out-Null
"[watch] start $(Get-Date -Format o) port=$ComPort" | Set-Content -Path $LogPath

$port = New-Object System.IO.Ports.SerialPort
$port.PortName = $ComPort
$port.BaudRate = 115200
$port.ReadTimeout = 800
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Write-Host "[watch] COM3 open, logging $LogPath"
$lastTick = Get-Date
$echoWait = $false
$echoBuf = ""
$echoAt = Get-Date

function Emit([string]$kind, [string]$prompt) {
  $line = "$kind {`"prompt`":`"$prompt`"}"
  Write-Host $line
  Add-Content -Path $LogPath -Value $line
}

while ($true) {
  if ($port.BytesToRead -gt 0) {
    $chunk = $port.ReadExisting()
    [System.IO.File]::AppendAllText($LogPath, $chunk)
    [Console]::Out.Write($chunk)
    if ($echoWait) { $echoBuf += $chunk }
    if ($chunk -match "HARDFAULT|Assertion failed|panic:") {
      Emit "AGENT_LOOP_WAKE_board-runaway" "Board HARDFAULT or panic. Read .debug/board_watch.log tail, diagnose, fix."
    }
  }
  $now = Get-Date
  if ($echoWait -and ($now - $echoAt).TotalSeconds -ge 3) {
    $echoWait = $false
    if ($echoBuf -notmatch "nsh>") {
      Emit "AGENT_LOOP_WAKE_board-runaway" "NSH silent after CR. Board likely wedged. Read .debug/board_watch.log."
    }
  }
  if (-not $echoWait -and ($now - $lastTick).TotalSeconds -ge $TickSec) {
    $lastTick = $now
    $echoWait = $true
    $echoBuf = ""
    $echoAt = $now
    try { $port.Write("`r") } catch {}
    Emit "AGENT_LOOP_TICK_board-health" "Read .debug/board_watch.log tail. Confirm nsh alive. If HARDFAULT or silent, fix."
  }
  Start-Sleep -Milliseconds 200
}
