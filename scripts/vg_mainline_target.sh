# Canonical contest firmware: stm32h750b-dk:velaguard-lvgl
# Usage: TARGET=$(vg_normalize_build_target "${1:-}")
vg_normalize_build_target() {
  local t="${1:-}"
  case "$t" in
    ""|velaguard|velaguard-lvgl|net)
      if [ "$t" = "net" ]; then
        echo "[build] 作品主线是 velaguard-lvgl；net 已并入该预设" >&2
      fi
      printf '%s\n' "velaguard-lvgl"
      ;;
    *)
      printf '%s\n' "$t"
      ;;
  esac
}
