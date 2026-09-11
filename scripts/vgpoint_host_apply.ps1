# Host serial: batch-write VelaGuard point table via NSH vgpoint (COM3).
# Flow: add/set -> test -> wait for Enter -> apply --confirm -> list
#   powershell.exe -ExecutionPolicy Bypass -File scripts/vgpoint_host_apply.ps1
#   powershell.exe -ExecutionPolicy Bypass -File scripts/vgpoint_host_apply.ps1 -PointsFile scripts/vgpoint_demo_points.json

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$PointsFile = ""
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

if ($PointsFile -eq "") {
  $PointsFile = Join-Path $PSScriptRoot "vgpoint_demo_points.json"
}

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
  Start-Sleep -Milliseconds 400
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

function Utf8Len {
  param([string]$Text)
  return [System.Text.Encoding]::UTF8.GetByteCount($Text)
}

function Build-AddCmd {
  param($pt)
  $id = [string]$pt.id
  if ([string]::IsNullOrWhiteSpace($id)) {
    throw "point missing id"
  }
  $addr = [int]$pt.addr
  $reg = [int]$pt.reg
  $cmd = "vgpoint add -i $id -a $addr -r $reg"
  if ($null -ne $pt.fc) { $cmd += " -f $([int]$pt.fc)" }
  if ($null -ne $pt.qty) { $cmd += " -q $([int]$pt.qty)" }
  if ($null -ne $pt.dtype -and "$($pt.dtype)" -ne "") { $cmd += " -d $($pt.dtype)" }
  if ($null -ne $pt.scale) { $cmd += " -s $($pt.scale)" }
  if ($null -ne $pt.unit -and "$($pt.unit)" -ne "") { $cmd += " -u $($pt.unit)" }
  if ($null -ne $pt.cmp -and "$($pt.cmp)" -ne "") { $cmd += " -k $($pt.cmp)" }
  if ($null -ne $pt.warn) { $cmd += " -w $($pt.warn)" }
  if ($null -ne $pt.crit) { $cmd += " -C $($pt.crit)" }
  if ($null -ne $pt.fail_n) { $cmd += " -n $([int]$pt.fail_n)" }
  $name = [string]$pt.name
  $nameCmd = $null
  if ($name -ne "") {
    $withName = "$cmd -N $name"
    if ((Utf8Len $withName) -le 120) {
      $cmd = $withName
    } else {
      $nameCmd = "vgpoint set $id -N $name"
      if ((Utf8Len $nameCmd) -gt 120) {
        throw "name command longer than 120 bytes: $nameCmd"
      }
    }
  }
  if ((Utf8Len $cmd) -gt 120) {
    throw "command longer than 120 bytes: $cmd"
  }
  return @{ Add = $cmd; NameSet = $nameCmd; Id = $id }
}

if (-not (Test-Path $PointsFile)) {
  throw "points file not found: $PointsFile"
}

$json = Get-Content -Raw -Path $PointsFile | ConvertFrom-Json
$points = @()
if ($null -ne $json.points) { $points = @($json.points) }
elseif ($json -is [System.Array]) { $points = @($json) }
if ($points.Count -lt 1) {
  throw "no points in $PointsFile"
}

$port = New-Object System.IO.Ports.SerialPort
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 8000
$port.NewLine = "`n"
$port.Encoding = [System.Text.Encoding]::UTF8

try {
  $port.Open()
  Start-Sleep -Seconds 3
  $boot = ""
  $bootDeadline = (Get-Date).AddSeconds(20)
  while ((Get-Date) -lt $bootDeadline) {
    if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
    if ($boot -match "nsh>") { break }
    Start-Sleep -Milliseconds 200
  }
  while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 50 }

  $r = Send-Serial $port "?" 8
  Assert-Match "vgpoint in help" $r.Text "vgpoint"

  foreach ($pt in $points) {
    $built = Build-AddCmd $pt
    $add = $built.Add
    $id = $built.Id
    $r = Send-Serial $port $add 12
    if ($r.Text -match "code=dup_id") {
      $set = ($add -replace "^vgpoint add -i $id ", "vgpoint set $id ")
      $r = Send-Serial $port $set 12
      Assert-Match "set $id" $r.Text "vgpoint: OK cmd=set"
    } else {
      Assert-Match "add $id" $r.Text "vgpoint: OK cmd=add"
    }
    if ($null -ne $built.NameSet) {
      $r = Send-Serial $port $built.NameSet 12
      Assert-Match "set name $id" $r.Text "vgpoint: OK cmd=set"
    }
  }

  $r = Send-Serial $port "vgpoint test" 45
  Assert-Match "test READ or fail token" $r.Text "vgpoint: READ |code=test_fail|code=no_candidate|code=bus_busy"
  Write-Output $r.Text

  Write-Host ""
  Write-Host "Candidate test finished. Review READ lines above."
  Write-Host "Press Enter to send: vgpoint apply --confirm"
  [void](Read-Host)

  $r = Send-Serial $port "vgpoint apply --confirm" 15
  Assert-Match "apply confirm" $r.Text "vgpoint: OK cmd=apply table=committed"

  $r = Send-Serial $port "vgpoint list" 12
  Assert-Match "list committed" $r.Text "vgpoint: OK cmd=list table=committed"

  Write-Output "`n[vgpoint_host_apply] pass=$($script:pass) fail=$($script:fail)"
  if ($script:fail -gt 0) { exit 1 }
}
catch {
  Write-Error "vgpoint_host_apply failed: $($_.Exception.Message)"
  exit 1
}
finally {
  if ($port.IsOpen) { $port.Close() }
}
