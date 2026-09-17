# Stage0 板端验收：经 COM3 NSH 批量执行 stage0-acceptance.md 核心命令。
# 用法（先关闭其它占用 COM3 的串口监视器）：
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage0_accept.ps1
#   powershell.exe -ExecutionPolicy Bypass -File scripts/stage0_accept.ps1 -Port COM3 -Log stage0-ac.log

param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$Log = ""
)

$ErrorActionPreference = "Stop"

function Wait-NshPrompt {
  param([string]$Buf, [int]$TimeoutSec = 8)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($Buf -match "nsh>" -or $Buf -match "vela>") { return $true }
    Start-Sleep -Milliseconds 120
  }
  return $false
}

$port = New-Object System.IO.Ports.SerialPort
$port.Encoding = [System.Text.Encoding]::UTF8
$port.PortName = $ComPort
$port.BaudRate = $Baud
$port.Parity = [System.IO.Ports.Parity]::None
$port.DataBits = 8
$port.StopBits = [System.IO.Ports.StopBits]::One
$port.ReadTimeout = 8000
$port.NewLine = "`n"
$logLines = New-Object System.Collections.Generic.List[string]

try {
  $port.Open()
  Start-Sleep -Milliseconds 800
  while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 80 }

  function Invoke-Nsh {
    param([string]$Cmd)
    $port.DiscardInBuffer()
    $port.WriteLine($Cmd)
    Start-Sleep -Milliseconds 900
    $buf = ""
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
      if ($port.BytesToRead -gt 0) { $buf += $port.ReadExisting() }
      if (Wait-NshPrompt $buf 1) { break }
      Start-Sleep -Milliseconds 120
    }
    $block = "`n=== $Cmd ===`n$buf"
    Write-Output $block
    $logLines.Add($block) | Out-Null
    return $buf
  }

  Invoke-Nsh "?"
  Invoke-Nsh "ls /dev/mmcsd0"
  Invoke-Nsh "mount"
  Invoke-Nsh "vgstats inject 1 crc"
  Invoke-Nsh "vgstats inject 1 timeout"
  Invoke-Nsh "vgstats dump 1"
  Invoke-Nsh "vgstats reset 1"
  Invoke-Nsh "vgcfg dump"
  Invoke-Nsh "vgrs485 tx"

  Write-Output "`n[stage0_accept] done. Check vgstats dump (crc/timeout > 0) and ? lists vgstats."
}
catch {
  Write-Error "stage0_accept failed on ${ComPort}: $($_.Exception.Message)"
  Write-Error "Close serial monitor / MThings on COM3 and retry."
  exit 1
}
finally {
  if ($port.IsOpen) { $port.Close() }
  if ($Log -ne "") {
    $logPath = $Log
    if ($Log -match '^/') {
      $winLog = & wslpath -w $Log 2>$null
      if ($winLog) { $logPath = $winLog }
    }
    $logLines -join "`n" | Set-Content -LiteralPath $logPath -Encoding UTF8
    Write-Output "Log: $logPath"
  }
}
