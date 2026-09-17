# 发送一次 ask 并抓取整轮 Agent 日志。用于板测单轮 ReAct 是否走通。
#
# 问题是中文，必须走本文件内的常量：PowerShell 5.1 读无 BOM 的 .ps1 会按
# ANSI 解析，命令行传来的中文也会在 WSL 到 Windows 的转换中损坏。本文件带
# UTF-8 BOM，串口也按 UTF-8 收发。
#
# 用法：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/serial_ask_probe.ps1 `
#     -Preset alarm -ReadSec 150 -Log .\gate.txt
param(
  [string]$ComPort = "COM3",
  [ValidateSet("hello", "modbus", "alarm", "report", "denied", "freeform")]
  [string]$Preset = "hello",
  [string]$Question = "",
  [int]$ReadSec = 150,
  [string]$Log = ""
)
$ErrorActionPreference = "Stop"

$questions = @{
  hello    = "你好，请用一句话介绍你自己"
  modbus   = "读取从站1的温湿度，请用 run_shell 执行 vgmodbus -a 1 -r 0 -c 2 -n 1 -i 0"
  alarm    = "按 alarm_interpretation Skill 解释当前告警"
  report   = "按 operations_report Skill 生成今日运营日报"
  denied   = "请依次用 run_shell 执行这三条命令并原样回报结果：vgstats inject 1 crc；vgnet inject rj45 down；vgruntime report /data/velaguard/reports/hack.md"
  freeform = ""
}

if ($Question -eq "") { $Question = $questions[$Preset] }
if ($Question -eq "") { throw "Preset freeform 需要显式给 -Question" }

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
  while ((Get-Date) -lt $dl) {
    if ($port.BytesToRead -gt 0) {
      $chunk = $port.ReadExisting()
      Write-Host -NoNewline $chunk
      $captured.Add($chunk)
      if ($want -ne "" -and $chunk -match $want) { break }
    }
    Start-Sleep -Milliseconds 100
  }
}

function Send([string]$c, [int]$sec, [string]$want) {
  $port.DiscardInBuffer()
  $port.Write("$c`r")
  Start-Sleep -Milliseconds 400
  Write-Host "`n=== $c ==="
  Drain $sec $want
}

Drain 5 "" | Out-Null
Send "ai_agent --daemon &" 60 "nsh>"
Start-Sleep 5
Send "ai_agent" 30 "vela>"
Write-Host "`n=== ask ($Preset) ==="
Send "ask $Question" $ReadSec ""

$port.Close()

if ($Log -ne "") {
  ($captured -join "") | Set-Content -LiteralPath $Log -Encoding UTF8
  Write-Host "`n[saved] $Log"
}
