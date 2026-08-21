# Implement: VelaGuard minimal bring-up config (velaguard-min)

## Ordered checklist

1. 备份：`cp nuttx/.config nuttx/.config.bak.velaguard-min-20260813`。
2. app 层解绑（队伍仓直接改）：
   - `app/velaguard/Kconfig`：新增 `VG_BRINGUP_TOOLS`（default 跟随 LVX demo、select ARCH_HAVE_LEDS）。
   - `app/velaguard/Makefile`：`MODULE = $(CONFIG_VG_BRINGUP_TOOLS)`。
   - `app/velaguard/CMakeLists.txt`：`if(CONFIG_VG_BRINGUP_TOOLS)`。
3. 验证 Kconfig 默认链：临时把 LVX demo=y 且 VG 未设 → `make olddefconfig` → VG 变 y（AC6 前置）；
   若 kconfiglib 不支持符号引用，改 `default y if LVX_USE_DEMO_...` 重试。
4. 配置手术（kconfig-tweak）：
   - 关闭 R2 清单全部符号（LVGL 树、LVX demo、EXAMPLES_FB、VIDEO_FB、LTDC 树、INPUT/FT5X06 树、
     DEV_URANDOM 树、NET/NETINIT/NET_*、SYSTEM_PING、SYSTEM_DHCPC_RENEW、LV perf/sysmon）。
   - `--set-str CONFIG_INIT_ENTRYPOINT "velaguard_app_main"`（并同步
     `CONFIG_INIT_ENTRYNAME`，避免残留 "nsh_main"）；`--enable CONFIG_VG_BRINGUP_TOOLS`。
5. `make olddefconfig`；核对 `.config`：keep 清单在、排除清单无（grep 断言）。
6. `make savedefconfig` → 检查 nuttx 根 `defconfig` → 拷入
   `boards/arm/stm32h7/stm32h750b-dk/configs/velaguard-min/defconfig`。
7. 全量构建（PATH 含 prebuilts/gcc/linux-x86_64/arm-none-eabi/bin），修编译问题。
8. 生成 nuttx patches（只含目标改动）：
   - `git add -N boards/.../configs/velaguard-min/defconfig && git diff -- <path>`
     → `scripts/openvela-velaguard-min-defconfig.patch`。
   - `git diff -- arch/arm/src/stm32h7/stm32_pwm.c`（确认仅 TIM15 笔误）
     → `scripts/openvela-pwm-tim15-fix.patch`。
   - 仿 `apply-openvela-qspi-patch.sh` 写两个幂等 apply 脚本。
9. 幂等验证：连续执行两次 apply 脚本，第二次输出 "already applied"；`git status` 干净。
10. 文档：更新 `docs/velaguard-bringup-known-issues.md`（切换命令、ps1 警告、排除项理由、
    VG_BRINGUP_TOOLS 说明）；如需同步 `app/velaguard/README.md`。
11. 硬件验证（用户执行）：烧录 → NSH → `ls /dev` 断言 → vgpwm/vgrs485/vgesp 实测（AC3）。
12. 用户需求迭代（2026-08-13）：入口改为 `velaguard_app_main`（Guard 常驻 +
    NSH 线程托管），`VG_APP_HOSTS_NSH` 开关删除；`velaguard_app` 命令带
    `g_app_running` 防重护栏；defconfig/补丁/build_minimal.sh 校验同步更新。
13. 收尾：`task.py` 状态推进、Trellis 会话记录、提交前与用户确认。

## Validation commands

```bash
export PATH="$OPENVELA_ROOT/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PATH"
cd $OPENVELA_ROOT/nuttx
make olddefconfig
make -j$(nproc)
make savedefconfig
# 断言
rg -n "INIT_ENTRYPOINT|VG_BRINGUP_TOOLS|GRAPHICS_LVGL|VIDEO_FB|LTDC|INPUT|DEV_URANDOM|CONFIG_NET=" defconfig
```

## Risky files / rollback points

- `nuttx/.config`（gitignored，手术前备份）
- `app/velaguard/Kconfig`、`Makefile`、`CMakeLists.txt`（队伍仓，git diff 可回滚）
- 新 defconfig 与 TIM15 patch（`git apply -R` 可逆）

## Follow-up checks before `task.py start`

- [ ] grilling 决策全部落入 PRD/design（已完成）
- [ ] 最终规划摘要已呈现且用户显式批准（待批）
