param([string]$ComPort='COM3',[int]$Seconds=55,[int]$IntervalMs=2000,[string]$Cmd='vgptt status',[string]$FinalCmd='vgvoice status')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
$port=New-Object System.IO.Ports.SerialPort $ComPort,115200
$port.Encoding=[System.Text.Encoding]::UTF8
$port.DtrEnable=$true
$port.RtsEnable=$true
$port.Open()
try {
  $end=[DateTime]::UtcNow.AddSeconds($Seconds)
  while([DateTime]::UtcNow -lt $end) {
    $port.DiscardInBuffer()
    Write-Output ((Get-Date -Format o)+' CMD '+$Cmd)
    $port.Write($Cmd+"`r")
    $until=[DateTime]::UtcNow.AddMilliseconds($IntervalMs-200)
    $buf=''
    while([DateTime]::UtcNow -lt $until) {
      if($port.BytesToRead -gt 0) { $buf += $port.ReadExisting() }
      Start-Sleep -Milliseconds 50
    }
    Write-Output $buf
  }
  if($FinalCmd) {
    $port.DiscardInBuffer()
    Write-Output ((Get-Date -Format o)+' CMD '+$FinalCmd)
    $port.Write($FinalCmd+"`r")
    Start-Sleep -Milliseconds 900
    if($port.BytesToRead -gt 0) { Write-Output $port.ReadExisting() }
  }
} finally { $port.Close() }
