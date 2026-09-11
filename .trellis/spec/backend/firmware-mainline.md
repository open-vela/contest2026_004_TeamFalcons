# VelaGuard contest firmware (one image)

## Scope / Trigger

Use when compiling, flashing, writing accept scripts, or telling judges which image to burn. Contest product firmware is a single preset.

## Contract

- Canonical target: `stm32h750b-dk:velaguard-lvgl` (`scripts/configs/velaguard-lvgl.defconfig`).
- Default command: `bash scripts/build.sh` (no args). Aliases `velaguard` and `net` normalize to `velaguard-lvgl`.
- VS Code / Cursor Build and Rebuild call `scripts/build.sh` with no args, so they follow the same default.
- Bring-up only (not the product image): `min`, `lvgl` (upstream demo), `ai-probe`, `emmc`.
- Do not tell judges or agents to flash a second `velaguard-net` image for demo, video, or Agent fallback.
- Boot must not autostart HMI and Agent together. After HMI and network are up, start Agent with `ai_agent --daemon`.

`.trellis/scripts/` is Trellis workflow (`task.py`, `get_context.py`). It has no firmware targets; this spec is the Trellis-side contract.

## Validation

```bash
bash scripts/build.sh
# expect: TARGET=velaguard-lvgl preset=stm32h750b-dk:velaguard-lvgl
```
