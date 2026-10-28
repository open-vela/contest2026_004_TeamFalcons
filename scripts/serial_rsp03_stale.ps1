# RSP-03 负例：过期样本必须回到完整流程。
#
# 造过期样本的办法：让 vgdiscover 扫描占住总线锁。采集线程拿不到锁就整轮
# 跳过，快照的 tick 停在最后一次成功采集，age_ms 越过 5 秒的过期阈值，
# 工具输出带上 stale=1。扫描不改变已确认点表，也不需要拔线。
#
# 中文问题写在本文件常量里：PowerShell 5.1 读无 BOM 的 .ps1 会按 ANSI 解析，
# 命令行传中文也会在 WSL 到 Windows 的转换中损坏。
#
# 用法：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/serial_rsp03_stale.ps1 `
#     -Log .\docs\maintenance-evidence\2026-10-20\rsp03-stale.log
param(
  [string]$ComPort = "COM3",
  [int]$ReadSec = 90,
  [string]$Log = ""
)
$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$question = "告诉我UPS负载的值"

$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep 1

$captured = New-Object System.Collections.Generic.List[string]

function Drain([int]$sec, [string]$want) {
  $dl = (Get-Date).AddSeconds($sec)
  $acc = ""
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) {
      $chunk = $port.ReadExisting()
      Write-Host -NoNewline $chunk
      $captured.Add($chunk)
      $acc += $chunk
      if ($want -ne "" -and $acc -match $want) { break }
      if ($want -ne "") { $acc = $acc.Substring([Math]::Max(0, $acc.Length - 400)) }
    }
    Start-Sleep -Milliseconds 100
  }
}

function Send([string]$c, [int]$sec, [string]$want) {
  $port.DiscardInBuffer()
  $port.Write("$c`r")
  Start-Sleep -Milliseconds 350
  Write-Host "`n=== $c ==="
  Drain $sec $want
}

try {
  Drain 4 "" | Out-Null

  # 扫描在 NSH 里后台跑，占住总线锁约两分钟。
  $port.DiscardInBuffer()
  $port.Write("vgdiscover scan -a 1-32 &`r")
  Start-Sleep -Seconds 1
  Drain 3 "" | Out-Null

  # 等快照停止更新，越过 5 秒阈值后再问。
  Start-Sleep -Seconds 9

  Send "ai_agent --daemon &" 20 "nsh>" | Out-Null
  Start-Sleep 2
  Send "ai_agent" 25 "vela>" | Out-Null
  Send "point_shortcut on" 8 "point shortcut: on" | Out-Null
  Send "session_clear console" 6 "Session (cleared|not found)" | Out-Null
  Send "cache_clear" 6 "cache cleared" | Out-Null

  # 直接看工具输出，确认这轮确实是过期样本而不是读失败。
  $captured.Add("/// RSP03 BEGIN stale on ///")
  Write-Host "`n--- stale on ---"
  $port.DiscardInBuffer()
  $port.Write("ask $question`r")
  Start-Sleep -Milliseconds 400
  Drain 8 "" | Out-Null
  Drain $ReadSec "END status=" | Out-Null
  Drain 3 "" | Out-Null
  $captured.Add("/// RSP03 END stale on ///")

  Send "point_shortcut off" 6 "point shortcut: off" | Out-Null
  Send "quit" 5 "" | Out-Null

  # 等扫描结束后确认采集恢复、快照重新变新鲜。
  Start-Sleep -Seconds 5
  Send "vgpoint get" 30 "" | Out-Null
}
finally {
  if ($port.IsOpen) { $port.Close() }
}

$text = ($captured -join "")
if ($Log -ne "") {
  $text | Set-Content -LiteralPath $Log -Encoding UTF8
  Write-Host "`n[saved] $Log"
}

Write-Host "`n===== RSP-03 stale ====="
foreach ($line in ($text -split "`r?`n")) {
  if ($line -match "Point shortcut: (\w+)") { Write-Host ("verdict: " + $Matches[1]) }
  if ($line -match "END status=(\w+).*elapsed_ms=(\d+)") {
    Write-Host ("round: status={0} elapsed={1}ms" -f $Matches[1], $Matches[2])
  }
  if ($line -match "vgquery: point id=(\w+) .* stale=1") {
    Write-Host ("tool: stale sample for " + $Matches[1])
  }
}
