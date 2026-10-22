param([Parameter(Mandatory=$true)][string]$Plan, [string]$ComPort='COM3')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
$steps=Get-Content -Raw -Encoding UTF8 -LiteralPath $Plan | ConvertFrom-Json
$port=New-Object System.IO.Ports.SerialPort $ComPort,115200
$port.Encoding=[System.Text.Encoding]::UTF8
$port.DtrEnable=$true
$port.RtsEnable=$true
$run=[Diagnostics.Stopwatch]::StartNew()
$port.Open()
try {
 foreach($step in $steps) {
  if ($run.ElapsedMilliseconds -gt 900000) { throw 'Plan exceeded 15 minutes' }
  $elapsed=[Diagnostics.Stopwatch]::StartNew()
  $seen=''
  $limit=60000
  if($step.timeout_ms) { $limit=[int]$step.timeout_ms }
  if($limit -lt 1 -or $limit -gt 60000) { throw 'Invalid timeout_ms' }
  if($step.command) {
   # Drain old output before sending, so an old prompt cannot complete this step.
   if($port.BytesToRead -gt 0) { [Console]::Write($port.ReadExisting()) }
   Write-Output ((Get-Date -Format o)+' CMD '+$step.command)
   $port.Write($step.command+"`r")
  }
  while($true) {
   if($port.BytesToRead -gt 0) {
    $chunk=$port.ReadExisting()
    [Console]::Write($chunk)
    $seen=($seen+$chunk)
    if($seen.Length -gt 16384) { $seen=$seen.Substring($seen.Length-16384) }
   }
   $ready=(-not $step.command) -or $seen.Contains('nsh>')
   if($ready -and $elapsed.ElapsedMilliseconds -ge [int]$step.wait_ms) { break }
   if($elapsed.ElapsedMilliseconds -ge $limit) { throw ('NSH prompt timeout: '+$step.command) }
   Start-Sleep -Milliseconds 20
  }
  Write-Output ''
 }
} finally { $port.Close() }
