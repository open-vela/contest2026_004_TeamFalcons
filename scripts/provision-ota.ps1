[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$BlobPath,
  [string]$ComPort='COM3'
)
$ErrorActionPreference='Stop'
$otaBlob=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $BlobPath).ProviderPath)
if ($otaBlob.Length -lt 52 -or $otaBlob.Length -gt 1076) { throw 'Invalid OTA credential envelope length.' }
$otaPort=New-Object IO.Ports.SerialPort $ComPort,115200
$otaPort.Encoding=[Text.Encoding]::UTF8
$otaPort.ReadTimeout=250
$otaPort.DtrEnable=$true
$otaPort.RtsEnable=$true
function Invoke-OtaCommand([string]$Command,[string]$Expected) {
  $otaPort.DiscardInBuffer()
  $otaPort.Write("$Command`r")
  $otaUntil=(Get-Date).AddSeconds(8)
  $otaReply=''
  while ((Get-Date) -lt $otaUntil) {
    if ($otaPort.BytesToRead -gt 0) {
      $otaReply+=$otaPort.ReadExisting()
      if ($otaReply -match $Expected -and $otaReply -match 'nsh>') { return }
    }
    Start-Sleep -Milliseconds 40
  }
  # Do not dump echoed import data, unrelated device output or private fields.
  throw 'OTA provisioning command was not acknowledged; runtime configuration was not confirmed.'
}
try {
  $otaPort.Open()
  Invoke-OtaCommand 'vgota provision begin' 'vgota: encrypted import started'
  for ($otaOffset=0;$otaOffset -lt $otaBlob.Length;$otaOffset+=32) {
    $otaCount=[Math]::Min(32,$otaBlob.Length-$otaOffset)
    $otaHex=([BitConverter]::ToString($otaBlob,$otaOffset,$otaCount)).Replace('-','').ToLowerInvariant()
    Invoke-OtaCommand "vgota provision put $otaHex" 'vgota: encrypted chunk stored'
  }
  Invoke-OtaCommand 'vgota provision commit' 'vgota: encrypted credentials committed'
  Invoke-OtaCommand 'vgota provision status' 'vgota: provision configured=1'
  Write-Output 'OTA encrypted credentials committed and device binding verified. Running service changes require restart.'
} finally {
  if ($otaPort.IsOpen) { $otaPort.Close() }
  [Array]::Clear($otaBlob,0,$otaBlob.Length)
}
