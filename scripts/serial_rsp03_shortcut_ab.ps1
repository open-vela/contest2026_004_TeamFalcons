# RSP-03 第二部分：单点查数捷径的配对测量，以及必须回到完整流程的负例。
#
# 同一份固件跑两臂：point_shortcut off 是原来的两次请求流程，on 是命中
# 捷径后直接用工具输出作答。交替先跑哪一臂，避免服务端首字节延迟的漂移
# 系统性地偏向一侧（RSP-02 已测得该延迟在 2 秒到 8 秒之间摆动）。
#
# 每轮记录：整轮耗时（END 行的 elapsed_ms）与该轮的 LLM 请求次数（BEGIN
# 与 END 之间的 http iter 行数）。这两项才是捷径的收益与正确性所在。
#
# 中文问题必须走本文件内的常量。PowerShell 5.1 读无 BOM 的 .ps1 会按 ANSI
# 解析，命令行传来的中文也会在 WSL 到 Windows 的转换中损坏。本文件带 UTF-8
# BOM，串口也按 UTF-8 收发。
#
# 用法：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/serial_rsp03_shortcut_ab.ps1 `
#     -Pairs 10 `
#     -Log .\docs\maintenance-evidence\2026-10-20\rsp03-shortcut-ab.log
#
#   -Negative 跑负例组：解释类、多点、离线、过期。负例只用来确认"没有命中"，
#   不计入配对统计。
param(
  [string]$ComPort = "COM3",
  [ValidateRange(0, 20)][int]$Pairs = 10,
  [ValidateRange(10, 180)][int]$ReadSec = 60,
  [string]$Log = "",
  [switch]$Negative,
  [string]$SlaveControl = ""
)
$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

function Get-Question([string]$key)
{
  # 被测问题：只问当前值，不要求解释。
  $map = @{
    ups      = "告诉我UPS负载的值"
    temp     = "现在温度是多少"
    volt     = "A相电压多少伏"
    humid    = "湿度多少"
    # 负例：需要措辞、判断或汇总，捷径必须让位给完整流程。
    why      = "UPS负载为什么这么高"
    normal   = "UPS负载正常吗"
    compare  = "比较温度和回风温度的差别"
    report   = "生成今天的运营日报"
    multi    = "温度和湿度分别是多少"
    offline  = "告诉我UPS负载的值"
    stale    = "告诉我UPS负载的值"
  }
  if (-not $map.ContainsKey($key)) { throw "未知问题键: $key" }
  return $map[$key]
}

$port = New-Object System.IO.Ports.SerialPort $ComPort, 115200
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Start-Sleep 1

$captured = New-Object System.Collections.Generic.List[string]

# Drain 到看齐 $want 或超时。want 为空时按秒数排空。
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

# 一轮 ask：先清会话与缓存，再发问题，等到 END 行后收尾。
# 清会话让每一轮都是独立问答，避免上一轮的上下文改变模型的选择。
function Round([string]$arm, [string]$label, [string]$question) {
  Send "point_shortcut $arm" 8 "point shortcut: (on|off)" | Out-Null
  Send "session_clear console" 6 "Session (cleared|not found)" | Out-Null
  Send "cache_clear" 6 "cache cleared" | Out-Null
  Write-Host "`n--- pair=$label arm=$arm question=$question ---"
  $port.DiscardInBuffer()
  $marker = "/// RSP03 BEGIN $label $arm ///"
  $captured.Add($marker)
  $port.Write("ask $question`r")
  Start-Sleep -Milliseconds 400
  Drain 8 "" | Out-Null
  # 等到本轮 END，再给 3 秒收尾部输出。
  Drain $ReadSec "END status=" | Out-Null
  Drain 3 "" | Out-Null

  # 网络中断的轮次耗时不到 1 秒且不产生工具调用，会以 status=fail 出现。
  # 这类轮次不属于本次实验，原样记下并在汇总里剔除，不当作捷径的收益或
  # 损失。
  $captured.Add("/// RSP03 END $label $arm ///")
}

function SetSlaveMode([string]$mode) {
  if ($SlaveControl -eq "") { throw "需要 -SlaveControl 才能切换从站模式" }
  [IO.File]::WriteAllText($SlaveControl, $mode)
  Write-Host "`n=== slave mode -> $mode ==="
}

try {
  Drain 4 "" | Out-Null
  # 守护进程由固件自启动；这一句在已运行时立即返回，不重复拉起。
  Send "ai_agent --daemon &" 20 "nsh>" | Out-Null
  Start-Sleep 2
  Send "ai_agent" 25 "vela>" | Out-Null
  Send "point_shortcut" 6 "point shortcut: (on|off)" | Out-Null

  if (-not $Negative) {
    # 同一问题的措辞轮换：连续 24 次一字不差的提问会落在服务端缓存或限流
    # 路径上，测到的就不是捷径的差别。四种问法各指向一个已确认点位，都
    # 只问当前值。
    $rotation = @("ups", "temp", "volt", "humid")
    for ($i = 1; $i -le $Pairs; $i++) {
      # 交替先跑哪一臂，使排序不能偏向任何一侧。
      if ($i % 2 -eq 1) { $order = @("off", "on") } else { $order = @("on", "off") }
      $q = Get-Question $rotation[($i - 1) % $rotation.Count]
      foreach ($arm in $order) {
        Round $arm ("p" + $i) $q
      }
    }
  } else {
    # 负例：每一组都在两臂下各问一次，确认两臂都走完整流程（或都命中）。
    $cases = @(
      @{ key = "why";     arm = "on" },
      @{ key = "why";     arm = "off" },
      @{ key = "normal";  arm = "on" },
      @{ key = "normal";  arm = "off" },
      @{ key = "compare"; arm = "on" },
      @{ key = "report";  arm = "on" },
      @{ key = "multi";   arm = "on" },
      @{ key = "temp";    arm = "on" },
      @{ key = "temp";    arm = "off" },
      @{ key = "volt";    arm = "on" },
      @{ key = "humid";   arm = "on" }
    )
    $n = 0
    foreach ($c in $cases) {
      $n++
      Round $c.arm ("n" + $n + "-" + $c.key) (Get-Question $c.key)
    }

    # 离线：从站不再应答。采集器每个点要等两次 500 ms 超时，整轮采集随之
    # 拉长，快照停止更新。等足够久后再问，工具输出应变成 read_failed；
    # 若这轮采集还没结束、旧样本仍在，则会先表现为 stale=1。两种都必须
    # 让位给完整流程，所以两臂各问一次，读数由判定横幅区分。
    Send "vgpoint get" 15 "" | Out-Null
    SetSlaveMode "offline"
    Start-Sleep 20
    Send "vgpoint get" 20 "" | Out-Null
    Round "on" "n-offline-on" (Get-Question "offline")
    Round "off" "n-offline-off" (Get-Question "offline")

    SetSlaveMode "normal"
    Start-Sleep 10
    Send "vgpoint get" 20 "" | Out-Null
  }

  Send "point_shortcut off" 6 "point shortcut: off" | Out-Null
  Send "quit" 5 "" | Out-Null
  Start-Sleep 1
}
finally {
  if ($port.IsOpen) { $port.Close() }
}

$text = ($captured -join "")
if ($Log -ne "") {
  $text | Set-Content -LiteralPath $Log -Encoding UTF8
  Write-Host "`n[saved] $Log"
}

# ── 提取 ────────────────────────────────────────────────────────
#
# 每轮的臂取自它前面的 point_shortcut 行，成本取该轮的 END 行，请求数取
# BEGIN 与 END 之间的 http iter 行。只统计 chat=console，板端自发的告警
# 轮次走 local_client，不属于本实验。

Write-Host "`n===== RSP-03 shortcut A/B ====="

$rounds = New-Object System.Collections.Generic.List[object]
$arm = "off"
$inRound = $false
$round = $null

foreach ($line in ($text -split "`r?`n")) {
  if ($line -match "point shortcut: (on|off)") { $arm = $Matches[1] }
  if ($line -match "/// RSP03 BEGIN (\S+) (\S+) ///") {
    $inRound = $true
    $round = @{ label = $Matches[1]; arm = $Matches[2]; http = 0; cache = 0
                verdict = ""; end = $null; llm_ms = $null; trace = "" }
    continue
  }
  if (-not $inRound) { continue }

  # 板端自发的告警轮次（chat=velaguard:N）会在同一串口上交错输出。只认
  # 本轮 console 的 trace id，否则别轮的 http 行会被算进本轮请求数。

  if ($round.trace -eq "" -and $line -match "\[trace:(\w+)\] BEGIN chat=console chan=cli") {
    $round.trace = $Matches[1]
    continue
  }
  if ($round.trace -eq "") { continue }

  if ($line -match ("\[trace:" + $round.trace + "\] http iter=")) { $round.http++ }
  if ($line -match "Cache hit, skipping LLM call") { $round.cache++ }
  if ($line -match "Point shortcut: (\w+)") { $round.verdict = $Matches[1] }
  if ($line -match ("\[trace:" + $round.trace + "\] END status=(\w+).*llm_ms=(\d+) .*elapsed_ms=(\d+)")) {
    $round.end = [int]$Matches[3]
    $round.llm_ms = [int]$Matches[2]
    $round.status = $Matches[1]
    $rounds.Add([pscustomobject]$round)
    $inRound = $false
  }
}

foreach ($r in $rounds) {
  Write-Host ("  {0,-16} arm={1,-3} http={2} cache={3} verdict={4,-14} status={5,-7} elapsed={6}ms llm={7}ms" -f
    $r.label, $r.arm, $r.http, $r.cache,
    ($(if ($r.verdict -ne "") { $r.verdict } else { "-" })),
    $r.status, $r.end, $r.llm_ms)
}

$paired = $rounds | Where-Object { $_.label -match "^p\d+$" }
$valid = $paired | Where-Object { $_.status -eq "ok" }
$dropped = $paired | Where-Object { $_.status -ne "ok" }
if ($dropped.Count -gt 0) {
  Write-Host ("`n{0} 轮因 LLM 建连失败被剔除（不属于本次实验）: {1}" -f
    $dropped.Count, (($dropped | ForEach-Object { "$($_.label)/$($_.arm)" }) -join ", "))
}

if ($valid.Count -gt 0) {
  foreach ($a in @("off", "on")) {
    $sel = $valid | Where-Object { $_.arm -eq $a }
    if ($sel.Count -eq 0) { continue }
    $vals = $sel | ForEach-Object { $_.end } | Sort-Object
    $med = $vals[[int]([math]::Floor($vals.Count / 2))]
    $http = ($sel | ForEach-Object { $_.http } | Measure-Object -Average).Average
    $hit = ($sel | Where-Object { $_.verdict -eq "hit" }).Count
    Write-Host ("{0,-3} n={1} median={2}ms max={3}ms http_per_round={4:n1} hits={5}" -f
      $a, $sel.Count, $med, (($vals | Measure-Object -Maximum).Maximum), $http, $hit)
  }

  # 逐对差值：只在两臂都完成的配对里比较。
  $labels = $valid | ForEach-Object { $_.label } | Sort-Object -Unique
  $diffs = New-Object System.Collections.Generic.List[int]
  foreach ($l in $labels) {
    $o = $valid | Where-Object { $_.label -eq $l -and $_.arm -eq "off" } | Select-Object -First 1
    $n = $valid | Where-Object { $_.label -eq $l -and $_.arm -eq "on" } | Select-Object -First 1
    if ($o -and $n) {
      $d = $o.end - $n.end
      $diffs.Add($d)
      Write-Host ("  {0}: off={1}ms on={2}ms diff(off-on)={3}ms http off={4} on={5}" -f
        $l, $o.end, $n.end, $d, $o.http, $n.http)
    }
  }
  if ($diffs.Count -gt 0) {
    $sorted = $diffs | Sort-Object
    $dmed = $sorted[[int]([math]::Floor($sorted.Count / 2))]
    $mean = ($diffs | Measure-Object -Average).Average
    $wins = ($diffs | Where-Object { $_ -gt 0 }).Count
    Write-Host ("median diff (off-on)={0}ms  mean={1:n0}ms  on faster in {2}/{3}" -f
      $dmed, $mean, $wins, $diffs.Count)
  }
}
