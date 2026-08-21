[CmdletBinding()]
param(
  [string]$WslDistro = ""
)

$ErrorActionPreference = "Stop"

$repoDir = Split-Path -Parent $PSScriptRoot
$repoDirWsl = ""

if ($repoDir -match '^\\\\wsl(?:\.localhost|\$)\\([^\\]+)\\(.*)$') {
  $uncDistro = $Matches[1]
  $repoDirWsl = "/" + ($Matches[2] -replace '\\', '/')
  if ([string]::IsNullOrWhiteSpace($WslDistro)) {
    $WslDistro = $uncDistro
  }
}

if ([string]::IsNullOrWhiteSpace($WslDistro)) {
  $WslDistro = [Environment]::GetEnvironmentVariable("OPENVELA_WSL_DISTRO")
}
if ([string]::IsNullOrWhiteSpace($WslDistro)) {
  $WslDistro = "Debian"
}

if ([string]::IsNullOrWhiteSpace($repoDirWsl)) {
  if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "wsl.exe was not found. Enable WSL before reopening this workspace."
  }

  $repoDirWsl = (& wsl.exe -d $WslDistro -- wslpath -u $repoDir).Trim()
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repoDirWsl)) {
    throw "Failed to convert the repository path to WSL: $repoDir"
  }
}

$cursorCmd = $null
foreach ($candidate in @("cursor", "cursor.cmd")) {
  if (Get-Command $candidate -ErrorAction SilentlyContinue) {
    $cursorCmd = $candidate
    break
  }
}
if (-not $cursorCmd) {
  throw "The Cursor 'cursor' command was not found in PATH. Re-run the Cursor installer with 'Add to PATH' enabled."
}

Write-Host "Opening $repoDirWsl in Cursor Remote - WSL ($WslDistro)..."
Push-Location $env:SystemRoot
try {
  & $cursorCmd --new-window --remote "wsl+$WslDistro" $repoDirWsl
  $cursorExit = $LASTEXITCODE
} finally {
  Pop-Location
}
if ($cursorExit -ne 0) {
  throw "Cursor failed to open the Remote - WSL workspace (exit $cursorExit)."
}
