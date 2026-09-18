# 只读数据工具（vg_point_read / vg_run_report）的板端探针。
#
# 这两个工具是模型在 ask 时会调用的；本脚本直接经 vgagent tool 调用它们，
# 走的是和 ReAct 循环同一套 guard 与审计路径，因此**不依赖 LLM 可用**。
# 模型后端不可达时（例如域名被解析到代理假 IP），这是唯一能在板上证明
# 工具本身好使的办法。
#
# 中文必须写在本文件的常量里：PowerShell 5.1 读无 BOM 的 .ps1 会按 ANSI
# 解析，而从 WSL 传进来的中文命令行也会在转换中损坏。本文件带 UTF-8 BOM。
#
# 用法：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/vg_agent_tools_probe.ps1 `
#     -Log .\tools_probe.txt

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [int]$BootSec = 8,
  [int]$PerCmdSec = 15,
  [string]$Log = ""
)

$ErrorActionPreference = "Stop"

# 点位 id 与中文名都取自 scripts/vgpoint_scene_room.json 的已确认表。
$queries = @(
  @{ name = "id 精确（ups_load）";        args = "ups_load";              want = "id=ups_load" },
  @{ name = "中文名精确（UPS负载）";       args = "UPS负载";               want = "id=ups_load" },
  @{ name = "中文名子串歧义（UPS）";       args = "UPS";                   want = "ambiguous" },
  @{ name = "中文名唯一子串（负载）";       args = "负载";                  want = "id=ups_load" },
  @{ name = "不存在的点位";                args = "不存在的点位";           want = "not_found" }
)

$port = New-Object System.IO.Ports.SerialPort
$port.Encoding = [System.Text.Encoding]::UTF8
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.ReadTimeout = 8000
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()

$captured = New-Object System.Collections.Generic.List[string]

function Drain([int]$sec) {
  $b = ""
  $dl = (Get-Date).AddSeconds($sec)
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) {
      $chunk = $port.ReadExisting()
      $b += $chunk
      $captured.Add($chunk)
    }
    Start-Sleep -Milliseconds 100
  }
  return $b
}

function Send-Cmd([string]$c, [int]$sec) {
  $port.DiscardInBuffer()
  $port.Write("$c`r")
  Start-Sleep -Milliseconds 400
  Write-Host "`n########## $c ##########"
  $b = Drain $sec
  Write-Host $b
  return $b
}

$script:pass = 0
$script:fail = 0

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

try {
  Start-Sleep -Milliseconds 500
  Drain $BootSec | Out-Null

  # 工具定义是否真的进了模型可见的工具表。注册表会静默丢弃 JSON 不成立的
  # provider，从外面看和「模型没调用」一模一样，所以要看到字符串本身。
  $r = Send-Cmd "vgagent tools" $PerCmdSec
  Assert-Match "vg_point_read 已注册并下发" $r "vg_point_read"
  Assert-Match "vg_run_report 已注册并下发" $r "vg_run_report"

  # 逐条只读调用。
  foreach ($q in $queries) {
    $r = Send-Cmd "vgagent tool vg_point_read $($q.args)" $PerCmdSec
    Assert-Match $q.name $r $q.want
    Assert-Match "  rc=0（工具自己应答，不是 unknown tool）" $r "tool vg_point_read rc=0"
  }

  # 运行报告：三节标题与板端来源声明。数字由板上统计产生。
  $r = Send-Cmd "vgagent tool vg_run_report {}" $PerCmdSec
  Assert-Match "运行报告 rc=0" $r "tool vg_run_report rc=0"
  Assert-Match "运行报告有通信质量节" $r "通信质量"
  Assert-Match "运行报告有点位在线节" $r "点位在线"
  Assert-Match "运行报告有异常时间线节" $r "异常时间线"
  Assert-Match "运行报告声明数字是实测" $r "数字不是推测"

  # 一个不存在的工具名必须以失败告终。这条既证明探针真的走了注册表，
  # 也证明上面那些 rc=0 是工具自己应答的，而不是探针把什么都当成功。
  #
  # 这里不测「run_shell 拒绝 vgpoint」：那条 JSON 参数里带空格，而 NSH 会
  # 吃掉引号，从串口发不出合法 JSON。vgpoint 对 Agent 不可达由
  # tool_shell.c 的 s_vg_readonly 在 C 里保证，验收脚本另有 ask 路径覆盖。
  $r = Send-Cmd "vgagent tool no_such_tool_xyz {}" $PerCmdSec
  Assert-Match "不存在的工具名会失败" $r "rc=-1|unknown tool"

  Write-Output "`n[vg_agent_tools_probe] pass=$script:pass fail=$script:fail"
}
finally {
  $port.Close()
  if ($Log -ne "") {
    ($captured -join "") | Set-Content -LiteralPath $Log -Encoding UTF8
    Write-Host "[saved] $Log"
  }
}

if ($script:fail -gt 0) { exit 1 }
