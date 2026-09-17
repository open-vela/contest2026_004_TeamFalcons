# Resident watchdog: probe the board on a cadence, capture the crash scene,
# and reset it over SWD so a long session does not die with the target.
#
# Two runaway shapes must both be caught:
#   1. HARDFAULT / Assertion / panic - the board prints a dump
#   2. silent wedge - not one byte comes back, even after CR (no clue at all,
#      and the one that keeps biting)
#
# Reset is SWD-only.  A VCP DTR pulse does not move this target; see
# .debug/nsh_reset_only.ps1 and .debug/runaway-watch/NOTES.md.
#
# ASCII only: Windows PowerShell reads .ps1 in the ANSI codepage, so UTF-8
# comments decode into stray quotes and break the parser.
#
# Usage:
#   powershell.exe -ExecutionPolicy Bypass -File scripts/board_watch_recover.ps1
#   ... -NoRecover          observe only, do not reset
#   ... -MaxRecoveries 2    stop after two rescues

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [int]$ProbeSec = 20,             # liveness probe cadence
  [int]$ProbeTimeoutSec = 8,       # no nsh> within this after CR means wedged
  [int]$BootTimeoutSec = 60,
  [int]$MaxRecoveries = 0,         # 0 = unlimited
  [switch]$NoRecover,
  [string]$LogPath = "",
  [string]$CubeCli = ""
)

$ErrorActionPreference = "Stop"

if ($LogPath -eq "") {
  $root = Split-Path -Parent $PSScriptRoot
  $LogPath = Join-Path $root ".debug\board_watch_recover.log"
}
New-Item -ItemType Directory -Force -Path (Split-Path $LogPath) | Out-Null

function Write-Log {
  param([string]$Text, [switch]$Raw)
  if ($Raw) {
    [System.IO.File]::AppendAllText($LogPath, $Text)
  } else {
    $line = "[watch] {0:yyyy-MM-ddTHH:mm:ss.fffzzz} $Text" -f (Get-Date)
    [System.IO.File]::AppendAllText($LogPath, $line + "`r`n")
    Write-Host $line
  }
}

function Resolve-CubeCli {
  param([string]$Explicit)
  $candidates = @(
    $Explicit,
    $env:STM32_PROGRAMMER_CLI,
    "D:\Develop\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "D:\Develop\STM32CubeCLT\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    (Join-Path $env:ProgramFiles "STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe")
  )
  Get-ChildItem -Path "D:\Develop" -Filter "STM32CubeProgrammer" -Recurse -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { $candidates += (Join-Path $_.FullName "bin\STM32_Programmer_CLI.exe") }
  foreach ($c in $candidates) {
    if (-not [string]::IsNullOrWhiteSpace($c) -and (Test-Path -LiteralPath $c -PathType Leaf)) {
      return (Resolve-Path -LiteralPath $c).Path
    }
  }
  return $null
}

# Text the board emits on its way down.  Hit means runaway; no need to wait
# for the probe to time out.
$panicPattern = "HARDFAULT|Assertion failed|panic:|stack_dump:|dump_tasks:"

$cli = $null
if (-not $NoRecover) {
  $cli = Resolve-CubeCli $CubeCli
  if ($null -eq $cli) {
    Write-Log "STM32CubeProgrammer CLI not found; cannot auto-reset. Use -NoRecover to observe."
    exit 2
  }
}

$port = New-Object System.IO.Ports.SerialPort
$port.Encoding = [System.Text.Encoding]::UTF8
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 500
$port.DtrEnable = $false      # no pulse: opening the port must not reset it
$port.RtsEnable = $false
try {
  $port.Open()
} catch {
  Write-Log "cannot open $ComPort : $($_.Exception.Message). Close other serial monitors first."
  exit 2
}

Write-Log "start port=$ComPort probe=${ProbeSec}s noRecover=$NoRecover log=$LogPath"

$recoveries = 0
$panics = 0
$wedges = 0

function Read-Into {
  # Drain whatever is on the wire into the log; return this round's text.
  $acc = ""
  while ($port.BytesToRead -gt 0) {
    $chunk = $port.ReadExisting()
    $acc += $chunk
    Write-Log $chunk -Raw
    if ($port.BytesToRead -eq 0) { Start-Sleep -Milliseconds 30 }
  }
  return $acc
}

function Wait-Prompt {
  param([int]$Seconds)
  $buf = ""
  $deadline = (Get-Date).AddSeconds($Seconds)
  while ((Get-Date) -lt $deadline) {
    $buf += Read-Into
    if ($buf -match "nsh>") { return @{ ok = $true; text = $buf } }
    Start-Sleep -Milliseconds 120
  }
  return @{ ok = $false; text = $buf }
}

function Recover {
  param([string]$Reason)

  # Script scope: a bare $recoveries++ here would only touch a local copy,
  # so every rescue would report #1 and MaxRecoveries would never be hit.
  $script:recoveries++
  Write-Log "RECOVER #$script:recoveries reason=$Reason"

  # Keep whatever is still buffered before the reset wipes the scene.
  [void](Read-Into)

  try {
    & $cli -c port=SWD mode=UR -rst | Out-Null
  } catch {
    Write-Log "SWD reset failed: $($_.Exception.Message)"
    return
  }

  $boot = Wait-Prompt -Seconds $BootTimeoutSec
  Write-Log $boot.text -Raw

  if (-not $boot.ok) {
    Write-Log "RECOVER #$script:recoveries no nsh> after reset; board may not have come up"
    return
  }

  $mounted = if ($boot.text -match "eMMC mounted at /mnt/emmc") { "yes" } else { "NO" }
  Write-Log "RECOVER #$script:recoveries done, booted, eMMC mounted=$mounted"

  # A board with no eMMC still boots and still answers; /data is the part
  # that is gone.  Say so explicitly instead of looking healthy.
  if ($mounted -eq "NO") {
    $port.DiscardInBuffer()
    $port.Write("vgcfg probe`r")
    $r = Wait-Prompt -Seconds 10
    Write-Log $r.text -Raw
  }
}

$nextProbe = (Get-Date).AddSeconds($ProbeSec)

while ($true) {
  $chunk = Read-Into
  if ($chunk -match $panicPattern) {
    $panics++
    Write-Log "PANIC detected (#$panics)"
    if (-not $NoRecover) {
      if ($MaxRecoveries -gt 0 -and $script:recoveries -ge $MaxRecoveries) {
        Write-Log "MaxRecoveries=$MaxRecoveries reached, stopping."
        break
      }
      Recover "panic"
      $nextProbe = (Get-Date).AddSeconds($ProbeSec)
      continue
    }
  }

  if ((Get-Date) -ge $nextProbe) {
    $nextProbe = (Get-Date).AddSeconds($ProbeSec)

    try { $port.Write("`r") } catch { }

    $res = Wait-Prompt -Seconds $ProbeTimeoutSec
    if (-not $res.ok) {
      $wedges++
      Write-Log "WEDGE detected (#$wedges): no nsh> within $ProbeTimeoutSec s of CR"
      if (-not $NoRecover) {
        if ($MaxRecoveries -gt 0 -and $script:recoveries -ge $MaxRecoveries) {
          Write-Log "MaxRecoveries=$MaxRecoveries reached, stopping."
          break
        }
        Recover "wedge"
      }
      $nextProbe = (Get-Date).AddSeconds($ProbeSec)
    }
  }

  Start-Sleep -Milliseconds 150
}

$port.Close()
Write-Log "stop panics=$panics wedges=$wedges recoveries=$recoveries"
exit 0
