---
name: velaguard-board-inner-loop
description: "VelaGuard 板端内环：作品主线编译、QSPI 烧录、COM3 NSH 自行验收。Use when: 编译 velaguard-lvgl、bash scripts/build.sh、烧录 flash.ps1、板测、COM3、NSH 验收、*_accept.ps1、自行验收、firmware build flash accept。"
---

# VelaGuard 板端内环

从源码编到板上 NSH 绿灯。Agent 自己跑完，再按一行结论汇报。不要把 flash / 板测甩给用户当默认收尾。

本 Skill 不管 `ask`、LLM、HMI 与 Agent 双 autostart。点表闸门走 `velaguard-candidate-confirm`；告警上屏走 `velaguard-alarm-to-screen`。

## 流程

1. 在选手仓根目录执行 `bash scripts/build.sh`。日常入口是本仓脚本，不是父目录 openvela 的 `./build.sh`。默认 TARGET=`velaguard-lvgl`。任务另有指定再用 `min` / `emmc` / `ai-probe`。
2. 需要烧录：先确认 `.debug/nuttx.hex` 是本次构建产物，再跑 `scripts/flash.ps1`。构建失败会清空旧主镜像，缺 hex 时禁止拿上一版固件凑合烧。
3. 改了 `app/velaguard/` 协议或点表逻辑：`make -C app/velaguard/host_tests test`。
4. Windows COM3 可用：跑对应 `scripts/*_accept.ps1`。选哪条见 [references/commands.md](references/commands.md)。
5. 汇报格式：命令 / pass-fail / 关键日志一行。失败先自行修一轮。

公共树（`../nuttx` / `../apps` / MQTT-C）只校验已含 VelaGuard 改动，**禁止** apply `scripts/openvela-*.patch`。树未就绪：`bash scripts/build.sh --sync-upstream`。

## 用户介入（仅这些情况）

- COM 口被占且脚本无法抢占
- 需要改 MThings / 从站 / 网线等物理接线
- 验收依赖真实 `ask` 且 LLM key 未配（本 Skill 不要求跑 `ask`；碰到则 SKIP 并说明）

## 常用命令

```bash
bash scripts/build.sh
bash scripts/build.sh --debug
bash scripts/build.sh --clean
make -C app/velaguard/host_tests test
powershell.exe -ExecutionPolicy Bypass -File scripts/flash.ps1
powershell.exe -ExecutionPolicy Bypass -File scripts/stage1_flash_and_accept.ps1
```

preset、accept 脚本对照、COM 占用处理：读 [references/commands.md](references/commands.md)。
