[CmdletBinding()]
param(
  [string]$WslDistro = "",
  [string]$OpenvelaDir = "",
  [string]$BoardConfig = "stm32h750b-dk:lvgl",
  [string]$OutDir = "",
  [switch]$DebugBuild,
  [switch]$UiPerfDiagnostics,
  [switch]$FastTouchPoll,
  [switch]$InterruptTouch,
  [switch]$DisableDma2d,
  [switch]$HidePerfMonitor,
  [ValidateSet("test", "production")]
  [string]$VelaGuardMode = "test",
  [string]$DeviceIdOverride = "vg-test-001",
  # incremental (default): reuse existing nuttx/.config, no make clean unless kconfig actually changes
  # full: re-run configure.sh and force make clean before build
  [ValidateSet("incremental", "full")]
  [string]$Rebuild = "incremental",
  [switch]$FullClean
)

$ErrorActionPreference = "Stop"

function Resolve-Setting {
  param(
    [string]$Value,
    [string]$EnvironmentName,
    [string]$DefaultValue
  )

  if (-not [string]::IsNullOrWhiteSpace($Value)) {
    return $Value
  }

  $environmentValue = [Environment]::GetEnvironmentVariable($EnvironmentName)
  if (-not [string]::IsNullOrWhiteSpace($environmentValue)) {
    return $environmentValue
  }

  return $DefaultValue
}

$WslDistro = Resolve-Setting $WslDistro "OPENVELA_WSL_DISTRO" "Debian"
$OutDir = Resolve-Setting $OutDir "OPENVELA_OUT_DIR" ""

if ($DeviceIdOverride.Length -ge 40 -or
    $DeviceIdOverride -notmatch '^[A-Za-z0-9_-]+$') {
  throw "DeviceIdOverride must be 1-39 characters using only letters, digits, '-' or '_'."
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
  throw "wsl.exe was not found. Enable WSL before running this task."
}

function Convert-ToWslPath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Description
  )

  if ($Path -match '^\\\\wsl(?:\.localhost|\$)\\([^\\]+)\\(.*)$') {
    return "/" + ($Matches[2] -replace '\\', '/')
  }

  $candidates = @(($Path -replace '\\', '/'), $Path) | Select-Object -Unique
  foreach ($candidate in $candidates) {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
      $converted = & wsl.exe -d $WslDistro -- wslpath -u $candidate 2>$null
      $exitCode = $LASTEXITCODE
    } finally {
      $ErrorActionPreference = $previousErrorActionPreference
    }

    if ($exitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($converted)) {
      return $converted.Trim()
    }
  }

  throw "Failed to convert $Description to a WSL path: $Path"
}

function Invoke-CheckedWslScript {
  param(
    [Parameter(Mandatory = $true)][string]$Content,
    [Parameter(Mandatory = $true)][string]$ScratchDir
  )

  $scriptPath = Join-Path $ScratchDir "openvela_windows_build.sh"
  $normalized = ($Content -replace "`r`n", "`n") -replace "`r", ""
  [System.IO.File]::WriteAllText(
    $scriptPath,
    $normalized,
    [System.Text.UTF8Encoding]::new($false)
  )

  $scriptWsl = Convert-ToWslPath $scriptPath "temporary build script"
  & wsl.exe -d $WslDistro -- bash $scriptWsl
  if ($LASTEXITCODE -ne 0) {
    throw "WSL build failed with exit code $LASTEXITCODE"
  }
}

$repoDir = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $OutDir = Join-Path $repoDir ".debug"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$repoDirWsl = Convert-ToWslPath $repoDir "contest repository"
$outDirWsl = Convert-ToWslPath $OutDir "artifact output directory"

if ([string]::IsNullOrWhiteSpace($OpenvelaDir)) {
  $OpenvelaDir = [Environment]::GetEnvironmentVariable("OPENVELA_ROOT_WSL")
}
if ([string]::IsNullOrWhiteSpace($OpenvelaDir)) {
  $OpenvelaDir = (& wsl.exe -d $WslDistro -- dirname $repoDirWsl).Trim()
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($OpenvelaDir)) {
    throw "Failed to derive the openvela root from $repoDirWsl"
  }
}

if ($FullClean) {
  $Rebuild = "full"
}

$debugBuildFlag = if ($DebugBuild) { "1" } else { "0" }
$uiPerfDiagnosticsFlag = if ($UiPerfDiagnostics) { "1" } else { "0" }
$uiPerfKind = if ($UiPerfDiagnostics) { "enabled" } else { "disabled" }
$interruptTouchFlag = if ($InterruptTouch) { "1" } else { "0" }
$touchMode = if ($InterruptTouch) { "interrupt" } else { "fast-poll" }
$dma2dFlag = if ($DisableDma2d) { "0" } else { "1" }
$renderBackend = if ($DisableDma2d) { "software" } else { "dma2d" }
$perfMonitorFlag = if ($HidePerfMonitor) { "0" } else { "1" }
$perfMonitorKind = if ($HidePerfMonitor) { "hidden" } else { "visible" }
$productModeFlag = if ($VelaGuardMode -eq "production") { "1" } else { "0" }
$buildKind = if ($DebugBuild) { "debug" } else { "release" }
$rebuildMode = $Rebuild
$buildCommand = @"
set -euo pipefail
OPENVELA_ROOT='$OpenvelaDir'
NUTTX_ROOT='${OpenvelaDir}/nuttx'
CONTEST_ROOT='$repoDirWsl'
OUT_ROOT='$outDirWsl'
REBUILD_MODE='$rebuildMode'
UI_PERF_DIAGNOSTICS='$uiPerfDiagnosticsFlag'
INTERRUPT_TOUCH='$interruptTouchFlag'
DMA2D_ENABLED='$dma2dFlag'
PERF_MONITOR_ENABLED='$perfMonitorFlag'

export PATH="`$OPENVELA_ROOT/prebuilts/tools/python/bin:`$OPENVELA_ROOT/prebuilts/tools/linux/x86_64:`$OPENVELA_ROOT/prebuilts/kconfig-frontends/bin:`$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:`$OPENVELA_ROOT/prebuilts/build-tools/linux-x86_64/bin:`$PATH"
export PYTHONPATH="`$OPENVELA_ROOT/prebuilts/tools/python/dist-packages/kconfiglib:`$OPENVELA_ROOT/prebuilts/tools/python/dist-packages:`${PYTHONPATH:-}"

bash "`$CONTEST_ROOT/scripts/ensure-openvela-links.sh" "`$OPENVELA_ROOT"
# packages/demos/Kconfig is generated and can predate a newly materialized
# manifest link.  Regenerate it through the normal apps helper before configure.
(
  cd "`$OPENVELA_ROOT/packages/demos"
  "`$OPENVELA_ROOT/apps/tools/mkkconfig.sh" -m Demos
)
bash "`$CONTEST_ROOT/scripts/apply-openvela-qspi-patch.sh" "`$OPENVELA_ROOT"
bash "`$CONTEST_ROOT/scripts/apply-openvela-eth-mii-patch.sh" "`$OPENVELA_ROOT"
bash "`$CONTEST_ROOT/scripts/apply-openvela-ui-performance-patch.sh" "`$OPENVELA_ROOT"
bash "`$CONTEST_ROOT/scripts/apply-openvela-display-acceleration-patch.sh" "`$OPENVELA_ROOT"

need_configure=0
if [ "`$REBUILD_MODE" = 'full' ]; then
  need_configure=1
elif [ ! -f "`$NUTTX_ROOT/.config" ]; then
  echo '[build] no nuttx/.config; running configure (first build)'
  need_configure=1
fi

if [ "`$need_configure" = '1' ]; then
  echo "[build] configure.sh -e $BoardConfig (rebuild=`$REBUILD_MODE)"
  "`$NUTTX_ROOT/tools/configure.sh" -e '$BoardConfig'
else
  echo '[build] incremental: reuse existing nuttx/.config (skip configure.sh)'
fi

before_config=`$(mktemp)
cp "`$NUTTX_ROOT/.config" "`$before_config"
kconfig-tweak --file "`$NUTTX_ROOT/.config" \
  --enable CONFIG_STM32H750B_DK_QSPI_BOOT \
  --enable CONFIG_LVX_USE_VELAGUARD \
  --disable CONFIG_LVX_USE_DEMO_CONTEST2026_004_VSCODE_LAB \
  --disable CONFIG_EXAMPLES_LVGLDEMO \
  --disable CONFIG_LV_BUILD_EXAMPLES \
  --disable CONFIG_LV_USE_DEMO_WIDGETS \
  --set-val CONFIG_LV_DEF_REFR_PERIOD 16 \
  --set-val CONFIG_LV_NUTTX_VSYNC_TIMER_PERIOD 16 \
  --set-val CONFIG_LVX_VELAGUARD_PRIORITY 120 \
  --enable CONFIG_SCHED_CPULOAD_SYSCLK \
  --disable CONFIG_SCHED_CPULOAD_NONE \
  --enable CONFIG_PSEUDOFS_FILE \
  --enable CONFIG_LV_FONT_MONTSERRAT_10 \
  --enable CONFIG_LV_FONT_MONTSERRAT_12 \
  --enable CONFIG_LV_FONT_MONTSERRAT_14 \
  --enable CONFIG_LV_FONT_MONTSERRAT_16 \
  --enable CONFIG_LV_FONT_MONTSERRAT_20 \
  --set-val CONFIG_VG_BUILD_MODE '$productModeFlag' \
  --set-str CONFIG_VG_FIRMWARE_VERSION '0.1.0' \
  --set-str CONFIG_INIT_ENTRYPOINT 'velaguard_main'

if [ "`$UI_PERF_DIAGNOSTICS" = '1' ]; then
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --enable CONFIG_VG_UI_PERF_DIAGNOSTICS
else
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --disable CONFIG_VG_UI_PERF_DIAGNOSTICS
fi

if [ "`$INTERRUPT_TOUCH" = '1' ]; then
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --disable CONFIG_FT5X06_POLLMODE
else
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --enable CONFIG_FT5X06_POLLMODE
fi

if [ "`$DMA2D_ENABLED" = '1' ]; then
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --enable CONFIG_VG_STM32H7_DMA2D
else
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --disable CONFIG_VG_STM32H7_DMA2D
fi

if [ "`$PERF_MONITOR_ENABLED" = '1' ]; then
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --enable CONFIG_LV_USE_SYSMON \
    --enable CONFIG_LV_USE_PERF_MONITOR \
    --disable CONFIG_LV_PERF_MONITOR_SERVICE_ONLY \
    --enable CONFIG_LV_PERF_MONITOR_ALIGN_TOP_LEFT \
    --disable CONFIG_LV_USE_PERF_MONITOR_LOG_MODE
else
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --disable CONFIG_LV_USE_PERF_MONITOR
fi

if [ '$productModeFlag' = '0' ]; then
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --set-str CONFIG_VG_DEVICE_ID_OVERRIDE '$DeviceIdOverride'
fi

if [ '$debugBuildFlag' = '1' ]; then
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --enable CONFIG_DEBUG_SYMBOLS \
    --set-str CONFIG_DEBUG_SYMBOLS_LEVEL '-g3' \
    --enable CONFIG_DEBUG_CUSTOMOPT \
    --set-str CONFIG_DEBUG_OPTLEVEL '-Og' \
    --disable CONFIG_DEBUG_NOOPT \
    --disable CONFIG_DEBUG_FULLOPT
else
  kconfig-tweak --file "`$NUTTX_ROOT/.config" \
    --disable CONFIG_DEBUG_SYMBOLS \
    --disable CONFIG_DEBUG_NOOPT \
    --enable CONFIG_DEBUG_FULLOPT
fi

# MB1381 H750XB-B01 onboard RJ45 (LAN8740A / MII) + DHCP bring-up.
# Keep this minimal: no Telnet/webclient; NETINIT_THREAD so no-cable boot
# does not block UI/NSH.  The PHY provides the external MII clocks.
kconfig-tweak --file "`$NUTTX_ROOT/.config" \
  --enable CONFIG_NET \
  --enable CONFIG_NET_ETHERNET \
  --enable CONFIG_NET_IPv4 \
  --enable CONFIG_NET_ARP \
  --enable CONFIG_NET_TCP \
  --enable CONFIG_NET_UDP \
  --enable CONFIG_NET_BROADCAST \
  --enable CONFIG_NET_UDP_CHECKSUMS \
  --enable CONFIG_NET_ICMP \
  --enable CONFIG_NET_ICMP_SOCKET \
  --enable CONFIG_NET_SOCKOPTS \
  --set-val CONFIG_NET_ETH_PKTSIZE 1500 \
  --enable CONFIG_STM32H7_ETHMAC \
  --enable CONFIG_STM32H7_MII \
  --enable CONFIG_STM32H7_MII_EXTCLK \
  --enable CONFIG_ETH0_PHY_LAN8740A \
  --disable CONFIG_ETH0_PHY_LAN8742A \
  --set-val CONFIG_STM32H7_PHYADDR 1 \
  --enable CONFIG_STM32H7_AUTONEG \
  --set-val CONFIG_STM32H7_PHYSR 31 \
  --enable CONFIG_STM32H7_PHYSR_ALTCONFIG \
  --set-val CONFIG_STM32H7_PHYSR_ALTMODE 0x001c \
  --set-val CONFIG_STM32H7_PHYSR_10HD 0x0004 \
  --set-val CONFIG_STM32H7_PHYSR_100HD 0x0008 \
  --set-val CONFIG_STM32H7_PHYSR_10FD 0x0014 \
  --set-val CONFIG_STM32H7_PHYSR_100FD 0x0018 \
  --enable CONFIG_SCHED_LPWORK \
  --enable CONFIG_NSH_NETINIT \
  --enable CONFIG_NETUTILS_NETINIT \
  --enable CONFIG_NETUTILS_DHCPC \
  --set-val CONFIG_NETUTILS_DHCPC_BOOTP_FLAGS 0x8000 \
  --enable CONFIG_NETUTILS_CJSON \
  --enable CONFIG_NETDB_DNSCLIENT \
  --enable CONFIG_NETINIT_DNS \
  --enable CONFIG_NETINIT_DHCPC \
  --enable CONFIG_NETINIT_THREAD \
  --disable CONFIG_NETINIT_MONITOR \
  --enable CONFIG_NETINIT_CARRIER_POLL \
  --set-val CONFIG_NETINIT_CARRIER_POLL_MSEC 500 \
  --set-val CONFIG_NETINIT_DHCP_RETRYMSEC 5000 \
  --enable CONFIG_NETINIT_NOMAC \
  --enable CONFIG_NETINIT_SWMAC \
  --set-val CONFIG_NETINIT_MACADDR_2 0x00e0 \
  --set-val CONFIG_NETINIT_MACADDR_1 0xde00a750 \
  --enable CONFIG_SYSTEM_PING \
  --enable CONFIG_SYSTEM_DHCPC_RENEW

make -C "`$NUTTX_ROOT" olddefconfig

force_clean=0
if [ "`$REBUILD_MODE" = 'full' ]; then
  force_clean=1
  echo '[build] full rebuild: make clean'
elif ! cmp -s "`$before_config" "`$NUTTX_ROOT/.config"; then
  force_clean=1
  echo '[build] .config changed after kconfig-tweak; make clean'
else
  echo '[build] .config unchanged; incremental make (no clean)'
fi
rm -f "`$before_config"

if [ "`$force_clean" = '1' ]; then
  make -C "`$NUTTX_ROOT" clean
fi

make -C "`$NUTTX_ROOT" -j`$(nproc)

# The legacy apps Make flow follows the workspace symlink and leaves these
# generated markers beside the team-owned source.  Keep the contest repository
# source-only after every build.
rm -f "`$CONTEST_ROOT/app/hello_app/.built" \
      "`$CONTEST_ROOT/app/hello_app/.depend" \
      "`$CONTEST_ROOT/app/hello_app/Make.dep" \
      "`$CONTEST_ROOT/app/velaguard_app/.built" \
      "`$CONTEST_ROOT/app/velaguard_app/.depend" \
      "`$CONTEST_ROOT/app/velaguard_app/Make.dep" \
      "`$CONTEST_ROOT/app/hello_app/"*.o \
      "`$CONTEST_ROOT/app/velaguard_app/"*.o \
      "`$CONTEST_ROOT/app/velaguard_app/src/"*.o

bootstub_dir="`$OUT_ROOT/qspi_boot_stub"
bash "`$CONTEST_ROOT/scripts/qspi_boot_stub/build_bootstub.sh" "`$bootstub_dir"

mkdir -p "`$OUT_ROOT"
cp "`$NUTTX_ROOT/nuttx" "`$OUT_ROOT/nuttx.elf"
cp "`$NUTTX_ROOT/nuttx.hex" "`$OUT_ROOT/nuttx.hex"
cp "`$NUTTX_ROOT/nuttx.bin" "`$OUT_ROOT/nuttx.bin"
cp "`$bootstub_dir/qspi_bootstub.elf" "`$OUT_ROOT/qspi_bootstub.elf"
cp "`$bootstub_dir/qspi_bootstub.hex" "`$OUT_ROOT/qspi_bootstub.hex"
cp "`$bootstub_dir/qspi_bootstub.bin" "`$OUT_ROOT/qspi_bootstub.bin"
cp "`$NUTTX_ROOT/.config" "`$OUT_ROOT/nuttx.config"
printf 'product_mode=%s\ncompiler_mode=%s\nui_perf=%s\ntouch_mode=%s\nrender_backend=%s\nperf_monitor=%s\n' \
  '$VelaGuardMode' '$buildKind' '$uiPerfKind' '$touchMode' \
  '$renderBackend' '$perfMonitorKind' \
  > "`$OUT_ROOT/build-info.txt"

"`$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-size" "`$OUT_ROOT/nuttx.elf" "`$OUT_ROOT/qspi_bootstub.elf"
"@

Write-Host "Building VelaGuard $VelaGuardMode/$buildKind QSPI-XIP firmware in WSL distro '$WslDistro' (rebuild=$rebuildMode, ui_perf=$UiPerfDiagnostics, monitor=$perfMonitorKind, render=$renderBackend, touch=$touchMode)."
Write-Host "openvela root: $OpenvelaDir"
Write-Host "artifacts: $OutDir"
Invoke-CheckedWslScript $buildCommand $OutDir
Write-Host "Build completed: $OutDir (rebuild=$rebuildMode)"
