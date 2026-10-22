param([string]$Plan, [string]$ComPort='COM3')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
$port=New-Object System.IO.Ports.SerialPort $ComPort,115200
$port.Encoding=[System.Text.Encoding]::UTF8
$port.DtrEnable=$true
$port.RtsEnable=$true
$port.Open()
try {
 foreach($step in (Get-Content -Raw -LiteralPath $Plan | ConvertFrom-Json)) {
  if($step.command) { Write-Output ((Get-Date -Format o)+' CMD '+$step.command); $port.Write($step.command+"`r") }
  $until=[DateTime]::UtcNow.AddMilliseconds($step.wait_ms)
  while([DateTime]::UtcNow -lt $until) {
   if($port.BytesToRead -gt 0) { [Console]::Write($port.ReadExisting()) }
   Start-Sleep -Milliseconds 20
  }
  if($port.BytesToRead -gt 0) { [Console]::Write($port.ReadExisting()) }
  Write-Output ''
 }
} finally { $port.Close() }
