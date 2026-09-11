# Implement: Keil-style Build / Rebuild / Download / Debug

## Checklist

1. `git mv scripts/build_minimal.sh scripts/build.sh`，更新内部日志前缀与用法注释；
   按 `design.md` 在 configure 前接入 links / 目标 apply / bootstub 自动构建。
2. `git mv scripts/windows_flash_cube.ps1 scripts/flash.ps1`，删除全部构建参数和对
   `windows_build_openvela.ps1` 的调用；只留校验 + Cube 三步烧录。
3. 新增 `scripts/flash.sh`（WSL → powershell.exe → `flash.ps1`），失败信息可行动。
4. 删除 `scripts/windows_build_openvela.ps1`。
5. 更新 `.vscode/tasks.json`：任务标签 Build / Rebuild / Download / Build & Download；
   linux 与 windows 的 Build/Rebuild 都调 `build.sh`；Download 在 linux 调 `flash.sh`，
   windows 可直接调 `flash.ps1`。
6. 更新 `.vscode/launch.json`：默认 `openvela: Debug`（只 attach）+ 可选
   `openvela: Debug (Download first)`。
   若 Cortex-Debug 对 `preLaunchTask` 数组支持差，改成复合 task。
7. 更新操作文档：`docs/windows_build_debug_setup.md`、`docs/windows_vscode_coding_setup.md`、
   `docs/velaguard-bringup-known-issues.md` 的脚本/任务入口；写明 Rebuild 复位预设、
   F5 不自动编译、与上游 `./build.sh` 的区别。
8. 全仓检索日常入口：`build_minimal.sh`、`windows_build_openvela.ps1`、
   `windows_flash_cube.ps1`、已消失的旧任务名。操作路径必须清掉；`docs/learn/` 历史叙述可留。

## Validation

不在本步做完整固件编译（除非用户在实现阶段要求）。最低检查：

```bash
# 脚本可执行、帮助/用法不引用旧名
bash -n scripts/build.sh scripts/flash.sh scripts/wsl_kill_openocd.sh

# 日常入口检索（操作文档与 vscode 应为 0）
rg -n 'build_minimal\.sh|windows_build_openvela\.ps1|windows_flash_cube\.ps1' \
  scripts .vscode docs/windows_build_debug_setup.md docs/windows_vscode_coding_setup.md \
  docs/velaguard-bringup-known-issues.md

# 无文件
test ! -e scripts/windows_build_openvela.ps1
test ! -e scripts/build_minimal.sh
test -x scripts/build.sh && test -f scripts/flash.ps1 && test -f scripts/flash.sh
```

硬件验收（用户执行，实现后）：

- Ctrl+Shift+B → Build 成功，`.debug/nuttx.hex` 时间戳更新。
- 任务 Rebuild → 日志出现重新 configure 所选预设，编译通过。
- 任务 Download → Cube 三步完成，板子复位。
- F5 Debug → 只 attach，不烧录；`openvela: Debug (Download first)` 才先烧再 attach。
- 故意清空 `.debug/nuttx.hex` 后 Download 失败且不烧旧文件。

## Risky files / rollback

- `.vscode/launch.json`：F5 选错配置会烧录或占 ST-LINK。默认项必须是 Debug（只 attach）。
- `scripts/flash.ps1`：去掉构建参数后，任何残留调用旧开关都会直接报错，这是预期。
- `scripts/build.sh` 误 apply 到错误目标（例如 net 打上 UI patch）会污染预设。按设计表调用。

回滚：还原本任务触及的 `scripts/` + `.vscode/` + 三份操作文档。

## Follow-up before `task.py start`

- [x] `prd.md` 已过收敛（无未决 Open Questions）
- [x] `design.md` / `implement.md` 已写
- [x] `implement.jsonl` / `check.jsonl` 有真实 spec/research 条目
- [ ] 用户明确批准本规划摘要后才能 `task.py start`
