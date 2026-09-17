# Stage1 ai_agent 板端冒烟（COM3）。先关其它 COM3 占用。
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_agent_accept.ps1

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$Log = "",
  [string]$MimoToken = $env:MIMO_API_KEY,
  [switch]$KeepPortOpen
)

$ErrorActionPreference = "Stop"
$script:pass = 0
$script:fail = 0

function Wait-Prompt {
  param([string]$Buf, [int]$TimeoutSec = 12)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($Buf -match "nsh>" -or $Buf -match "vela>") { return $true }
    Start-Sleep -Milliseconds 120
  }
  return $false
}

function Send-Serial {
  param(
    [System.IO.Ports.SerialPort]$Port,
    [string]$Cmd,
    [int]$WaitSec = 15,
    [ValidateSet("any", "nsh", "vela")]
    [string]$WantPrompt = "any"
  )
  $Port.DiscardInBuffer()
  $Port.Write("$Cmd`r")
  Start-Sleep -Milliseconds 400
  $buf = ""
  $deadline = (Get-Date).AddSeconds($WaitSec)
  while ((Get-Date) -lt $deadline) {
    if ($Port.BytesToRead -gt 0) { $buf += $Port.ReadExisting() }
    $ok = switch ($WantPrompt) {
      "nsh"  { $buf -match "nsh>" }
      "vela" { $buf -match "vela>" }
      default { ($buf -match "nsh>") -or ($buf -match "vela>") }
    }
    if ($ok) { break }
    Start-Sleep -Milliseconds 120
  }
  $block = "`n=== $Cmd ===`n$buf"
  Write-Output $block
  if ($Log -ne "" -and $script:logLines -ne $null) {
    $script:logLines.Add($block) | Out-Null
  }
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
$port.DtrEnable = $true
$port.RtsEnable = $true
$logLines = New-Object System.Collections.Generic.List[string]

try {
  $port.Open()
  $boot = ""
  $bootDeadline = (Get-Date).AddSeconds(25)
  while ((Get-Date) -lt $bootDeadline) {
    if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
    if ($boot -match "nsh>") { break }
    Start-Sleep -Milliseconds 150
  }
  Start-Sleep -Seconds 1
  while ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting(); Start-Sleep -Milliseconds 80 }
  if ($boot -notmatch "nsh>") { $boot += (Send-Serial $port "" 8).Text }

  $r = Send-Serial $port "?" 8
  Assert-Match "ai_agent in help" $r.Text "ai_agent"

  $r = Send-Serial $port "ls /data/agent/skills" 10
  Assert-Match "modbus_query skill" $r.Text "modbus_query"

  $prov = Send-Serial $port "vgprovision status" 12
  $provOk = ($prov.Text -match "vgprovision: OK")

  # Start daemon in background, then attach CLI (full foreground init can exceed 45s).
  $r = Send-Serial $port "ai_agent --daemon &" 90 -WantPrompt "nsh"
  Assert-Match "daemon bg returns nsh" $r.Text "nsh>"
  Start-Sleep -Seconds 3
  $r = Send-Serial $port "ai_agent" 45 -WantPrompt "vela"
  if ($r.Text -notmatch "vela>") {
    Write-Output "[DEBUG] ai_agent attach output (tail):"
    if ($r.Text.Length -gt 1500) { Write-Output $r.Text.Substring($r.Text.Length - 1500) } else { Write-Output $r.Text }
  }
  Assert-Match "vela prompt" $r.Text "vela>"

  Start-Sleep -Seconds 15
  $r = Send-Serial $port "net_test" 90 -WantPrompt "vela"
  Assert-Match "net_test TLS ok" $r.Text "Handshake OK"
  Assert-Match "net_test HTTP ok" $r.Text "HTTP Status: 200"

  $r = Send-Serial $port "config_show" 15 -WantPrompt "vela"
  if ($provOk -or $r.Text -match "api_key|mimo|token-plan") {
    $r = Send-Serial $port "ask read slave 1 temp humidity via run_shell vgmodbus -a 1 -r 0 -c 2 -n 1 -i 0" 120 -WantPrompt "vela"
    Assert-Match "ask response" $r.Text "\[Agent\]|temp|humidity|1000|800"
  } elseif ($MimoToken -and $MimoToken.Length -gt 8) {
    $setCmd = "set_llm https://token-plan-cn.xiaomimimo.com/v1 mimo-v2.5 $MimoToken"
    $r = Send-Serial $port $setCmd 15
    $r = Send-Serial $port "ask read slave 1 temp via run_shell vgmodbus -a 1 -r 0 -c 2 -n 1 -i 0" 90 -WantPrompt "vela"
    Assert-Match "ask response" $r.Text "\[Agent\]|temp|1000|800"
  } else {
    Write-Output "[SKIP] ask/LLM (eMMC provision missing; run scripts/provision-llm-from-secrets.sh)"
    $r = Send-Serial $port "quit" 8
  }

  for ($i = 0; $i -lt 3; $i++) {
    if ($r.Text -match "nsh>") { break }
    $r = Send-Serial $port "quit" 12 -WantPrompt "nsh"
  }
  if ($r.Text -notmatch "nsh>") {
    throw "still not at nsh> after ai_agent exit"
  }

  $r = Send-Serial $port "echo agent-persist-test > /data/agent/memory/persist.txt" 10
  $r = Send-Serial $port "cat /data/agent/memory/persist.txt" 10
  Assert-Match "persist write" $r.Text "agent-persist-test"

  Write-Output "`n[stage1_agent_accept] pass=$($script:pass) fail=$($script:fail)"
  if ($Log -ne "") {
    $script:logLines.Add("[stage1_agent_accept] pass=$($script:pass) fail=$($script:fail)") | Out-Null
    $script:logLines | Set-Content -LiteralPath $Log -Encoding UTF8
  }
  if ($script:fail -gt 0) { exit 1 }
}
catch {
  Write-Error "stage1_agent_accept failed: $($_.Exception.Message)"
  exit 1
}
finally {
  if (-not $KeepPortOpen -and $port.IsOpen) { $port.Close() }
}
