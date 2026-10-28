# BVO-04 bounded playback exercise through the standard NxPlayer path.
# Human listening and touch confirmation are separate required evidence.
# -RecordUnderruns is diagnostic only: continue lifecycle collection, retain
# every underrun, and exit 2 instead of reporting full acceptance success.
param([int]$NormalRounds=20, [int]$StopRounds=10, [string]$ComPort='COM3',
      [switch]$RecordUnderruns)
$ErrorActionPreference='Stop'
if ($NormalRounds -lt 1 -or $NormalRounds -gt 20 -or $StopRounds -lt 0 -or $StopRounds -gt 10) { throw 'rounds out of range' }
try {[Console]::OutputEncoding=[System.Text.Encoding]::UTF8} catch {}
$p=New-Object System.IO.Ports.SerialPort $ComPort,115200
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
 # Pace input after a reproduced truncated command.  Still require bounded
 # replies and byte/callback evidence; pacing is not an audio error waiver.
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
function Snap([bool]$Player) {
 $cmd=if($Player){'!vgaudio sai'}else{'vgaudio sai'}
 $b=Send $cmd 500
 if ($b -notmatch 'dma_done=(\d+) errors=(\d+) timeouts=(\d+) txbytes=(\d+)') {throw 'missing TX counters'}
 $r=@{done=[long]$Matches[1];errors=[long]$Matches[2];timeouts=[long]$Matches[3];bytes=[long]$Matches[4]}
 if ($b -notmatch 'sai_underruns=(\d+) clockerrors=(\d+)') {throw 'missing SAI counters'}
 $r.underruns=[long]$Matches[1]; $r.clockerrors=[long]$Matches[2]
 if ($r.clockerrors -ne 0) {throw 'SAI clock error recorded'}
 if ($r.underruns -ne 0) {
  if (-not $RecordUnderruns) {throw 'SAI underrun recorded'}
  [Console]::WriteLine("DIAGNOSTIC SAI_UNDERRUN=$($r.underruns) QUALITY=FAIL (strict acceptance unchanged)")
 }
 if ($b -notmatch 'stream_ms=(\d+) stream_bytes=(\d+) tick_us=(\d+)') {throw 'missing stream timing'}
 $r.ms=[long]$Matches[1]; $r.stream_bytes=[long]$Matches[2]
 if ($b -notmatch 'saien=0') {throw 'SAI remains active after bounded wait'}
 if ($r.errors -ne 0 -or $r.timeouts -ne 0) {throw 'DMA transfer error/timeout recorded'}
 return $r
}
$p.Open()
try {
 $null=Drain 12000
 $null=Send 'vgaudio txcheck' 800
 $null=Send 'vgruntime dump' 800
 $null=Send 'vgstats dump 1' 700
 $null=Send 'vgnet status' 700
 $storage=Send 'vgstoretest 1' 8000
 if ($storage -notmatch 'vgstoretest: OK') {throw 'storage check missing success result'}
 $null=Send 'free' 700
 $baseline=Snap $false
 $null=Send 'nxplayer' 400
 $null=Send 'volume 15' 150
 $last=$baseline
 $normalDone=0; $stopDone=0; $restartDone=0
 for($i=0;$i -lt $NormalRounds;$i++) {
  $file=if($i % 2 -eq 0){'tone'}else{'speech'}
  $null=Send "play /mnt/emmc/bvo04/$file.wav" 2200
  $now=Snap $true
  if($now.bytes-$last.bytes -ne 64000 -or $now.done-$last.done -ne 8) {throw "incomplete normal round $i"}
  if ($now.stream_bytes -ne 64000 -or $now.ms -lt 950 -or $now.ms -gt 1100) {throw "normal duration outside diagnostic tolerance $i"}
  $normalDone++
  [Console]::WriteLine("ACCEPT normal=$i file=$file bytes=64000 dma=8 PASS")
  $last=$now
  if (($i+1) % 10 -eq 0) {$null=Send '!free' 500}
 }
 for($i=0;$i -lt $StopRounds;$i++) {
  $null=Send 'play /mnt/emmc/bvo04/tone-stop.wav' 200
  $timer=[Diagnostics.Stopwatch]::StartNew()
  $b=Send 'stop' 1500
  $now=Snap $true
  if($b -notmatch 'nxplayer>' -or $now.bytes-$last.bytes -le 0 -or $now.bytes-$last.bytes -ge 320000) {throw "stop not demonstrated $i"}
  $stopDone++
  [Console]::WriteLine("ACCEPT stop=$i completed_bytes=$($now.bytes-$last.bytes) bounded_wait_ms=$($timer.ElapsedMilliseconds) PASS")
  $last=$now
  $null=Send 'play /mnt/emmc/bvo04/speech.wav' 2200
  $now=Snap $true
  if($now.bytes-$last.bytes -ne 64000 -or $now.done-$last.done -ne 8) {throw "incomplete restart $i"}
  if ($now.stream_bytes -ne 64000 -or $now.ms -lt 950 -or $now.ms -gt 1100) {throw "restart duration outside diagnostic tolerance $i"}
  $restartDone++
  [Console]::WriteLine("ACCEPT restart=$i bytes=64000 dma=8 PASS")
  $last=$now
 }
 # Read-only state plus a restricted storage probe while a known file plays.
 $null=Send 'play /mnt/emmc/bvo04/tone.wav' 100
 $null=Send '!vgaudio sai' 100
 $storage=Send '!vgstoretest 1' 6000
 if ($storage -notmatch 'vgstoretest: OK') {throw 'concurrent storage result missing'}
 $last=Snap $true
 $null=Send 'play /mnt/emmc/bvo04/tone.wav' 100
 $null=Send '!vgstats dump 1' 200
 $null=Send '!vgnet status' 1200
 $last=Snap $true
 $null=Send 'quit' 600
 $null=Send 'vgruntime dump' 800
 $null=Send 'vgstats dump 1' 700
 $null=Send 'vgnet status' 700
 $storage=Send 'vgstoretest 1' 8000
 if ($storage -notmatch 'vgstoretest: OK') {throw 'storage check missing success result'}
 $null=Send 'vgaudio txcheck' 800
 $null=Send 'free' 700
 $tasks=Send 'ps' 1000
 if ($tasks -match '\b(playthread|wm8994)\b') {throw 'playback thread remains after quit'}
 $quality=if($last.underruns -eq 0){'PASS'}else{'FAIL'}
 [Console]::WriteLine("ACCEPT summary normal=$normalDone stop=$stopDone restart=$restartDone lifecycle_checks=PASS sai_quality=$quality underruns_before=$($baseline.underruns) underruns_after=$($last.underruns) human_listening=SEPARATE touch=SEPARATE")
 if ($last.underruns -ne 0) {exit 2}
} finally { $p.Close() }
