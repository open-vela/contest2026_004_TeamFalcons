[CmdletBinding()]
param(
  [string]$CubeCli = "",
  [string]$ExternalLoader = "",
  [string]$OutDir = "",
  [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

function Resolve-ExistingFile {
  param(
    [string]$ExplicitValue,
    [string]$EnvironmentName,
    [string[]]$Candidates,
    [string]$Description
  )

  $values = @($ExplicitValue, [Environment]::GetEnvironmentVariable($EnvironmentName)) + $Candidates
  foreach ($value in $values) {
    if (-not [string]::IsNullOrWhiteSpace($value) -and (Test-Path -LiteralPath $value -PathType Leaf)) {
      return (Resolve-Path -LiteralPath $value).Path
    }
  }

  throw "$Description was not found. Install STM32CubeProgrammer, pass the path explicitly, or set $EnvironmentName."
}

function Get-IntelHexAddressRange {
  param([Parameter(Mandatory = $true)][string]$Path)

  [int64]$baseAddress = 0
  $minAddress = $null
  [int64]$maxAddress = 0

  foreach ($line in Get-Content -LiteralPath $Path) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line[0] -ne ':') {
      continue
    }

    [int64]$byteCount = [Convert]::ToInt32($line.Substring(1, 2), 16)
    [int64]$offset = [Convert]::ToInt32($line.Substring(3, 4), 16)
    $recordType = [Convert]::ToInt32($line.Substring(7, 2), 16)

    switch ($recordType) {
      0 {
        [int64]$start = $baseAddress + $offset
        [int64]$end = $start + $byteCount
        if ($null -eq $minAddress -or $start -lt $minAddress) {
          $minAddress = $start
        }
        if ($end -gt $maxAddress) {
          $maxAddress = $end
        }
      }
      2 { $baseAddress = ([int64][Convert]::ToInt32($line.Substring(9, 4), 16)) -shl 4 }
      4 { $baseAddress = ([int64][Convert]::ToInt32($line.Substring(9, 4), 16)) -shl 16 }
    }
  }

  if ($null -eq $minAddress) {
    throw "No data records were found in Intel HEX file: $Path"
  }

  return [pscustomobject]@{ Min = [int64]$minAddress; Max = $maxAddress }
}

function Assert-Range {
  param(
    [string]$Label,
    [pscustomobject]$Range,
    [int64]$Minimum,
    [int64]$Maximum
  )

  if ($Range.Min -lt $Minimum -or $Range.Max -gt $Maximum) {
    throw ("{0} address range is invalid: 0x{1:x8}..0x{2:x8}; expected 0x{3:x8}..0x{4:x8}." -f $Label, $Range.Min, $Range.Max, $Minimum, ($Maximum - 1))
  }
}

if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $OutDir = [Environment]::GetEnvironmentVariable("OPENVELA_OUT_DIR")
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) ".debug"
}

$cubeCandidates = @(
  "D:\Develop\STM32CubeCLT\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
  (Join-Path $env:ProgramFiles "STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe")
)
$CubeCli = Resolve-ExistingFile $CubeCli "STM32_PROGRAMMER_CLI" $cubeCandidates "STM32CubeProgrammer CLI"

$loaderCandidates = @(
  (Join-Path (Split-Path -Parent $CubeCli) "ExternalLoader\MT25TL01G_STM32H750B-DISCO.stldr"),
  "D:\Develop\STM32CubeCLT\STM32CubeProgrammer\bin\ExternalLoader\MT25TL01G_STM32H750B-DISCO.stldr"
)
$ExternalLoader = Resolve-ExistingFile $ExternalLoader "STM32_EXTERNAL_LOADER" $loaderCandidates "STM32H750B-DK External Loader"

$mainHex = Join-Path $OutDir "nuttx.hex"
$stubHex = Join-Path $OutDir "qspi_bootstub.hex"
if (-not (Test-Path -LiteralPath $mainHex -PathType Leaf)) {
  throw "Main firmware was not found: $mainHex. Run 'openvela: Build' (contest scripts/build.sh) first. Download will not flash a previous image."
}
if (-not (Test-Path -LiteralPath $stubHex -PathType Leaf)) {
  throw "QSPI boot stub was not found: $stubHex. Re-run 'openvela: Build' so scripts/build.sh can recreate it. Download will not flash a previous image."
}

$mainRange = Get-IntelHexAddressRange $mainHex
$stubRange = Get-IntelHexAddressRange $stubHex
Assert-Range "Main QSPI image" $mainRange 0x90000000L 0x98000000L
Assert-Range "Internal boot stub" $stubRange 0x08000000L 0x08020000L

Write-Host ("Main QSPI image : 0x{0:x8}..0x{1:x8}" -f $mainRange.Min, $mainRange.Max)
Write-Host ("Internal stub   : 0x{0:x8}..0x{1:x8}" -f $stubRange.Min, $stubRange.Max)
Write-Host "External Loader : $ExternalLoader"

if ($ValidateOnly) {
  Write-Host "Validation completed; hardware was not accessed."
  exit 0
}

function Invoke-CubeStep {
  param(
    [string]$Label,
    [scriptblock]$Action
  )

  Write-Host $Label
  & $Action
  if ($LASTEXITCODE -ne 0) {
    Write-Host ("error: CubeProgrammer failed during '{0}' (exit {1}). If ST-LINK is occupied, close CubeProgrammer GUI and other OpenOCD, then retry." -f $Label, $LASTEXITCODE)
    exit $LASTEXITCODE
  }
}

Invoke-CubeStep "1/3 Programming and verifying external QSPI..." {
  & $CubeCli -c port=SWD mode=UR -el $ExternalLoader -d $mainHex -v
}
Invoke-CubeStep "2/3 Programming and verifying internal boot stub..." {
  & $CubeCli -c port=SWD mode=UR -d $stubHex -v
}
Invoke-CubeStep "3/3 Resetting target..." {
  & $CubeCli -c port=SWD mode=UR -rst
}

Write-Host "Flash completed: internal stub -> QSPI XIP image."
