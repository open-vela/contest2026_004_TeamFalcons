# Open all 32 VelaGuard Modbus Slave windows (NN_<sensor>.mbs).
#
# IMPORTANT: Mbslave.Document COM windows are owned by this process. If the
# script exits, every window closes. This script ALWAYS holds until you press
# Enter (or -KeepOpenSeconds elapses).
#
# Usage:
#   .\open_velaguard_mbslaves.ps1
#   .\open_velaguard_mbslaves.ps1 -ComPort 4 -OpenConnection
#   .\open_velaguard_mbslaves.ps1 -Dir "$env:USERPROFILE\Documents\mthings"

param(
  [string]$Dir = "$env:USERPROFILE\Documents\mthings",
  [int]$ComPort = 4,
  [int]$Baud = 9600,
  [switch]$OpenConnection,
  [int]$KeepOpenSeconds = 0
)

$ErrorActionPreference = "Stop"

function Get-SafeSensorName([string]$Name) {
  $n = $Name -replace '^\[S\]', ''
  $n = $n.Trim()
  foreach ($c in @('<', '>', ':', '"', '/', '\', '|', '?', '*')) {
    $n = $n.Replace($c, '_')
  }
  $n = $n -replace '\s+', ''
  if ([string]::IsNullOrWhiteSpace($n)) { $n = 'sensor' }
  return $n
}

function Resolve-SlaveMbs([string]$Root, [int]$Addr, [string]$Name) {
  $safe = Get-SafeSensorName $Name
  $named = Join-Path $Root ("{0:D2}_{1}.mbs" -f $Addr, $safe)
  if (Test-Path $named) { return $named }
  $legacy = Join-Path $Root ("velaguard_slave{0:D2}.mbs" -f $Addr)
  if (Test-Path $legacy) { return $legacy }
  return $null
}

if (-not (Test-Path $Dir)) {
  throw "Directory not found: $Dir"
}
$Dir = (Resolve-Path $Dir).Path

$repoRoot = Split-Path -Parent $PSScriptRoot
$csvPath = Join-Path $repoRoot "config\modbus-slave\velaguard_slaves.csv"
# When this script lives in Documents\mthings, also try contest repo CSV.
if (-not (Test-Path $csvPath)) {
  $candidates = @(
    "$env:USERPROFILE\openvela\contest2026_004_TeamFalcons\config\modbus-slave\velaguard_slaves.csv",
    "\\wsl$\Ubuntu\home\hello19y\openvela\contest2026_004_TeamFalcons\config\modbus-slave\velaguard_slaves.csv",
    "\\wsl.localhost\Ubuntu\home\hello19y\openvela\contest2026_004_TeamFalcons\config\modbus-slave\velaguard_slaves.csv"
  )
  foreach ($c in $candidates) {
    if (Test-Path $c) { $csvPath = $c; break }
  }
}

$files = @()
if (Test-Path $csvPath) {
  foreach ($s in (Import-Csv $csvPath)) {
    $addr = [int]$s.addr
    $path = Resolve-SlaveMbs $Dir $addr $s.name
    if (-not $path) { throw "Missing mbs for addr=$addr under $Dir" }
    $files += [pscustomobject]@{ Addr = $addr; Name = $s.name; File = $path }
  }
}
else {
  $named = Get-ChildItem -Path $Dir -Filter "*.mbs" |
    Where-Object { $_.Name -match '^\d{2}_.+\.mbs$' } |
    Sort-Object Name
  if ($named.Count -eq 0) {
    throw "No NN_<sensor>.mbs in $Dir"
  }
  foreach ($f in $named) {
    $files += [pscustomobject]@{
      Addr = [int]$f.BaseName.Substring(0, 2)
      Name = $f.BaseName
      File = $f.FullName
    }
  }
}

$files = @($files | Sort-Object Addr)
Write-Output "=== Open $($files.Count) Modbus Slave docs from $Dir ==="
Write-Output "NOTE: keep this console open — closing it closes all slave windows."

try {
  $app = New-Object -ComObject Mbslave.Application
}
catch {
  throw "Mbslave.Application COM failed. Install Modbus Slave and run it once."
}

$app.Connection = 0
$app.SerialPort = $ComPort
$app.BaudRate = $Baud
$app.DataBits = 8
$app.Parity = 0
$app.StopBits = 1
try { $app.Mode = 0 } catch {}

# ArrayList keeps strong refs so RCW isn't GC'd mid-hold.
$docs = New-Object System.Collections.ArrayList
foreach ($f in $files) {
  $doc = New-Object -ComObject Mbslave.Document
  $ok = $false
  try { $ok = [bool]$doc.Open([string]$f.File) } catch {}
  if (-not $ok) {
    throw "Failed to open $($f.File)"
  }
  try { $doc.ShowWindow() | Out-Null } catch {}
  [void]$docs.Add($doc)
  Write-Output ("addr={0,2} opened {1}" -f $f.Addr, (Split-Path $f.File -Leaf))
}

if ($OpenConnection) {
  $st = $app.OpenConnection()
  Write-Output "OpenConnection status=$st (COM$ComPort @ $Baud 8N1)"
}
else {
  Write-Output ""
  Write-Output "Serial not opened by script. In Modbus Slave: Connection -> COM$ComPort @ $Baud 8N1 -> Connect"
  Write-Output "Or re-run with: -OpenConnection"
}

Write-Output ""
Write-Output "Holding $($docs.Count) windows alive via COM."
try {
  if ($KeepOpenSeconds -gt 0) {
    Write-Output "Auto-exit in ${KeepOpenSeconds}s (Ctrl+C sooner)..."
    Start-Sleep -Seconds $KeepOpenSeconds
  }
  else {
    Write-Host -ForegroundColor Yellow "Press Enter here when finished (windows will then close)."
    [void][System.Console]::ReadLine()
  }
}
finally {
  Write-Output "Releasing COM docs..."
  try { $app.CloseConnection() | Out-Null } catch {}
  for ($i = $docs.Count - 1; $i -ge 0; $i--) {
    try { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($docs[$i]) } catch {}
  }
  try { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($app) } catch {}
  [GC]::Collect()
  [GC]::WaitForPendingFinalizers()
}
