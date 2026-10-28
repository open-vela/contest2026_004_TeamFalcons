# Capture the full NSH boot log on the ST-LINK VCP while resetting the target over SWD.
#
# The board prints its boot banner once and NSH does not buffer it, so the port has
# to be open *before* the reset. This opens COM3 first, triggers a SWD reset through
# STM32CubeProgrammer, then drains everything the board prints.
#
# Usage (from WSL, using the absolute powershell.exe path):
#   powershell.exe -NoProfile -ExecutionPolicy Bypass `
#     -File scripts/serial_boot_capture.ps1 -ComPort COM3 -Seconds 40 -Out .debug/boot.log
#
# -NoReset captures without touching the target (useful right after flash.ps1).
param(
  [string]$ComPort = "COM3",
  [int]$Baud = 115200,
  [int]$Seconds = 40,
  [string]$Out = "",
  [switch]$NoReset,
  [string]$CubeCli = ""
)

$ErrorActionPreference = "Stop"

function Resolve-CubeCli {
  param([string]$ExplicitValue)
  $candidates = @()
  if (-not [string]::IsNullOrWhiteSpace($ExplicitValue)) { $candidates += $ExplicitValue }
  $envValue = [Environment]::GetEnvironmentVariable("STM32_PROGRAMMER_CLI")
  if (-not [string]::IsNullOrWhiteSpace($envValue)) { $candidates += $envValue }
  Get-ChildItem -Path "D:\Develop" -Filter "STM32CubeCLT*" -Directory -ErrorAction SilentlyContinue |
    ForEach-Object { $candidates += (Join-Path $_.FullName "STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe") }
  $candidates += "D:\Develop\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
  $candidates += "D:\Develop\STM32CubeCLT\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
  $candidates += (Join-Path $env:ProgramFiles "STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe")
  foreach ($c in $candidates) {
    if (-not [string]::IsNullOrWhiteSpace($c) -and (Test-Path -LiteralPath $c -PathType Leaf)) {
      return (Resolve-Path -LiteralPath $c).Path
    }
  }
  throw "STM32CubeProgrammer CLI was not found. Pass -CubeCli or set STM32_PROGRAMMER_CLI."
}

$writer = $null
if (-not [string]::IsNullOrWhiteSpace($Out)) {
  $writer = New-Object System.IO.StreamWriter($Out, $false, [System.Text.UTF8Encoding]::new($false))
  $writer.AutoFlush = $true
}
function Emit([string]$text) {
  Write-Host -NoNewline $text
  if ($null -ne $writer) { $writer.Write($text) }
}

$port = New-Object System.IO.Ports.SerialPort $ComPort, $Baud
$port.Encoding = [System.Text.Encoding]::UTF8
$port.ReadTimeout = 500
$port.DtrEnable = $true
$port.RtsEnable = $true
$port.Open()
Write-Host "opened $ComPort @ $Baud, capture ${Seconds}s, reset=$(-not $NoReset)"

$resetNote = "no reset requested"
if (-not $NoReset) {
  $cli = Resolve-CubeCli $CubeCli
  Write-Host "resetting target via $cli"
  $resetOut = & $cli -c port=SWD mode=UR -rst 2>&1 | Out-String
  $resetNote = if ($LASTEXITCODE -eq 0) { "SWD reset OK" } else { "SWD reset FAILED exit=$LASTEXITCODE" }
  Write-Host $resetNote
}

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
  try {
    if ($port.BytesToRead -gt 0) { Emit $port.ReadExisting() }
  } catch { }
  Start-Sleep -Milliseconds 100
}
try { if ($port.BytesToRead -gt 0) { Emit $port.ReadExisting() } } catch { }
$port.Close()
if ($null -ne $writer) { $writer.Close() }

Write-Host ""
Write-Host "capture done: $resetNote"
