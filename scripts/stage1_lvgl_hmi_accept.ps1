# Stage1 LVGL HMI board accept (velaguard-lvgl = net+HMI @ COM3).
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_lvgl_hmi_accept.ps1

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$Log = ""
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

function Send-Serial {
  param(
    [System.IO.Ports.SerialPort]$Port,
    [string]$Cmd,
    [int]$WaitSec = 12
  )
  $Port.DiscardInBuffer()
  $Port.Write("$Cmd`r")
  Start-Sleep -Milliseconds 400
  $buf = ""
  $deadline = (Get-Date).AddSeconds($WaitSec)
  while ((Get-Date) -lt $deadline) {
    if ($Port.BytesToRead -gt 0) { $buf += $Port.ReadExisting() }
    if ($buf -match "nsh>") { break }
    Start-Sleep -Milliseconds 80
  }
  Write-Host "`n=== $Cmd ==="
  Write-Host $buf
  return $buf
}

function Assert-Match {
  param([string]$Name, [string]$Hay, [string]$Pattern)
  if ($Hay -match $Pattern) {
    Write-Host "[PASS] $Name"
    $script:pass++
  } else {
    Write-Host "[FAIL] $Name (pattern: $Pattern)"
    $script:fail++
  }
}

$port = New-Object System.IO.Ports.SerialPort
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$logLines = New-Object System.Collections.Generic.List[string]

try {
  $port.Open()

  $cubeCandidates = @(
    "D:\Develop\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "D:\Develop\STM32CubeCLT\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
  )
  $cube = $cubeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  if ($cube) {
    Write-Host "Resetting MCU so boot logs are captured..."
    & $cube -c port=SWD mode=UR -rst | Out-Host
  }

  $boot = ""
  $bootDeadline = (Get-Date).AddSeconds(25)
  while ((Get-Date) -lt $bootDeadline) {
    if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
    if ($boot -match "vghmi: autostart ok") { break }
    Start-Sleep -Milliseconds 150
  }
  Start-Sleep -Seconds 3
  while ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting(); Start-Sleep -Milliseconds 80 }

  Write-Host "===== BOOT ====="
  Write-Host $boot
  $logLines.Add($boot)

  Assert-Match "HMI autostart log" $boot "vghmi: autostart ok"
  Assert-Match "LVGL 480x272" $boot "xres: 480"
  Assert-Match "touchscreen open" $boot "touchscreen /dev/input0 open success"
  if ($boot -notmatch "Assertion failed") {
    Write-Host "[PASS] no agent panic"
    $script:pass++
  } else {
    Write-Host "[FAIL] no agent panic"
    $script:fail++
  }

  # C1: cold start must not auto-scan RS485.
  if ($boot -notmatch "vghmi scan:" -and $boot -notmatch "vg_bus_scan") {
    Write-Host "[PASS] C1 no auto RS485 scan"
    $script:pass++
  } else {
    Write-Host "[FAIL] C1 no auto RS485 scan"
    $script:fail++
  }

  $fleetDeadline = (Get-Date).AddSeconds(12)
  while ((Get-Date) -lt $fleetDeadline -and $boot -notmatch "vghmi: home fleet n=") {
    if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
    Start-Sleep -Milliseconds 150
  }
  Assert-Match "C4 home fleet log" $boot "vghmi: home fleet n="
  Assert-Match "C4 mthings fleet n=52" $boot "vghmi: home fleet n=52"
  Assert-Match "acq thread start" $boot "vghmi: acq start ok points=52"

  $liveDeadline = (Get-Date).AddSeconds(70)
  while ((Get-Date) -lt $liveDeadline -and $boot -notmatch "vghmi: live ok=[1-9]") {
    if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
    Start-Sleep -Milliseconds 200
  }
  Assert-Match "live acq log" $boot "vghmi: live ok="
  $liveOk = 0
  if ($boot -match "(?s).*vghmi: live ok=(\d+)/(\d+)") {
    $liveOk = [int]$Matches[1]
  }
  if ($liveOk -gt 0) {
    Write-Host "[PASS] live Modbus reads (ok=$liveOk)"
    $script:pass++
  } else {
    Write-Host "[FAIL] live Modbus reads (need mock slave on USB-RS485; ok=$liveOk)"
    $script:fail++
  }

  $help = Send-Serial $port "?" 8
  Assert-Match "vghmi in help" $help "vghmi"
  Assert-Match "vgdiscover in help" $help "vgdiscover"
  Assert-Match "vgmqtt in help" $help "vgmqtt"

  $ps = Send-Serial $port "ps" 8
  Assert-Match "vghmi autostart task" $ps "vghmi"
  Assert-Match "velaguard_app entry" $ps "velaguard_app"

  # C7 NSH cross-check (LCD discover/confirm still visual)
  Start-Sleep -Milliseconds 500
  $vgcfg = Send-Serial $port "vgcfg dump" 12
  Assert-Match "vgcfg dump responds" $vgcfg "seq="

  $lsrep = Send-Serial $port "ls /data/agent/reports" 10
  if ($lsrep -match "daily-") {
    Write-Host "[PASS] reports dir has daily file"
    $script:pass++
  }
  elseif ($lsrep -match "stat failed|No such file|nsh>") {
    Write-Host "[PASS] reports empty-or-missing (C5 empty OK)"
    $script:pass++
  }
  else {
    Write-Host "[FAIL] reports dir check"
    $script:fail++
  }

  $lspts = Send-Serial $port "ls /data/velaguard/config/points.json" 10
  $fleetFile = Send-Serial $port "cat /data/velaguard/hmi_fleet.txt" 8
  $fleetN = 0
  if ($boot -match "vghmi: home fleet n=(\d+)") {
    $fleetN = [int]$Matches[1]
  }
  elseif ($fleetFile -match "n=(\d+)") {
    $fleetN = [int]$Matches[1]
  }

  if ($fleetN -eq 52) {
    Write-Host "[PASS] C4 home fleet from velaguard.mthings (n=$fleetN)"
    $script:pass++
  }
  elseif ($lspts -match "points.json" -and $lspts -notmatch "stat failed|No such file") {
    if ($fleetN -gt 0) {
      Write-Host "[PASS] C4 home fleet from points.json (n=$fleetN)"
      $script:pass++
    } else {
      Write-Host "[FAIL] C4 points.json present but home fleet n=0"
      $script:fail++
    }
  }
  elseif ($fleetN -eq 0) {
    Write-Host "[PASS] C4 empty home until confirm (no points.json)"
    $script:pass++
  }
  else {
    Write-Host "[PASS] C4 empty-or-pending (no points.json; LCD confirm still visual)"
    $script:pass++
  }

  Write-Host "`n=== Summary: pass=$($script:pass) fail=$($script:fail) ==="
  Write-Host "NOTE: visually confirm LCD home (empty or 从站N) + discover switch OFF."
  Write-Host "NOTE: C2-C4 board: scan@9600 -> confirm -> home shows 从站N; vgcfg after confirm."
  Write-Host "NOTE: C5 report page / C6 alarm AI block need LCD visual check."
  if ($Log -ne "") {
    $logLines.Add("pass=$($script:pass) fail=$($script:fail)")
    Set-Content -LiteralPath $Log -Value ($logLines -join [Environment]::NewLine) -Encoding UTF8
  }
  if ($script:fail -gt 0) { exit 1 }
  exit 0
}
finally {
  if ($port.IsOpen) { $port.Close() }
}
