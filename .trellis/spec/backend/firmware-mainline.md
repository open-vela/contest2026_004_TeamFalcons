# VelaGuard contest firmware (one image)

## Scope / Trigger

Use when compiling, flashing, writing accept scripts, or telling judges which image to burn. Contest product firmware is a single preset.

## Contract

- Canonical target: `stm32h750b-dk:velaguard-lvgl` (`scripts/configs/velaguard-lvgl.defconfig`).
- Default command: `bash scripts/build.sh` (no args). Aliases `velaguard` and `net` normalize to `velaguard-lvgl`.
- VS Code / Cursor Build and Rebuild call `scripts/build.sh` with no args, so they follow the same default.
- Bring-up only (not the product image): `min`, `lvgl` (upstream demo), `ai-probe`, `emmc`.
- Do not tell judges or agents to flash a second `velaguard-net` image for demo, video, or Agent fallback.
- Boot chain: NSH → net_mgr → HMI autostart → Agent autostart (`CONFIG_VG_AGENT_AUTOSTART=y`, starts `ai_agent --daemon` 3s after the HMI task). Do not instruct judges or scripts to start the Agent manually; `ai_agent` (no flag) attaches an interactive CLI and re-running `ai_agent --daemon` is safe (reentry guard).

`.trellis/scripts/` is Trellis workflow (`task.py`, `get_context.py`). It has no firmware targets; this spec is the Trellis-side contract.

## Validation

```bash
bash scripts/build.sh
# expect: TARGET=velaguard-lvgl preset=stm32h750b-dk:velaguard-lvgl
```

## Measurement builds (CONFIG_VG_HMI_PERF)

`bash scripts/build.sh --hmi-perf` builds the same single `velaguard-lvgl` mainline with the bounded HMI performance counters enabled (task 09-13-hmi-performance-baseline). It is a temporary measurement configuration, not a second product image:

- Only valid for `velaguard-lvgl`; other targets are rejected.
- The flag is injected into the installed `.config` at build time; the source defconfig stays off, and a plain default build rejects the leftover `=y` and reconfigures itself back to the closed state.
- `vghmi perf` on the measurement firmware is a read-only NSH diagnostic; it never starts a second HMI.
- After measuring, run `bash scripts/build.sh` and flash the default image to restore the product firmware.
