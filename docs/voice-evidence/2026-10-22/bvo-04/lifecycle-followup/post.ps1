$ErrorActionPreference='Stop'
try {[Console]::OutputEncoding=[System.Text.Encoding]::UTF8} catch {}
$p=New-Object System.IO.Ports.SerialPort 'COM3',115200
$p.Encoding=[System.Text.Encoding]::UTF8
$p.DtrEnable=$true; $p.RtsEnable=$true
function Drain([int]$ms) {
 $b='';$until=(Get-Date).AddMilliseconds($ms)
 while ((Get-Date) -lt $until) {
  if ($p.BytesToRead -gt 0) {$s=$p.ReadExisting();$b+=$s;[Console]::Write($s)}
  Start-Sleep -Milliseconds 10
 }
 return $b
}
function Send([string]$cmd,[int]$ms) {
 [Console]::WriteLine("`nHOST $((Get-Date).ToString('o')) SEND $cmd")
 foreach ($ch in ($cmd+"`r").ToCharArray()) {
  $p.Write([string]$ch)
  Start-Sleep -Milliseconds 5
 }
 $b=Drain $ms
 $deadline=(Get-Date).AddSeconds(20)
 while ($b -notmatch '(nsh>|nxplayer>)' -and (Get-Date) -lt $deadline) {
  $b+=Drain 100
 }
 if ($b -notmatch '(nsh>|nxplayer>)') {throw "command did not return within bounded wait: $cmd"}
 return $b
}
$p.Open()
try {
 $null=Drain 300
 $null=Send 'vgaudio codec' 500
 $null=Send 'vgpdmdump -r 3 -n 16384' 1500
 $null=Send 'vgaudio sai' 500
 $null=Send 'free' 500
 $null=Send 'ps' 500
} finally { $p.Close() }
