# Component Guidelines

> How components are built in this project.

---

## Overview

<!--
Document your project's component conventions here.

Questions to answer:
- What component patterns do you use?
- How are props defined?
- How do you handle composition?
- What accessibility standards apply?
-->

(To be filled by the team)

---

## Component Structure

<!-- Standard structure of a component file -->

(To be filled by the team)

---

## Props Conventions

<!-- How props should be defined and typed -->

(To be filled by the team)

---

## Styling Patterns

<!-- How styles are applied (CSS modules, styled-components, Tailwind, etc.) -->

(To be filled by the team)

---

## Accessibility

<!-- A11y requirements and patterns -->

(To be filled by the team)

---

## Common Mistakes

<!-- Component-related mistakes your team has made -->

### Common Mistake: plain `lv_obj` containers swallow taps meant for a clickable parent card

**Symptom**: A clickable card row (e.g. an alarm row with an `on_row_click` handler) never receives `LV_EVENT_CLICKED`; buttons inside it still work. The row shows only its default/derived state.

**Cause**: In LVGL 9, objects created with `lv_obj_create()` carry `LV_OBJ_FLAG_CLICKABLE` by default. A plain container laid out inside the card (a header line, an inner flex box) is itself clickable, sits on top of the card, and intercepts the press-release, so the card handler never fires.

**Fix**: Clear the flag on every plain container inside a clickable card:

```c
line1 = lv_obj_create(row);
lv_obj_remove_flag(line1, LV_OBJ_FLAG_SCROLLABLE);
/* plain containers are clickable by default in LVGL 9; let taps fall
 * through to the row card so the whole row selects */
lv_obj_clear_flag(line1, LV_OBJ_FLAG_CLICKABLE);
```

Also clear it on composite widgets that are not meant to be tapped (e.g. a `vg_status_chip` inside the row). Buttons (`lv_button`) keep their own handlers and stay clickable.

**Prevention**: When building a tappable card from nested containers, list every child container and either give it a handler or clear `LV_OBJ_FLAG_CLICKABLE`. Verify with the headless render harness (`gui/headless/alarm_check_main.c`), which drives the real pointer indev and asserts the state change actually happened.

---

### Pattern: headless render harness for LVGL pages (no SDL, no board)

**Problem**: The SDL simulator needs libsdl2 (absent in WSL) and board acceptance cannot show pixel-level UI state.

**Solution**: `gui/headless/alarm_check_main.c` builds the real UI into a 480x272 off-screen buffer, drives a scripted pointer indev (clicks located by widget text, so they survive layout changes), dumps frames as PPM (convert to PNG for inspection), and prints model-level PASS/FAIL lines. It is excluded from both the firmware build (app/velaguard Makefile globs `gui/main/ui/{app,shell,pages,model,theme,widgets,fonts}` only) and the SDL sim (CMake globs `main/ui` only).

Key implementation facts (LVGL 9.3):
- `lv_color_t` is always 3-byte RGB888 now; the display buffer is XRGB8888 (4 bytes). In `flush_cb` decode with the display color format and `lv_draw_buf_width_to_stride()` — do not iterate `lv_color_t` over the render buffer.
- `lv_indev_create()` takes no arguments in 9.3; use `lv_indev_set_type()` + `lv_indev_set_read_cb()`.

**When to use**: any page-layout or interaction change to `gui/main/ui` before flashing the board.

**Standalone build (since 09-13-hmi-performance-baseline)**: `gui/headless/CMakeLists.txt` builds these checks without SDL and runs them under CTest. Knobs: `VG_LVGL_DIR` (default: firmware checkout `../apps/graphics/lvgl/lvgl`), `VG_HEADLESS_COLOR_DEPTH` (16=RGB565 board-like / 32=PC), `VG_HEADLESS_PROFILE` (pc or board capacity via the test-only `VG_MODEL_BOARD_CAPACITY` macro). Shared plumbing lives in `harness_common.c` (format-aware buffer sizing and RGB565/XRGB8888 decode — never `sizeof(lv_color_t)`); `fixture_check_main.c` imports deterministic point tables through `vg_model_import_runtime_points` / `vg_model_set_live`.

```bash
cmake -S gui/headless -B .debug/hmi-headless -DVG_LVGL_DIR="$PWD/../apps/graphics/lvgl/lvgl" -DVG_HEADLESS_COLOR_DEPTH=16
cmake --build .debug/hmi-headless && ctest --test-dir .debug/hmi-headless --output-on-failure
```

Both LVGL checkouts report version 9.1 via `lvgl.h` macros (the spec text above mentioning 9.3 predates this verification); verify API facts against the actual checkout.

### Common Mistake: quantized histogram keyed into a too-small array

**Symptom**: a "compare pixel distribution" helper corrupts unrelated globals and the crash surfaces later inside LVGL (e.g. in style/event code), pointing at the wrong module.

**Cause**: packing 8-bit RGB into a 4-bit-per-channel key produces 4096 values; indexing a 256-entry histogram silently writes past the array.

**Fix**: size the histogram for the full key space (4096) or reduce the key to 8 bits. Caught by ASAN as global-buffer-overflow — run harnesses under `-fsanitize=address` when they crash far from the changed code.

### Common Mistake: trend full-window assertion without switching the window

**Symptom**: headless trend assertions expect `当前 <VG_HISTORY_LEN> 点` but read `当前 60 点`.

**Cause**: the trend page defaults to the 60-sample recent window; the full-window label only appears after clicking the `全部` toggle. Full-window sample counts follow `VG_HISTORY_LEN` (300 PC / 128 board capacity), so assert against the macro, not a literal.
