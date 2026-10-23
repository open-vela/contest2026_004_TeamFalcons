param([Parameter(Mandatory=$true)][string]$Plan, [string]$ComPort='COM3',
      [string]$FixtureControlPath='', [string]$LogFile='')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
$steps=Get-Content -Raw -Encoding UTF8 -LiteralPath $Plan | ConvertFrom-Json
$logWriter=$null
if ($LogFile) {
  $logWriter=New-Object System.IO.StreamWriter($LogFile, $false, [System.Text.UTF8Encoding]::new($false))
}
function Log-Chunk([string]$text) {
  [Console]::Write($text)
  if ($logWriter) { $logWriter.Write($text); $logWriter.Flush() }
}
function Log-Line([string]$text) {
  Write-Output $text
  if ($logWriter) { $logWriter.WriteLine($text); $logWriter.Flush() }
}
$port=New-Object System.IO.Ports.SerialPort $ComPort,115200
$port.Encoding=[System.Text.Encoding]::UTF8
$port.DtrEnable=$true
$port.RtsEnable=$true
$run=[Diagnostics.Stopwatch]::StartNew()
$port.Open()
$lastResponse=''
$sinceCmd=''
Log-Line 'HARNESS v2026-10-24c stepdbg+livemerge'
try {
 foreach($step in $steps) {
  if ($run.ElapsedMilliseconds -gt 900000) { throw 'Plan exceeded 15 minutes' }
  if($step.mode) {
   if(!$FixtureControlPath -or $step.mode -notin @('normal','warn','critical','offline','offline1','offline2','offline12','critical_offline2')) {
    throw 'Invalid fixture mode or missing FixtureControlPath'
   }
   # 从站读取控制文件时会短暂持有 Windows 共享锁；仅对此类 IO 冲突限时重试。
   $modeDeadline=[Diagnostics.Stopwatch]::StartNew()
   while($true) {
    try { [IO.File]::WriteAllText($FixtureControlPath,[string]$step.mode); break }
    catch [IO.IOException] {
     if($modeDeadline.ElapsedMilliseconds -ge 2000) { throw }
     Start-Sleep -Milliseconds 20
    }
   }
   Log-Line ((Get-Date -Format o)+' FIXTURE_MODE '+$step.mode)
  }
  $elapsed=[Diagnostics.Stopwatch]::StartNew()
  $seen=''
  # 无命令的等待继承上一响应，避免漏掉快速失败的完成标记。
Log-Line ('STEP cmd=['+$step.command+'] lr='+$lastResponse.Length+' sc='+$sinceCmd.Length)
  if(!$step.command -and $step.wait_for) { $seen=($lastResponse+$sinceCmd); Log-Line ('PRESEED seen='+$seen.Length+' hasMarker='+$seen.Contains([string]$step.wait_for)) }
  $limit=60000
  if($step.timeout_ms) { $limit=[int]$step.timeout_ms }
  if($limit -lt 1 -or $limit -gt 60000) { throw 'Invalid timeout_ms' }
  if($step.command) {
   # Drain old output before sending, so an old prompt cannot complete this step.
   if($port.BytesToRead -gt 0) { Log-Chunk ($port.ReadExisting()) }
   Log-Line ((Get-Date -Format o)+' CMD '+$step.command)
   $sinceCmd=''
   $port.Write($step.command+"`r")
  }
  while($true) {
   if($port.BytesToRead -gt 0) {
    $chunk=$port.ReadExisting()
    Log-Chunk $chunk
    $seen=($seen+$chunk)
    $sinceCmd=($sinceCmd+$chunk)
    if($seen.Length -gt 16384) { $seen=$seen.Substring($seen.Length-16384) }
    if($sinceCmd.Length -gt 16384) { $sinceCmd=$sinceCmd.Substring($sinceCmd.Length-16384) }
   }
   $ready=(-not $step.command) -or $seen.Contains('nsh>')
   if($step.wait_for) {
    $hay = if($step.command){$seen}else{$seen+$sinceCmd}
    $pos=0
    foreach($needle in @($step.wait_for)) {
     $found=$hay.IndexOf([string]$needle,$pos,[StringComparison]::Ordinal)
     if($found -lt 0) { $ready=$false; break }
     $pos=$found+([string]$needle).Length
    }
   }
   if($ready -and $elapsed.ElapsedMilliseconds -ge [int]$step.wait_ms) { break }
   if($elapsed.ElapsedMilliseconds -ge $limit) {
    if((!$step.command -and $step.optional_wait) -or ($step.command -and $step.optional_prompt)) {
     Log-Line ('EXPECTED_MARKER_MISSING: '+$step.command+'; raw response retained')
     break
    }
    throw ('NSH prompt timeout: '+$step.command)
   }
   Start-Sleep -Milliseconds 20
  }
  # 等待末尾输出完成，避免元数据与下一条命令交错。
  Start-Sleep -Milliseconds 200
  if($port.BytesToRead -gt 0) {
   $tail=$port.ReadExisting(); Log-Chunk $tail; $seen+=$tail; $sinceCmd+=$tail
   Log-Line ('TAIL seen='+$seen.Length+' sc='+$sinceCmd.Length)
   if($sinceCmd.Length -gt 16384) { $sinceCmd=$sinceCmd.Substring($sinceCmd.Length-16384) }
  }
  $lastResponse=$seen
  Log-Line ''
 }
} finally {
 if ($logWriter) { $logWriter.Dispose() }
 $port.Close()
}
