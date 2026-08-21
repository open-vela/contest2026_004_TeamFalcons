# velaguard（VelaGuard bring-up 工具）

映射到 openvela `packages/demos/contest2026_004_hello_app`。
队伍把应用代码放在本目录下。

## 构建开关

- `CONFIG_VG_BRINGUP_TOOLS`：编译 `velaguard_app` / `vgpwm` / `vgrs485` /
  `vgesp` / `vgmqtt` / `vgmodbus` 六个 NSH 命令。默认跟随
  `CONFIG_LVX_USE_DEMO_CONTEST2026_004_VELAGUARD_APP`（ps1 路径），
  `stm32h750b-dk:velaguard-min` 预设显式启用，与 LVGL 解绑。
- `vgmodbus`：Modbus RTU 主站（nanoMODBUS + `/dev/rs485`）。示例：
  `vgmodbus -a 1 -r 0 -c 4 -n 1` 读从站 1 的 4 个保持寄存器后退出。
- 旧符号 `CONFIG_LVX_USE_DEMO_CONTEST2026_004_VELAGUARD_APP` 保留给未来 LVGL demo
  应用使用。
