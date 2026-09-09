# VelaGuard MThings 验收参考

Stage0 RS485 / Modbus / 帧统计板测以 **`velaguard.mthings`** 为点表与总线参数基准。

## 权威路径

| 环境 | 路径 |
|------|------|
| Windows（主副本） | `C:\Users\19y\Documents\mthings\velaguard.mthings` |
| WSL | `/mnt/c/Users/19y/Documents/mthings/velaguard.mthings` |
| 选手仓（与主副本同步） | `config/mthings/velaguard.mthings` |
| 点表 CSV（可再生成） | `config/mthings/velaguard_sensors.csv` |

校验：

```bash
python3 .cursor/skills/mthings-automation-config-skill/scripts/validate_mthings.py config/mthings/velaguard.mthings
```

## Mock 从站：MThings vs Modbus Slave

| 场景 | 推荐 |
|------|------|
| SCADA / 点表编辑 / Stage0 文档基准 | **MThings**（本目录 `velaguard.mthings`） |
| 板端 HMI **1–32 全扫**、`vgscan` 压测 | **Modbus Slave**（见 [`config/modbus-slave/README.md`](../modbus-slave/README.md)） |

MThings 32 从站 mock 在密集 FC03 扫描下易 **闪退**；Modbus Slave 仅被动应答，与板端主站兼容。迁移步骤：

```powershell
python3 scripts/export_velaguard_modbus_slave.py   # WSL 或 Git Bash
.\scripts\build_velaguard_mbslave.ps1 -OpenConnection
```

## 总线参数（与板端一致）

| 项 | 值 |
|----|-----|
| 串口 | **COM4**（PC USB-RS485，与 `velaguard.mthings` 一致）↔ 板 `/dev/rs485` |
| 波特率 | **9600** |
| 格式 | **8N1**（Parity=0, StopBit=0, DataBit=0） |
| 从站 | **1–32**（MThings 或 Modbus Slave mock，**二选一**，勿双开） |

## 从站与寄存器（摘自工程）

MThings 内 `BLOCK="2"` + `Addr` 对应 **保持寄存器 FC03**，`start = Addr`，`qty` 按点数。

**地址扫描探针**：HMI / `vgdiscover scan` 对每个地址依次尝试 **FC03 @reg 0、1、2**，每种 **qty=1 再 qty=2**（与 MThings 单点读兼容）。32 个 mock 从站中 **仅 addr=2 水浸** 首点在 **reg2**，其余首点均在 **reg0**。

| 从站 | 设备 | 首点 reg | 说明 |
|------|------|----------|------|
| 1 | [S]温湿度-导轨V1.5 | 0 | 湿度/温度 @0–1 |
| 2 | [S]水浸-V1.1 | **2** | 水浸状态 @2；灵敏度 @16 |
| 3 | [S]PT100-FTX458 | 0 | 4 通道温度 @0–3 |
| 4–32 | 见 `velaguard.mthings` | 0 | 各 1–N 点，首点均在 reg0 |

板端 `vgmodbus` 对照命令（FC03 缺省）：

```text
vgmodbus -a 1 -r 0 -c 2 -n 20 -i 1
vgmodbus -a 2 -r 2 -c 1 -n 20 -i 1
vgmodbus -a 3 -r 0 -c 4 -n 20 -i 1
vgdiscover scan -a 1-32
```

**HMI 扫描**：关闭 MThings，改用 Modbus Slave 或停采集后，发现页「开始扫描 1-32」；全扫约 **1–2 分钟**（9600 + 50ms 帧间间隔）；扫描过程中列表会随已发现从站递增。

## 接线与总线占用

```text
PC (MThings COM6) ── USB-RS485 ── A/B/GND ── H750B-DK RS485 (/dev/rs485)
```

- **同一时刻只允许一个主站** 占用 RS485。MThings 轮询与板端 `vgmodbus` **不要同时** 对同一总线发主站请求。
- RS485 物理层自测（`vgrs485 tx/rx`、Hex 抓包）时，先 **停止** MThings 采集。
- Modbus / 帧统计验收时，关闭 MThings 或断开 PC 适配器，由 **板端主站** 轮询真实/模拟从站。

## Stage0 验收对照

完整步骤见 [`.trellis/tasks/08-29-stage0-foundation/stage0-acceptance.md`](../../.trellis/tasks/08-29-stage0-foundation/stage0-acceptance.md)。

| 子能力 | 本工程作用 |
|--------|------------|
| RS485 时序 | 点表无关；`vgrs485` + COM6 Hex 或 LA |
| Modbus 采集 | 从站地址 / 寄存器 / 9600 与 `velaguard.mthings` 一致 |
| 帧统计 | `vgmodbus` 按上表轮询后 `vgstats dump <addr>` |
| eMMC / 掉电 | 与 MThings 无关；走 NSH 存储用例 |

## 维护

修改点表时优先改 Windows 主副本，再同步到仓内：

```bash
cp /mnt/c/Users/19y/Documents/mthings/velaguard.mthings config/mthings/velaguard.mthings
python3 .cursor/skills/mthings-automation-config-skill/scripts/validate_mthings.py config/mthings/velaguard.mthings
```
