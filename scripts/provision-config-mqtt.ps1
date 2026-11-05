# Provision the dedicated candidate MQTT client without exposing plaintext
# credentials on the serial console.
param(
  [Parameter(Mandatory = $true)]
  [string]$CredentialsJson,
  [string]$ComPort = "COM3",
  [string]$ContestRoot = "",
  [string]$ReadyFile = "",
  [int]$OnlineWaitSec = 20
)

$ErrorActionPreference = "Stop"
if ($ContestRoot -eq "") {
  $ContestRoot = Split-Path -Parent $PSScriptRoot
}
if (-not (Test-Path -LiteralPath $CredentialsJson -PathType Leaf)) {
  throw "Credential JSON was not found: $CredentialsJson"
}
$CredentialsJson = (Resolve-Path -LiteralPath $CredentialsJson).ProviderPath

$plainText = Get-Content -LiteralPath $CredentialsJson -Raw
$plainBytes = [System.Text.Encoding]::UTF8.GetBytes($plainText)
if ($plainBytes.Length -eq 0 -or $plainBytes.Length -gt 512) {
  throw "Credential JSON must contain 1..512 bytes"
}

$debugDir = Join-Path $ContestRoot ".debug"
New-Item -ItemType Directory -Path $debugDir -Force | Out-Null
$blobPath = Join-Path $debugDir "config_mqtt_secrets.v1"

$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 12000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 800

function Send-Cmd {
  param(
    [System.IO.Ports.SerialPort]$Serial,
    [string]$Command,
    [int]$WaitSec = 12
  )

  $Serial.DiscardInBuffer()
  $Serial.Write("$Command`r")
  Start-Sleep -Milliseconds 350
  $output = ""
  $deadline = (Get-Date).AddSeconds($WaitSec)
  while ((Get-Date) -lt $deadline) {
    if ($Serial.BytesToRead -gt 0) {
      $output += $Serial.ReadExisting()
    }
    if ($output -match "nsh>") {
      break
    }
    Start-Sleep -Milliseconds 80
  }
  return $output
}

$boot = ""
$bootDeadline = (Get-Date).AddSeconds(25)
while ((Get-Date) -lt $bootDeadline) {
  if ($port.BytesToRead -gt 0) {
    $boot += $port.ReadExisting()
  }
  if ($boot -match "nsh>") {
    break
  }
  Start-Sleep -Milliseconds 150
}
if ($boot -notmatch "nsh>") {
  $boot += Send-Cmd $port "" 8
}
if ($boot -notmatch "nsh>") {
  $port.Close()
  throw "No NSH prompt on $ComPort"
}

$uidOutput = Send-Cmd $port "vgprovision uid" 8
if ($uidOutput -notmatch "uid=([0-9a-fA-F]{24})") {
  $port.Close()
  throw "vgprovision uid did not return a device-bound identity"
}
$uid = $Matches[1]

& python (Join-Path $ContestRoot "scripts\vg-provision-pack.py") `
  $uid $CredentialsJson --output $blobPath
if ($LASTEXITCODE -ne 0) {
  $port.Close()
  throw "Credential sealing failed"
}

$bytes = [byte[]](Get-Content -LiteralPath $blobPath -Encoding Byte -ReadCount 0)
if ($bytes.Length -lt 60 -or $bytes.Length -gt 632) {
  $port.Close()
  throw "Sealed credential size is invalid"
}
$hex = -join ($bytes | ForEach-Object { "{0:x2}" -f $_ })

$wipeOutput = Send-Cmd $port "vgconfigmqtt wipe" 8
if ($wipeOutput -notmatch "vgconfigmqtt: wiped") {
  $port.Close()
  throw "Existing MQTT configuration could not be cleared; reboot the board and retry"
}

$chunkCharacters = 32
for ($offset = 0; $offset -lt $hex.Length; $offset += $chunkCharacters) {
  $count = [Math]::Min($chunkCharacters, $hex.Length - $offset)
  $chunk = $hex.Substring($offset, $count)
  $putOutput = Send-Cmd $port "vgconfigmqtt put $chunk" 6
  if ($putOutput -match "put failed") {
    $port.Close()
    throw "Sealed credential chunk was rejected"
  }
}

$commitOutput = Send-Cmd $port "vgconfigmqtt commit" 15
if ($commitOutput -notmatch "credentials stored; reset required") {
  $port.Close()
  throw "MQTT credential commit failed"
}
$port.Close()

$resetScript = Join-Path $ContestRoot ".debug\nsh_reset_only.ps1"
if (-not (Test-Path -LiteralPath $resetScript -PathType Leaf)) {
  throw "Board reset helper was not found: $resetScript"
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $resetScript
if ($LASTEXITCODE -ne 0) {
  throw "Board reset failed"
}
Start-Sleep -Seconds 8
if ($ReadyFile -ne "") {
  Set-Content -LiteralPath $ReadyFile -Value "ready" -Encoding ascii
}

$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 12000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 800

$online = $false
$deadline = (Get-Date).AddSeconds($OnlineWaitSec)
while ((Get-Date) -lt $deadline) {
  $statusOutput = Send-Cmd $port "vgconfigmqtt status" 8
  if ($statusOutput -match "initialized=1 running=1 online=1") {
    $online = $true
    break
  }
  Start-Sleep -Seconds 1
}

$port.Close()
if (-not $online) {
  throw "Credential commit succeeded, but the MQTT service did not reach online=1"
}
Write-Host "[config-mqtt] sealed credential committed; service initialized, running and online"
