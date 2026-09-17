# Board vgscan 1-32 via COM3 NSH
param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [string]$Range = "1-32",
  [int]$TimeoutSec = 240
)

$ErrorActionPreference = "Stop"
$port = New-Object System.IO.Ports.SerialPort $ComPort, $Baud
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 5000
$port.WriteTimeout = 3000
$port.NewLine = "`n"
$port.DtrEnable = $true

try {
  $port.Open()
  Start-Sleep -Seconds 4
  while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 50 }

  $port.WriteLine("")
  Start-Sleep -Milliseconds 400
  while ($port.BytesToRead -gt 0) { [void]$port.ReadExisting(); Start-Sleep -Milliseconds 50 }

  $port.WriteLine("?")
  Start-Sleep -Seconds 1
  $help = ""
  while ($port.BytesToRead -gt 0) { $help += $port.ReadExisting(); Start-Sleep -Milliseconds 80 }
  Write-Output "=== help snippet ==="
  Write-Output (($help -split "`n" | Select-String -Pattern "vgscan|vghmi|vgdiscover|nsh>" | ForEach-Object { $_.Line }) -join "`n")

  $cmd = "vghmi scan -a $Range"
  if ($help -match "vgscan") { $cmd = "vgscan -a $Range" }

  Write-Output "=== $cmd ==="
  $port.DiscardInBuffer()
  $port.WriteLine($cmd)
  $buf = ""
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($port.BytesToRead -gt 0) {
      $chunk = $port.ReadExisting()
      $buf += $chunk
      Write-Host -NoNewline $chunk
    }
    if ($buf -match "(vgscan|vghmi scan): found \d+/\d+ slave" -and $buf -match "nsh>") {
      break
    }
    if ($buf -match "(vgscan|vghmi scan): failed" -and $buf -match "nsh>") {
      break
    }
    Start-Sleep -Milliseconds 120
  }

  Write-Output ""
  Write-Output "=== summary ==="
  if ($buf -match "(?:vgscan|vghmi scan): found (\d+)/(\d+) slave") {
    $n = [int]$Matches[1]
    $tot = [int]$Matches[2]
    Write-Output "hits=$n expected=$tot"
    $addrs = [regex]::Matches($buf, "addr=(\d+)") | ForEach-Object { [int]$_.Groups[1].Value }
    Write-Output ("addrs=" + ($addrs -join ","))
    $missing = @()
    for ($a = 1; $a -le 32; $a++) {
      if ($addrs -notcontains $a) { $missing += $a }
    }
    if ($missing.Count -gt 0) {
      Write-Output ("missing=" + ($missing -join ","))
    }
    if ($n -ge $tot) { exit 0 } else { exit 1 }
  }

  Write-Output "[FAIL] no result"
  Write-Output $buf
  exit 1
}
catch {
  Write-Error $_.Exception.Message
  exit 2
}
finally {
  if ($port.IsOpen) { $port.Close() }
}
