# 板端内环：preset、验收脚本、COM

先读本 Skill 的 SKILL.md。这里只放对照表。

## preset

`scripts/build.sh` 把 `velaguard` / `velaguard-lvgl` / `net` 都归一成作品主线 `velaguard-lvgl`。

| 参数 | defconfig | 何时用 |
|------|-----------|--------|
| （默认）/ `velaguard` / `net` | `velaguard-lvgl` | 作品主线（LVGL + 网 + discover + Agent 已链接） |
| `min` | `velaguard-min` | 无屏无网的 bring-up |
| `emmc` | `velaguard-emmc` | eMMC / `/data` 探针 |
| `ai-probe` | `velaguard-ai-probe` | Agent 可行性探针 |
| `lvgl` | 上游 `lvgl` | 不要当作品主线 |

`--clean` 会 `configure.sh -E` 复位到所选预设，未 `savedefconfig` 的 `.config` 改动会丢掉。

产物：`nuttx/nuttx.hex`、`nuttx/nuttx.bin`，并暂存到选手仓 `.debug/`。Download / `flash.ps1` 烧 `.debug` 里的 HEX。

## 验收脚本怎么选

一律：

```text
powershell.exe -ExecutionPolicy Bypass -File scripts/<name>.ps1
```

默认串口 COM3、115200 8N1，提示符 `nsh>`。

| 本次改动 | 脚本 |
|----------|------|
| 编完要烧再抽检开机 | `stage1_flash_and_accept.ps1`（先占 COM3，再 `flash.ps1`，再等 boot） |
| 只烧、不跑 NSH 套件 | `flash.ps1` |
| 点表 / discover / 总线 | `stage1_modbus_discovery_accept.ps1` |
| HMI / LVGL 页面 | `stage1_lvgl_hmi_accept.ps1` |
| Agent 集成（不含本 Skill 必跑的 `ask`） | `stage1_agent_accept.ps1` |
| 网口 / MQTT 最小环 | `stage1_net_full_accept.ps1` |
| 运营 Skill / 日报落盘 | `stage1_agent_ops_accept.ps1` |
| 阶段 0 bring-up | `stage0_accept.ps1` |

对应 `*_accept_nsh.txt` 是手工 NSH 步骤，脚本不可用时按文件做。

Host 单测不占串口：`make -C app/velaguard/host_tests test`。

## COM 占用

1. 先关 Cursor 串口监视器、SSCOM、其它 `SerialPort` 占用。
2. 优先 `stage1_flash_and_accept.ps1`：它先打开 COM3 再烧，避免烧完被监视器抢走。
3. 仍被占：查占用进程，能杀则杀，再重跑脚本。
4. 杀不掉或口不是 COM3：停下来告诉用户，不要假装板测通过。

抢占失败属于用户介入，不是跳过验收。
