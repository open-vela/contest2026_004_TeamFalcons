# VelaGuard Modbus Slave mock 总线

板端 HMI / `vgscan` 全扫 **1–32** 时，MThings 32 从站 mock 负载高，易出现 **闪退**。推荐改用 **Modbus Slave** 作 RS485 从站模拟：只被动应答 FC03，无采集轮询，与板端主站扫描兼容。

## 为何迁移

| | MThings | Modbus Slave |
|---|---------|--------------|
| 角色 | 主站采集 + mock 从站 | 纯从站模拟 |
| 32 站扫描 | 易与板端争用总线 / 进程崩溃 | 轻量，按 slave ID 应答 |
| 点表来源 | `velaguard.mthings` | 本目录 `velaguard_slaves.csv`（由 mthings 导出） |

点表仍以 **`config/mthings/velaguard.mthings`** 为权威；Modbus Slave 只复刻 **FC03 保持寄存器** 布局，供扫描与 `vgmodbus` 验收。

## 文件

| 文件 | 说明 |
|------|------|
| `velaguard_slaves.csv` | 32 从站：addr、名称、首点 reg、holding 数量、种子值 |
| `../mthings/velaguard.mthings` | 完整点表（MThings 工程，Stage0 文档仍引用） |

生成 / 刷新 CSV（改 mthings 后执行）：

```bash
python3 scripts/export_velaguard_modbus_slave.py
```

## Windows 一键生成 32 从站窗口

需已安装 [Modbus Slave](https://www.modbustools.com/)（与现有 `Mbslave1.mbs` 同产品）。

```powershell
cd C:\...\contest2026_004_TeamFalcons
.\scripts\build_velaguard_mbslave.ps1
# 关闭 MThings 后，一次加载 32 窗口并保持连总线（推荐扫描前）：
.\scripts\build_velaguard_mbslave.ps1 -OpenConnection -KeepOpenSeconds 3600
```

说明：COM 自动化里 `OpenConnection` 可能返回 0，但串口仍可用；以板端 `vgmodbus`/`vgdiscover` 实读为准。

脚本为每个地址创建 **独立数据窗口**（slave ID = 站号），`SetupHoldingRegisters(addr, 0, qty)`：

- **addr=1**：qty=2（湿度/温度 @0–1）
- **addr=2 水浸**：qty=17，首点在 **reg2**（与扫描探针一致）
- **addr=3–32**：qty 见 CSV，首点均在 **reg0**

输出默认目录：`%USERPROFILE%\Documents\mthings\NN_<传感器名>.mbs`
（例如 `01_温湿度-导轨V1.5.mbs`；名称来自 `velaguard.mthings` / CSV）。

一键打开 32 个窗口（推荐）：

```powershell
.\scripts\open_velaguard_mbslaves.ps1
# 或资源管理器双击：
#   %USERPROFILE%\Documents\mthings\open_velaguard_mbslaves.cmd
```

连接总线时：

```powershell
.\scripts\open_velaguard_mbslaves.ps1 -OpenConnection -KeepOpenSeconds 3600
```

### 手动接线（与 MThings 相同）

```text
PC Modbus Slave (COM6, 9600 8N1, Parity=None) ── USB-RS485 ── 板 /dev/rs485
```

**Connection 对话框必须与板端一致：9600、8 数据位、无校验、1 停止位。** 若 Parity 误设为 Even，板端会收到错误帧（如 `01 20 ...`），表现为 timeout。

## 验收

1. **关闭 MThings**（释放 COM6）。
2. Modbus Slave：**Connect**；确认 32 个窗口均已加载且 slave ID 1–32 不重复。
3. 板端 NSH（COM3）：

```text
vgmodbus -a 1 -r 0 -c 2 -n 20 -i 1
vgmodbus -a 2 -r 2 -c 1 -n 20 -i 1
vghmi scan -a 1-32
```

或 Windows：

```powershell
.\scripts\stage1_vgscan_1_32.ps1
```

预期：**found 32/32 slave**。

## 与 MThings 的关系

- Stage0 文档、`velaguard_sensors.csv` 仍以 MThings 工程为准。
- 日常 **32 站扫描 / HMI 发现页** 优先 Modbus Slave；需要 MThings SCADA 时再单独开，**勿与板端主站同时占用 RS485**。
- 修改点表：改 Windows 主副本 mthings → 同步仓内 → 重新 `export_velaguard_modbus_slave.py` → `build_velaguard_mbslave.ps1`。
