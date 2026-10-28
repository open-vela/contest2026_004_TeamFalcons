param([string]$ComPort = "COM3", [int]$ReadSec = 60, [string]$Log = "")
$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true; $port.RtsEnable = $true
$port.Open(); Start-Sleep 1
$cap = New-Object System.Collections.Generic.List[string]
function Drain([int]$sec, [string]$want) {
  $dl = (Get-Date).AddSeconds($sec); $acc = ""
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) {
      $c = $port.ReadExisting(); Write-Host -NoNewline $c; $cap.Add($c); $acc += $c
      if ($want -ne "" -and $acc -match $want) { break }
      if ($want -ne "") { $acc = $acc.Substring([Math]::Max(0,$acc.Length-400)) }
    }
    Start-Sleep -Milliseconds 100
  }
}
function Send([string]$c,[int]$sec,[string]$want) {
  $port.DiscardInBuffer(); $port.Write("$c`r"); Start-Sleep -Milliseconds 350
  Write-Host "`n=== $c ==="; Drain $sec $want
}
try {
  Drain 4 "" | Out-Null
  Send "ai_agent --daemon &" 20 "nsh>" | Out-Null
  Start-Sleep 2
  Send "ai_agent" 25 "vela>" | Out-Null
  Send "point_shortcut" 6 "point shortcut: (on|off)" | Out-Null
  Send "session_clear console" 6 "Session" | Out-Null
  Send "cache_clear" 6 "cache cleared" | Out-Null
  Write-Host "`n--- A: direct value question ---"
  $port.DiscardInBuffer(); $port.Write("ask 告诉我UPS负载的值`r")
  Start-Sleep -Milliseconds 400; Drain 6 "" | Out-Null
  Drain $ReadSec "END status=" | Out-Null; Drain 3 "" | Out-Null
  Send "session_clear console" 6 "Session" | Out-Null
  Send "cache_clear" 6 "cache cleared" | Out-Null
  Write-Host "`n--- B: explanation question ---"
  $port.DiscardInBuffer(); $port.Write("ask UPS负载为什么这么高`r")
  Start-Sleep -Milliseconds 400; Drain 6 "" | Out-Null
  Drain $ReadSec "END status=" | Out-Null; Drain 3 "" | Out-Null
  Send "quit" 5 "" | Out-Null
} finally { if ($port.IsOpen) { $port.Close() } }
if ($Log -ne "") { ($cap -join "") | Set-Content -LiteralPath $Log -Encoding UTF8; Write-Host "`n[saved] $Log" }
Write-Host "`n===== SUMMARY ====="
foreach ($l in (($cap -join "") -split "`r?`n")) {
  if ($l -match "point shortcut: (on|off)") { Write-Host ("default: " + $Matches[1]) }
  if ($l -match "Point shortcut: (\w+)") { Write-Host ("verdict: " + $Matches[1]) }
  if ($l -match "END status=(\w+).*elapsed_ms=(\d+)") { Write-Host ("round: {0} {1}ms" -f $Matches[1], $Matches[2]) }
  if ($l -match "http iter=") { $script:n = 1 + $script:n; }
}
Write-Host ("http lines total: " + $script:n)
