# Read LTDC error counters without stopping or resetting the running board.
# Requires the matching instrumented firmware and its ELF in .debug/.
param(
  [ValidateRange(1, 3600)][int]$Seconds = 30,
  [ValidateRange(100, 10000)][int]$IntervalMs = 1000,
  [string]$OpenOcd = 'D:\Develop\xpack-openocd-0.12.0-7\bin\openocd.exe',
  [string]$Elf = '',
  [string]$LogPath = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$Elf) { $Elf = Join-Path $root '.debug\nuttx.elf' }
if (!$LogPath) { $LogPath = Join-Path $root '.debug\ltdc-observe.log' }
$nm = (Get-Command arm-none-eabi-nm.exe -ErrorAction Stop).Source
$symbols = & $nm --defined-only $Elf
if ($LASTEXITCODE -ne 0) { throw 'Cannot read ELF symbols' }
$symbol = @($symbols | Where-Object { $_ -match '\bg_ltdc_diag$' })
if ($symbol.Count -ne 1) { throw 'ELF has no unique LTDC diagnostic symbol' }
$address = '0x' + ($symbol[0] -split '\s+')[0]
$samples = [int][Math]::Ceiling($Seconds * 1000.0 / $IntervalMs)
$ocdRoot = Split-Path -Parent (Split-Path -Parent $OpenOcd)
$ocdScripts = Join-Path $ocdRoot 'openocd\scripts'
$probe = Join-Path $PSScriptRoot 'gdb\ltdc-observe.tcl'
foreach ($item in @($OpenOcd, $Elf, $probe, $ocdScripts)) {
  if (!(Test-Path -LiteralPath $item)) { throw "Missing path: $item" }
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
$LogPath = [System.IO.Path]::GetFullPath($LogPath)
"start=$([DateTimeOffset]::Now.ToString('o')) elf_sha256=$((Get-FileHash -LiteralPath $Elf -Algorithm SHA256).Hash)" |
  Set-Content -LiteralPath $LogPath -Encoding UTF8
# Windows PowerShell treats native stderr as ErrorRecord; OpenOCD uses it for
# normal startup output as well. Exit status and the final result are checked.
$ErrorActionPreference = 'Continue'
& $OpenOcd -s $ocdScripts -f interface/stlink.cfg -c 'transport select swd' `
  -f target/stm32h7x.cfg -c 'reset_config none' -c 'adapter speed 1000' `
  -c 'init' -c "set vg_diag_addr $address" `
  -c "set vg_diag_samples $samples" -c "set vg_diag_ms $IntervalMs" `
  -f $probe -c shutdown 2>&1 | ForEach-Object {
    $line = $_.ToString()
    [System.IO.File]::AppendAllText($LogPath, $line + [Environment]::NewLine,
      (New-Object System.Text.UTF8Encoding $false))
    Write-Output $line
  }
$rc = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($rc -ne 0 -or !(Select-String -LiteralPath $LogPath -SimpleMatch 'vg-ltdc result')) {
  throw "LTDC observation failed (OpenOCD exit $rc)"
}
