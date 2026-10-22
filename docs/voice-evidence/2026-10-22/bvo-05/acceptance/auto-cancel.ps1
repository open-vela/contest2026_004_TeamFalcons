param([string]$ComPort='COM3',[int]$Count=3,[int]$CancelAfterMs=1500,[int]$TimeoutSec=60)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.Encoding]::UTF8
$port=New-Object System.IO.Ports.SerialPort $ComPort,115200
$port.Encoding=[System.Text.Encoding]::UTF8
$port.DtrEnable=$true
$port.RtsEnable=$true
$port.Open()
function Send-Cmd([string]$cmd,[int]$readMs) {
  $port.DiscardInBuffer()
  Write-Host ((Get-Date -Format o)+' CMD '+$cmd)
  $port.Write($cmd+"`r")
  $until=[DateTime]::UtcNow.AddMilliseconds($readMs)
  $buf=''
  while([DateTime]::UtcNow -lt $until) {
    if($port.BytesToRead -gt 0) { $buf += $port.ReadExisting() }
    Start-Sleep -Milliseconds 40
  }
  Write-Host $buf
  return $buf
}
try {
  $done=0; $lastBegin=-1; $end=[DateTime]::UtcNow.AddSeconds($TimeoutSec)
  while($done -lt $Count -and [DateTime]::UtcNow -lt $end) {
    $r = Send-Cmd 'vgptt status' 350
    $m = [regex]::Match($r,'state=(\w+)')
    $b = [regex]::Match($r,'begins=(\d+)')
    if($m.Success -and $b.Success -and $m.Groups[1].Value -eq 'recording') {
      $begin=[int]$b.Groups[1].Value
      if($begin -ne $lastBegin) {
        $lastBegin=$begin
        Start-Sleep -Milliseconds $CancelAfterMs
        Send-Cmd 'vgvoice cancel' 600 | Out-Null
        $done++
        Write-Host ((Get-Date -Format o)+" CANCEL-SENT begin=$begin n=$done")
        Start-Sleep -Milliseconds 1500
      }
    }
    Start-Sleep -Milliseconds 150
  }
  Send-Cmd 'vgvoice status' 800 | Out-Null
  Send-Cmd 'vgptt status' 800 | Out-Null
} finally { $port.Close() }
