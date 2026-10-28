param(
  [string]$ComPort = 'COM3',
  [ValidateRange(0.01,168)][double]$Hours = 24,
  [ValidateRange(1,10000)][int]$StoreRounds = 50,
  [ValidateRange(1,3600)][int]$IntervalSec = 60,
  [ValidateRange(0,32)][int]$MinimumOnline = 0,
  [string]$LogPath = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
if (!$LogPath) { $LogPath = Join-Path $PSScriptRoot '../.debug/maintenance-soak.log' }
$LogPath = [IO.Path]::GetFullPath($LogPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LogPath)) | Out-Null
if ([IO.Ports.SerialPort]::GetPortNames() -notcontains $ComPort) {
  throw "Board port $ComPort is unavailable. No reset or recovery was attempted."
}
$port = New-Object IO.Ports.SerialPort $ComPort,115200
$port.Encoding = [Text.Encoding]::UTF8
$writer = New-Object IO.StreamWriter($LogPath,$true,[Text.Encoding]::UTF8)
$writer.AutoFlush = $true
$script:LastReply = ''
function Record([string]$Text) { $writer.WriteLine($Text); Write-Host $Text }
function Invoke-Nsh([string]$Command,[string]$Expected) {
  Record ("COMMAND {0:o} {1}" -f (Get-Date),$Command)
  $port.Write("$Command`r")
  $timer = [Diagnostics.Stopwatch]::StartNew()
  $text = ''
  while ($timer.Elapsed.TotalSeconds -lt 120) {
    $text += $port.ReadExisting()
    if ($text -match 'HARDFAULT|PANIC|Assertion failed|outside SRAM|Queue full') {
      Record $text
      throw 'Firmware failure detected; soak stopped without reset.'
    }
    $plain = $text -replace '\x1b\[[0-9;]*[A-Za-z]', ''
    if ($plain -match $Expected -and $plain -match 'nsh>\s*$') {
      Record $text
      Record ("COMMAND_MS {0}" -f $timer.ElapsedMilliseconds)
      $script:LastReply = $plain
      return
    }
    Start-Sleep -Milliseconds 100
  }
  Record $text
  throw "Command timeout: $Command"
}
function Read-PointTable {
  # A background log can interleave with one console row. Retry once rather
  # than treating an incomplete console print as an actual table change.
  for ($attempt=0; $attempt -lt 2; $attempt++) {
    Invoke-Nsh 'vgpoint list' 'vgpoint: OK cmd=list'
    $count = [regex]::Match($script:LastReply,'table=committed n=(\d+)')
    $rows = @([regex]::Matches($script:LastReply,'(?m)^vgpoint: POINT [^\r\n]+') | ForEach-Object {$_.Value})
    if ($count.Success -and $rows.Count -eq [int]$count.Groups[1].Value) {
      return ($rows -join "`n")
    }
  }
  throw 'Incomplete committed-table console output.'
}
function Check-Heap {
  for ($attempt=0; $attempt -lt 2; $attempt++) {
    Invoke-Nsh 'cat /proc/meminfo' 'Umem'
    $m = [regex]::Match($script:LastReply,'(?m)^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+\d+\s+Umem\s*$')
    if ($m.Success) {
      [long]$total=$m.Groups[1].Value; [long]$used=$m.Groups[2].Value
      [long]$available=$m.Groups[3].Value; [long]$largest=$m.Groups[5].Value
      if ($total -le 0 -or $used+$available -ne $total -or $largest -gt $available) {
        throw 'Heap accounting is inconsistent; stopped without reset.'
      }
      return
    }
  }
  throw 'Incomplete heap console output.'
}
try {
  $port.Open()
  Record ("START {0:o} port={1} hours={2} online_min={3} scope=storage-runtime" -f (Get-Date),$ComPort,$Hours,$MinimumOnline)
  Invoke-Nsh '' 'nsh>'
  Invoke-Nsh 'uname -a' 'NuttX'
  $baseline = Read-PointTable
  $timer = [Diagnostics.Stopwatch]::StartNew()
  [long]$lastUptime=-1
  $cycles = 0
  do {
    Invoke-Nsh "vgstoretest $StoreRounds" 'vgstoretest: OK'
    if ((Read-PointTable) -cne $baseline) { throw 'Committed table changed during soak.' }
    Invoke-Nsh 'vgagent status' 'llm: credentials=ready'
    Invoke-Nsh 'vgruntime dump' 'vgruntime: uptime_s='
    $m=[regex]::Match($script:LastReply,'vgruntime: uptime_s=(\d+)')
    if (!$m.Success) { throw 'Runtime uptime is missing.' }
    [long]$uptime=$m.Groups[1].Value
    if ($uptime -lt $lastUptime) { throw 'Board uptime decreased; unexpected reset.' }
    $lastUptime=$uptime
    $online=[regex]::Matches($script:LastReply,'vgruntime: point [^\r\n]+ online=1\b').Count
    if ($online -lt $MinimumOnline) { throw "Only $online points online; need $MinimumOnline." }
    Check-Heap
    $cycles++
    Record ("CYCLE {0} elapsed_s={1:N0} uptime_s={2} online={3}" -f $cycles,$timer.Elapsed.TotalSeconds,$uptime,$online)
    if ($timer.Elapsed.TotalHours -lt $Hours) { Start-Sleep -Seconds $IntervalSec }
  } while ($timer.Elapsed.TotalHours -lt $Hours)
  Record ("PASS scope=storage-runtime cycles={0} elapsed_s={1:N0} time={2:o}" -f $cycles,$timer.Elapsed.TotalSeconds,(Get-Date))
} catch {
  Record ("FAIL {0:o} {1}" -f (Get-Date),$_.Exception.Message)
  throw
} finally {
  if ($port.IsOpen) { $port.Close() }
  $port.Dispose()
  $writer.Dispose()
}
