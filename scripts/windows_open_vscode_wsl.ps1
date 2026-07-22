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

if (-not (Get-Command code -ErrorAction SilentlyContinue)) {
  throw "The VS Code 'code' command was not found in PATH. Re-run the VS Code installer with 'Add to PATH' enabled."
}

Write-Host "Opening $repoDirWsl in VS Code Remote - WSL ($WslDistro)..."
Push-Location $env:SystemRoot
try {
  & code --new-window --remote "wsl+$WslDistro" $repoDirWsl
  $codeExit = $LASTEXITCODE
} finally {
  Pop-Location
}
if ($codeExit -ne 0) {
  throw "VS Code failed to open the Remote - WSL workspace (exit $codeExit)."
}
