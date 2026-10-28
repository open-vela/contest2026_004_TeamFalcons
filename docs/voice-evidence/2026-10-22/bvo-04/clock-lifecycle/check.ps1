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
 $p.Write("$cmd`r")
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
 $null=Drain 10000
 $null=Send 'vgaudio id' 300
 $null=Send 'vgaudio codec' 500
 $null=Send 'vgaudio txcheck' 500
 $null=Send 'free' 500
 $null=Send 'nxplayer' 300
 $null=Send 'volume 15' 200
 $null=Send 'play /mnt/emmc/bvo04/tone.wav' 100
 $null=Send '!vgaudio codec' 500
 $null=Drain 2000
 $null=Send '!vgaudio sai' 500
 $null=Send 'play /mnt/emmc/bvo04/speech.wav' 2000
 $null=Send '!vgaudio sai' 500
 $null=Send 'quit' 500
 $null=Send 'vgaudio codec' 500
 $null=Send 'vgpdmdump -r 3 -n 16384' 1500
 $null=Send 'vgnet status' 500
 $null=Send 'vgstats dump 1' 500
 $null=Send 'vgstoretest 1' 6000
 $null=Send 'free' 500
 $null=Send 'ps' 500
} finally { $p.Close() }
