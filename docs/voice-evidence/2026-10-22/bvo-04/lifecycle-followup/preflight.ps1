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
 $null=Drain 8000
 $null=Send 'vgaudio fixtures' 10000
 $null=Send 'vgaudio id' 300
 $null=Send 'nxplayer' 400
 $null=Send 'volume 15' 200
 $null=Send 'play /mnt/emmc/bvo04/tone-stop.wav' 6000
 $null=Send '!vgaudio sai' 500
 $null=Send 'play /mnt/emmc/bvo04/speech.wav' 2000
 $null=Send 'quit' 500
 $null=Send 'vgaudio codec' 500
} finally { $p.Close() }
