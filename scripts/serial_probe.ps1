# Probe COM ports with optional DTR reset pulse
param(
  [string[]]$Ports = @("COM3", "COM6"),
  [int]$Baud = 115200
)
$ErrorActionPreference = "Stop"
foreach ($ComPort in $Ports) {
  Write-Host "`n########## $ComPort ##########"
  try {
    $port = New-Object System.IO.Ports.SerialPort
    $port.Encoding = [System.Text.Encoding]::UTF8
    $port.PortName = $ComPort
    $port.BaudRate = $Baud
    $port.ReadTimeout = 8000
    $port.DtrEnable = $false
    $port.RtsEnable = $false
    $port.Open()
    Start-Sleep -Milliseconds 200
    $port.DtrEnable = $true
    $port.RtsEnable = $true
    Start-Sleep -Milliseconds 100
    $port.DtrEnable = $false
    Start-Sleep -Milliseconds 150
    $port.DtrEnable = $true
    Start-Sleep -Milliseconds 300
    $boot = ""
    $deadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $deadline) {
      if ($port.BytesToRead -gt 0) { $boot += $port.ReadExisting() }
      if ($boot -match "nsh>" -or $boot -match "vela>" -or $boot -match "panic" -or $boot -match "openvela") { break }
      Start-Sleep -Milliseconds 120
    }
    Write-Host "BOOT len=$($boot.Length)"
    if ($boot.Length -gt 0) { Write-Host $boot }
    if ($boot -match "nsh>") {
      $port.Write("?`r")
      Start-Sleep -Seconds 2
      $help = ""
      while ($port.BytesToRead -gt 0) { $help += $port.ReadExisting(); Start-Sleep -Milliseconds 80 }
      Write-Host "HELP len=$($help.Length)"
      if ($help.Length -gt 0) { Write-Host $help.Substring(0, [Math]::Min(1200, $help.Length)) }
    }
    $port.Close()
  } catch {
    Write-Host "ERR: $($_.Exception.Message)"
  }
}
