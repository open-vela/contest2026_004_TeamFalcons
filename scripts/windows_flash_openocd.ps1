[CmdletBinding()]
param(
  [string]$CubeCli = "",
  [string]$ExternalLoader = "",
  [string]$OutDir = "",
  [switch]$NoBuild,
  [switch]$DebugBuild,
  [switch]$UiPerfDiagnostics,
  [switch]$FastTouchPoll,
  [switch]$InterruptTouch,
  [switch]$DisableDma2d,
  [switch]$HidePerfMonitor,
  [ValidateSet("test", "production")]
  [string]$VelaGuardMode = "test",
  [string]$DeviceIdOverride = "vg-test-001",
  [ValidateSet("incremental", "full")]
  [string]$Rebuild = "incremental",
  [switch]$FullClean,
  [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"
Write-Warning "OpenOCD is debug-only for STM32H750B-DK dual QSPI. Redirecting the flash request to CubeProgrammer."

$parameters = @{}
if (-not [string]::IsNullOrWhiteSpace($CubeCli)) { $parameters["CubeCli"] = $CubeCli }
if (-not [string]::IsNullOrWhiteSpace($ExternalLoader)) { $parameters["ExternalLoader"] = $ExternalLoader }
if (-not [string]::IsNullOrWhiteSpace($OutDir)) { $parameters["OutDir"] = $OutDir }
if ($NoBuild) { $parameters["NoBuild"] = $true }
if ($DebugBuild) { $parameters["DebugBuild"] = $true }
if ($UiPerfDiagnostics) { $parameters["UiPerfDiagnostics"] = $true }
if ($FastTouchPoll) { $parameters["FastTouchPoll"] = $true }
if ($InterruptTouch) { $parameters["InterruptTouch"] = $true }
if ($DisableDma2d) { $parameters["DisableDma2d"] = $true }
if ($HidePerfMonitor) { $parameters["HidePerfMonitor"] = $true }
$parameters["VelaGuardMode"] = $VelaGuardMode
$parameters["DeviceIdOverride"] = $DeviceIdOverride
$parameters["Rebuild"] = $Rebuild
if ($FullClean) { $parameters["FullClean"] = $true }
if ($ValidateOnly) { $parameters["ValidateOnly"] = $true }

& (Join-Path $PSScriptRoot "windows_flash_cube.ps1") @parameters
exit $LASTEXITCODE
