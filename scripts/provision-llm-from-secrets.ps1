# Encrypt MiMo secrets from secrets/ and write to board eMMC via COM3.
# Usage: powershell.exe -ExecutionPolicy Bypass -File scripts/provision-llm-from-secrets.ps1

param(
  [string]$ComPort = "COM3",
  [string]$ContestRoot = ""
)

$ErrorActionPreference = "Stop"
if ($ContestRoot -eq "") { $ContestRoot = Split-Path -Parent $PSScriptRoot }

$keyFile = Join-Path $ContestRoot "secrets\agent_llm.key"
$epFile  = Join-Path $ContestRoot "secrets\agent_llm.endpoint"
$plain   = Join-Path $ContestRoot ".debug\provision-plain.json"
$blob    = Join-Path $ContestRoot ".debug\llm_secrets.v1"

if (-not (Test-Path $keyFile) -or -not (Test-Path $epFile)) {
  throw "Missing secrets/agent_llm.key or secrets/agent_llm.endpoint"
}

$key = (Get-Content -LiteralPath $keyFile -Raw).Trim()
$endpoint = (Get-Content -LiteralPath $epFile -Raw).Trim()

$llmHost = $endpoint
$path = "/v1/chat/completions"
$portNum = "443"
if ($endpoint -match '^https?://') {
  if ($endpoint.StartsWith("https://")) { $rest = $endpoint.Substring(8) } else { $rest = $endpoint.Substring(7); $portNum = "80" }
  if ($rest -match '/') {
    $hp = $rest.Split('/', 2)
    $llmHost = $hp[0]
    $path = '/' + $hp[1]
    if ($path -eq '/v1' -or $path -eq '/v1/') { $path = '/v1/chat/completions' }
    elseif ($path -notmatch 'chat/completions') { $path = $path.TrimEnd('/') + '/chat/completions' }
  } else { $llmHost = $rest }
}

$obj = @{ host = $llmHost; path = $path; port = $portNum; model = 'mimo-v2.5'; api_key = $key }
$obj | ConvertTo-Json -Compress | Out-File -LiteralPath $plain -Encoding ascii -NoNewline

$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 15000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep -Milliseconds 800

$boot = ""
$deadline = (Get-Date).AddSeconds(25)
while ((Get-Date) -lt $deadline) {
  if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
  if ($boot -match 'nsh>' -or $boot -match 'vghmi: autostart ok') { break }
  Start-Sleep -Milliseconds 150
}
Start-Sleep -Seconds 1
while ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting(); Start-Sleep -Milliseconds 80 }

function Send-Cmd {
  param([System.IO.Ports.SerialPort]$Port, [string]$Cmd, [int]$WaitSec = 12)
  $Port.DiscardInBuffer()
  $Port.Write("$Cmd`r")
  Start-Sleep -Milliseconds 400
  $buf = ""
  $dl = (Get-Date).AddSeconds($WaitSec)
  while ((Get-Date) -lt $dl) {
    if ($Port.BytesToRead -gt 0) { $buf += $Port.ReadExisting() }
    if ($buf -match 'nsh>') { break }
    Start-Sleep -Milliseconds 80
  }
  return $buf
}

if ($boot -notmatch 'nsh>') { $boot += Send-Cmd $port "" 8 }
if ($boot -notmatch 'nsh>') { $port.Close(); throw "no nsh> prompt" }

$uidOut = Send-Cmd $port "vgprovision uid" 8
if ($uidOut -notmatch 'uid=([0-9a-fA-F]{24})') { $port.Close(); throw "vgprovision uid failed: $uidOut" }
$uid = $Matches[1]
Write-Host "[provision] uid=$uid"

$wslRoot = "/home/hello19y/openvela/contest2026_004_TeamFalcons"
& wsl.exe -d Debian bash -lc "python3 '$wslRoot/scripts/vg-provision-pack.py' '$uid' '$wslRoot/.debug/provision-plain.json' > '$wslRoot/.debug/llm_secrets.v1'"
if ($LASTEXITCODE -ne 0) { $port.Close(); throw "vg-provision-pack failed" }
$bytes = [System.IO.File]::ReadAllBytes($blob)
$hex = -join ($bytes | ForEach-Object { '{0:x2}' -f $_ })
Write-Host "[provision] blob bytes=$($bytes.Length)"

Send-Cmd $port "vgprovision wipe" 8 | Out-Null
for ($i = 0; $i -lt $hex.Length; $i += 32) {
  $chunk = $hex.Substring($i, [Math]::Min(32, $hex.Length - $i))
  Send-Cmd $port "vgprovision put $chunk" 6 | Out-Null
}
$out = Send-Cmd $port "vgprovision commit" 10
Write-Host $out
$st = Send-Cmd $port "vgprovision status" 8
Write-Host $st
if ($st -notmatch 'vgprovision: OK') { $port.Close(); throw "provision status failed" }

Write-Host "[provision] rebooting to apply..."
Send-Cmd $port "reboot" 10 | Out-Null
$port.Close()
Start-Sleep -Seconds 22
Write-Host "[provision] OK"
