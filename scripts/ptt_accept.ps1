# USER1 按住说话事件验收（BVO-02）。
#
# 串口脚本不能模拟人手按键，所以分两段：
#   1. 脚本负责清计数、在指定窗口读统计、按关键字判定；
#   2. 人在窗口内按板上的 B1 USER。
#
# 用法（在 WSL 里执行）：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/ptt_accept.ps1 `
#     -Case normal -Count 30
#
# -Case 说明：
#   normal  普通按放：每次按住约 1 秒后松手。期望 begins/submits 各 +N。
#   bounce  快速连点：按几下短促点击，期望仍只增加 N 个会话。
#   long    长按：按住不放超过 15 秒再松手，期望 submits 中 timeouts +N，
#           且松手不再追加提交。
#   idle    只观察：不按键，确认空闲时 samples/events 不增长。
#
# 结果写 logs 并打印 PASS/FAIL 判定行。

param(
  [string]$ComPort = "COM3",
  [ValidateSet("normal", "bounce", "long", "idle")]
  [string]$Case = "normal",
  [int]$Count = 1,
  [int]$WindowSec = 12,
  [string]$Log = ""
)

$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$cap = New-Object System.Collections.Generic.List[string]
$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()

function Drain([int]$sec) {
  $dl = (Get-Date).AddSeconds($sec)
  $acc = ""
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) {
      $c = $port.ReadExisting()
      Write-Host -NoNewline $c
      $cap.Add($c)
      $acc += $c
    }
    Start-Sleep -Milliseconds 100
  }
  return $acc
}

function SendText([string]$cmd, [int]$sec) {
  $port.DiscardInBuffer()
  $port.Write("$cmd`r")
  Start-Sleep -Milliseconds 400
  $out = Drain $sec
  return $out
}

# 从 "vgptt: begins=A submits=B timeouts=C last_ms=D" 摘出计数
function ParseStats([string]$text) {
  $m = [regex]::Match($text, 'begins=(\d+) submits=(\d+) timeouts=(\d+) last_ms=(\d+)')
  if (-not $m.Success) { return $null }
  return [pscustomobject]@{
    Begins   = [int]$m.Groups[1].Value
    Submits  = [int]$m.Groups[2].Value
    Timeouts = [int]$m.Groups[3].Value
    LastMs   = [int]$m.Groups[4].Value
  }
}

function ParseDetail([string]$text) {
  $m = [regex]::Match($text, 'samples=(\d+) events=(\d+) timeouts_wake=(\d+) poll_err=(\d+) read_err=(\d+)')
  if (-not $m.Success) { return $null }
  return [pscustomobject]@{
    Samples   = [int]$m.Groups[1].Value
    Events    = [int]$m.Groups[2].Value
    WakeTo    = [int]$m.Groups[3].Value
    PollErr   = [int]$m.Groups[4].Value
    ReadErr   = [int]$m.Groups[5].Value
  }
}

try {
  Drain 4 | Out-Null

  $ver = SendText "uname -a" 3
  Write-Host "`n=== version ==="
  Write-Host $ver

  SendText "vgptt stop" 4 | Out-Null
  SendText "vgptt reset" 3 | Out-Null
  $started = SendText "vgptt start" 4
  Write-Host "`n=== start ==="
  Write-Host $started

  Write-Host ""
  Write-Host ("=" * 62)
  switch ($Case) {
    "normal" { Write-Host "现在请按 B1 USER $Count 次：每次按住约 1 秒后松手。" }
    "bounce" { Write-Host "现在请快速连点 B1 USER $Count 下，间隔尽量短。" }
    "long"   { Write-Host "现在请按住 B1 USER 不放，超过 15 秒后再松手，共 $Count 次。" }
    "idle"   { Write-Host "现在请不要碰按键，保持静止。" }
  }
  Write-Host "窗口 ${WindowSec} 秒。"
  Write-Host ("=" * 62)

  # 之前的版本直接倒计时，而按键的人看不到窗口何时开始，容易整段错过。
  # 脚本本身不等待输入（从 WSL 调用时没有可交互终端），由调用方在开跑
  # 之前与操作者对齐"现在开始"。
  Write-Host ">>> 立即开始。"

  for ($i = $WindowSec; $i -gt 0; $i--) {
    Write-Host -NoNewline "`r  剩余 $i 秒 "
    Start-Sleep -Seconds 1
  }
  Write-Host "`r  窗口结束。          "

  $after = SendText "vgptt status" 6
  Write-Host "`n=== after ==="
  Write-Host $after

  $s = ParseStats $after
  $d = ParseDetail $after
  if ($null -eq $s -or $null -eq $d) {
    Write-Host "VERDICT: FAIL (无法解析统计)"
    exit 1
  }

  Write-Host ""
  Write-Host ("begins={0} submits={1} timeouts={2} last_ms={3}" -f $s.Begins, $s.Submits, $s.Timeouts, $s.LastMs)
  Write-Host ("samples={0} events={1} timeouts_wake={2} poll_err={3} read_err={4}" -f $d.Samples, $d.Events, $d.WakeTo, $d.PollErr, $d.ReadErr)

  $ok = $true
  $notes = @()

  switch ($Case) {
    "normal" {
      if ($s.Begins -ne $Count) { $ok = $false; $notes += "begins=$($s.Begins) 期望 $Count" }
      if ($s.Submits -ne $Count) { $ok = $false; $notes += "submits=$($s.Submits) 期望 $Count" }
      if ($s.Timeouts -ne 0) { $ok = $false; $notes += "出现 timeouts=$($s.Timeouts)，不应触达上限" }
      if ($s.LastMs -lt 300 -or $s.LastMs -gt 3000) {
        $ok = $false; $notes += "last_ms=$($s.LastMs) 与约 1 秒的按住不符"
      }
    }
    "bounce" {
      if ($s.Begins -ne $Count) { $ok = $false; $notes += "begins=$($s.Begins) 期望 $Count（去抖未合并）" }
      if ($s.Submits -ne $Count) { $ok = $false; $notes += "submits=$($s.Submits) 期望 $Count" }
    }
    "long" {
      if ($s.Begins -ne $Count) { $ok = $false; $notes += "begins=$($s.Begins) 期望 $Count" }
      if ($s.Submits -ne $Count) {
        $ok = $false; $notes += "submits=$($s.Submits) 期望 $Count（松手后不应追加）"
      }
      if ($s.Timeouts -ne $Count) { $ok = $false; $notes += "timeouts=$($s.Timeouts) 期望 $Count" }
      if ($s.LastMs -lt 15000) { $ok = $false; $notes += "last_ms=$($s.LastMs) 未达 15000" }
    }
    "idle" {
      if ($d.Samples -ne 0) { $ok = $false; $notes += "空闲时 samples=$($d.Samples)，存在周期轮询" }
      if ($s.Begins -ne 0 -or $s.Submits -ne 0) { $ok = $false; $notes += "空闲时产生事件" }
    }
  }

  if ($d.PollErr -ne 0 -or $d.ReadErr -ne 0) {
    $ok = $false; $notes += "poll_err=$($d.PollErr) read_err=$($d.ReadErr)"
  }

  Write-Host ""
  if ($ok) {
    Write-Host "VERDICT: PASS ($Case)"
  } else {
    Write-Host "VERDICT: FAIL ($Case)"
    foreach ($n in $notes) { Write-Host "  - $n" }
  }

  if ($Log -ne "") {
    ($cap -join "") | Set-Content -LiteralPath $Log -Encoding UTF8
    Write-Host "[saved] $Log"
  }

  if (-not $ok) { exit 1 }
}
finally {
  if ($port.IsOpen) { $port.Close() }
}
