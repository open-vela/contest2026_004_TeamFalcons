# Build Modbus Slave workspace for VelaGuard 32-slave mock bus.
# Requires Modbus Slave (Mbslave.Application COM) on Windows.
#
# Usage (PowerShell):
#   .\scripts\build_velaguard_mbslave.ps1
#   .\scripts\build_velaguard_mbslave.ps1 -OpenConnection -KeepOpen
#   .\scripts\build_velaguard_mbslave.ps1 -OpenConnection -KeepOpenSeconds 3600
#
# Output: %USERPROFILE%\Documents\mthings\NN_<sensor>.mbs
#   e.g. 01_温湿度-导轨V1.5.mbs  (names from velaguard.mthings / CSV)

param(
  [int]$ComPort = 6,
  [int]$Baud = 9600,
  [switch]$OpenConnection,
  [switch]$ShowWindows,
  [switch]$KeepOpen,
  [int]$KeepOpenSeconds = 0,
  [string]$OutDir = "$env:USERPROFILE\Documents\mthings"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$csvPath = Join-Path $repoRoot "config\modbus-slave\velaguard_slaves.csv"

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

if (-not (Test-Path $csvPath)) {
  throw "Missing $csvPath — run export from velaguard.mthings first."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$slaves = Import-Csv $csvPath

Write-Output "=== Modbus Slave: VelaGuard $($slaves.Count) slaves, COM$ComPort @ $Baud 8N1 ==="

try {
  $app = New-Object -ComObject Mbslave.Application
}
catch {
  throw "Mbslave.Application COM failed. Install Modbus Slave and register COM (run mbslave once as admin)."
}

$app.Connection = 0          # Serial
$app.SerialPort = $ComPort
$app.BaudRate = $Baud
$app.DataBits = 8
$app.Parity = 0              # None (8N1, same as board / MThings)
$app.StopBits = 1
try { $app.Mode = 0 } catch {}  # RTU (if property exists)

$docs = @()
foreach ($s in $slaves) {
  $addr = [int]$s.addr
  $qty = [int]$s.holding_qty
  $name = $s.name
  $mbs = Join-Path $OutDir ("{0:D2}_{1}.mbs" -f $addr, (Get-SafeSensorName $name))
  # Remove legacy numbered name if present so the folder stays tidy.
  $legacy = Join-Path $OutDir ("velaguard_slave{0:D2}.mbs" -f $addr)
  if ((Test-Path $legacy) -and ($legacy -ne $mbs)) {
    Remove-Item -Force $legacy
  }

  $doc = New-Object -ComObject Mbslave.Document
  $ok = $doc.SetupHoldingRegisters($addr, 0, $qty)
  if (-not $ok) {
    throw "SetupHoldingRegisters failed addr=$addr qty=$qty"
  }

  $doc.EnableRefresh = $false
  $seedReg = [int]$s.seed_reg
  $seedVal = [int]$s.seed_value
  if ($seedReg -ge 0 -and $seedReg -lt $qty) {
    $doc.URegisters($seedReg) = $seedVal
  }
  # Give every slave a non-zero at reg0 unless addr=2 (first point at reg2).
  if ($addr -ne 2 -and $qty -gt 0) {
    $doc.URegisters(0) = 100 + $addr
  }

  try {
    $doc.Save($mbs)
  }
  catch {
    Write-Warning "COM Save not available for addr=$addr — save manually: $mbs"
  }

  if ($ShowWindows) {
    $doc.ShowWindow() | Out-Null
  }

  $docs += [pscustomobject]@{ Addr = $addr; Name = $name; Qty = $qty; File = $mbs; Doc = $doc }
  Write-Output ("addr={0,2} qty={1,2} first_reg={2} -> {3}" -f $addr, $qty, $s.first_reg, $mbs)
}

$connOk = $false
if ($OpenConnection) {
  # Empirically Mbslave may return 0 even when the serial port is usable.
  # Treat the call as best-effort; KeepOpen still holds the COM objects.
  $st = $app.OpenConnection()
  Write-Output "OpenConnection status=$st (holding docs regardless)"
  $connOk = $true
}

$msw = Join-Path $OutDir "velaguard.msw"
Write-Output ""
Write-Output "Next steps:"
Write-Output "  1. Close MThings (release COM$ComPort)"
Write-Output "  2. Modbus Slave: Connection -> Serial COM$ComPort, $Baud 8N1, Parity=None"
Write-Output "  3. Open all: .\scripts\open_velaguard_mbslaves.ps1   (or double-click open_velaguard_mbslaves.cmd)"
Write-Output "  4. File -> Save Workspace -> $msw  (one-time, reuse later)"
Write-Output "  5. Connection -> Connect"
Write-Output ""
Write-Output "Board: vgdiscover scan -a 1-32   or   vgscan -a 1-32"

$holdSec = $KeepOpenSeconds
if ($KeepOpen -and $holdSec -le 0) { $holdSec = 7200 }

if ($holdSec -gt 0) {
  if (-not $connOk -and $OpenConnection) {
    throw "Cannot KeepOpen: serial connection failed."
  }
  Write-Output ""
  Write-Output "Holding $($docs.Count) slave docs for ${holdSec}s (Ctrl+C to stop)..."
  try {
    Start-Sleep -Seconds $holdSec
  }
  finally {
    try { $app.CloseConnection() | Out-Null } catch {}
  }
}
elseif (-not $OpenConnection) {
  Write-Output ""
  Write-Output "Tip: add -OpenConnection -KeepOpenSeconds 3600 after COM port is free."
}
