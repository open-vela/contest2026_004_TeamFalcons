# OTA 任务 05：B 槽自检、Confirmed 与回滚

日期：2026-10-28。任务 05 已完成：B 槽首次启动执行新固件确定性自检，成功后向另一份 BCB 提交 CONFIRMED，重启后直接由 boot stub 启动 B；自检失败或回滚命令写入 FAILED 并重启回 A 槽；B 槽启动挂起时由硬件独立看门狗（IWDG1）复位，trial 次数超限（3 次）后 boot stub 自动降级启动 A 槽；A 槽内容和出厂启动能力在整个回滚过程中保持不变。

## 已实现

- **试用计数与超限回滚 (`boot/ota_core/`)**：
  - 定义 `VG_OTA_MAX_TRIALS = 3`。
  - `vg_ota_boot_decide()`：记录 `bcb_trial_count`，若状态为 TRIAL 且计数达上限，直接决策 `slot = A, reason = a-failed`。
  - `vg_ota_trial_advance()`：当 boot stub 检测到非首次安装的 TRIAL 重启时，步进递增当前 BCB 的 `trial_count`（1->2->3）；超限后就地向另一份 BCB 写入 `FAILED(seq+1)` 并回滚到 A 槽。
- **Boot Stub 硬件看门狗 (`scripts/qspi_boot_stub/`)**：
  - 新增 `trial_watchdog_enable()`，配置 STM32H750 片上 IWDG1（预分频 `/256`、重装载 `0xFFF`，超时约 32 秒）。
  - 仅在启动 B 槽且状态为 TRIAL/B_INSTALLED 时使能 IWDG1；A 槽及已 confirmed 的 B 槽启动均不启用该看门狗。
  - 邮箱 MBOX 结构增加 `boot_bcb_trials` 字段，版本递增为 4。
- **应用层自检与确认服务 (`app/velaguard/vg_ota_self_check.c/h`)**：
  - 系统启动挂载 eMMC 后调用 `vg_ota_self_check_boot()`，仅在 B 槽 TRIAL 状态下执行。
  - 确定性检查：运行槽位与链接地址、邮箱槽位、BCB 处于 TRIAL 且版本匹配、无故障注入标志、存储可写。
  - 自检成功：在另一份 BCB 写入 `CONFIRMED(seq+1, slot=B)`，记录 `STATE.BIN` 为 `result=confirmed`。
  - 自检失败：在另一份 BCB 写入 `FAILED(seq+1, slot=B)`，记录 `STATE.BIN` 为 `result=self_check_failed`，并调用 `vg_ota_safe_reboot()` 重启到 A。
  - 堆安全：使用动态分配避免在 8 KiB 的 init/console 栈上压入 4 KiB 块，彻底杜绝栈越界导致的 TCB 破坏与 HardFault。
- **NSH 命令与上报契约**：
  - 新增 `vgota selfcheck [--run]`、`vgota confirm`、`vgota rollback [reason]`。
  - `vgota bootinfo` 展示 `trials=<count>`。
  - 测试构建新增 `vgota fixture fail <on|off|status>` 注入失败文件。
  - `vg_mqtt_payload.c/.h` 新增 `vg_mqtt_format_ota_result` 与 `vg_mqtt_format_ota_confirm`。

## 主机测试

执行 `make -C app/velaguard/host_tests test`，完整测试套件 100% 通过：

- `test_ota_boot`：验证 `trial_count` 0/1/2 时保持 B 槽 TRIAL 决议，`trial_count` 3/5/10 时自动降级为 A 槽 FAILED 决议。
- `test_ota_install`：验证 `vg_ota_trial_advance` 的逐步步进（1->2->3）、超限时向另一份 BCB 持久化 FAILED、以及后续重启永久保留 A 槽。
- `test_ota_self_check`：覆盖槽位判断、CONFIRMED 提交、文件/任务名故障注入、版本不匹配拒绝、启动集成以及手动回滚。
- `test_mqtt_payload`：验证 `ota/result` 与 `ota/confirm` 报文的格式与字段完整性。

## 构建与栈

诊断构建执行 `bash scripts/build.sh --ota-fixtures`：

```text
check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1524800 b_size=1524800
boot stub: text=11628 data=0 bss=20
```

Boot stub 片内 Flash 占用为 11.36 KiB（上限 128 KiB），栈余量充足。
生产构建执行 `bash scripts/build.sh`，未开启 `CONFIG_VG_OTA_TEST_FIXTURES`，镜像大小一致（1517152 字节），检查通过。

## 真机证据

测试在真实 STM32H750B-DK 硬件与 COM3 串口上进行：

### 1. 本机长按确认安装 B 槽与自检成功
- 用户在开发板旁边，执行 `vgota fixture stage 1524800 20 T05-OK` 并执行 `vgota fixture arm`。
- 用户在 60 秒窗口内长按 USER 蓝色按键 2 秒以上，设备输出：
  ```text
  vgota: fixture pending locally confirmed seq=20
  ```
- 执行 `vgota reboot`，boot stub 安装镜像并首次跳转至 B 槽，B 槽启动后自动执行自检：
  ```text
  vgota: running new firmware self-check on slot B...
  vgota: self-check PASSED: confirmed seq=23
  vghmi: autostart ok
  LED num: 3
  Hello from openvela contest 2026 team 004!
  ```
- 查询当前状态：
  ```text
  vgota identity
  vgota: identity version=0.1.0 slot=B build=aee1b5540d9d-dirty link=90800000
  vgota bootinfo
  vgota: bootinfo slot=B reason=b-installed bcb=1 state=trial seq=22 bcb_slot=B valid0=1 valid1=1 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0 trials=1
  vgota selfcheck
  vgota: selfcheck state=passed slot=B running_slot=B trials=1 seq=23 bcb_state=confirmed task=T05-OK ver=0.1.0 detail=confirmed seq=23
  vgota fixture bcb status
  vgota: fixture BCB0.BIN valid seq=23 state=confirmed slot=B size=1524800 version=0.1.0 task=T05-OK trials=1
  vgota: fixture BCB1.BIN valid seq=22 state=trial slot=B size=1524800 version=0.1.0 task=T05-OK trials=1
  ```

### 2. 重启后直接由 boot stub 启动 B (b-confirmed)
- 在 B 槽执行 `vgota reboot`，系统重启后直接由 boot stub 启动 B 槽，不再启动试用看门狗：
  ```text
  vgota identity
  vgota: identity version=0.1.0 slot=B build=aee1b5540d9d-dirty link=90800000
  vgota bootinfo
  vgota: bootinfo slot=B reason=b-confirmed bcb=0 state=confirmed seq=23 bcb_slot=B valid0=1 valid1=1 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0 trials=1
  vgota: install stage=0 error=0 bytes=0
  ```

### 3. 失败回滚到出厂 A 槽
- 在 B 槽执行 `vgota rollback intentional_self_check_failure`：
  ```text
  vgota rollback intentional_self_check_failure
  vgota: rollback to slot A committed, rebooting...
  vgota: safe reboot version=0.1.0 slot=B
  ```
- 重启后 boot stub 检测到最新有效 BCB 为 FAILED，自动安全启动出厂 A 槽：
  ```text
  vgota identity
  vgota: identity version=0.1.0 slot=A build=aee1b5540d9d-dirty link=90000000
  vgota bootinfo
  vgota: bootinfo slot=A reason=a-failed bcb=1 state=failed seq=24 bcb_slot=B valid0=1 valid1=1 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0 trials=1
  vgota: install stage=0 error=0 bytes=0
  ```
- A 槽完全恢复正常业务（LVGL HMI、Modbus 采集、AI Agent），绝无启动循环。

### 4. 试用看门狗挂死复位与超限自动回滚
- 在早期未将大缓冲区移出栈前，B 槽启动因 8 KiB 栈耗尽发生 HardFault 挂起。
- 硬件 IWDG1 在约 32 秒后发生超时，由硬件强制触发 MCU 复位。
- 每次复位后 boot stub 检测到处于 TRIAL，自动递增 `trial_count`：
  - 尝试 1：`b-installed trials=1`，挂死复位；
  - 尝试 2：`b-trial trials=2`，挂死复位；
  - 尝试 3：`b-trial trials=3`，挂死复位；
  - 尝试 4：boot stub 检测到 `trials=3 >= VG_OTA_MAX_TRIALS`，判定重试超限，自动回滚启动 A 槽！
- A 槽启动后显示：
  ```text
  vgota identity
  vgota: identity version=0.1.0 slot=A build=aee1b5540d9d-dirty link=90000000
  vgota bootinfo
  vgota: bootinfo slot=A reason=a-failed bcb=1 state=trial seq=22 bcb_slot=B valid0=1 valid1=1 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0 trials=3
  vgota: install stage=0 error=0 bytes=0
  ```
- 证明硬件看门狗超时复位与连续失败超限回滚机制在真实芯片上生效。

## 收尾与环境恢复

1. 执行 `vgota fixture clear`，清除 eMMC `/mnt/emmc/VGOTA/` 中的全部测试临时文件（IMAGE.BIN、MANIFEST.BIN、BCB0.BIN、BCB1.BIN、STATE.BIN）。
2. 使用 `STM32_Programmer_CLI` 擦除 QSPI B 槽区间 `[64 127]` 与传输测试窗口 `[128 139]`，恢复纯净空白状态。
3. 烧录干净生产构建固件（无测试 fixture 入口），SWD 硬复位重启。
4. 最终状态核验：
   ```text
   vgota identity
   vgota: identity version=0.1.0 slot=A build=aee1b5540d9d-dirty link=90000000
   vgota bootinfo
   vgota: bootinfo slot=A reason=a-no-bcb bcb=255 state=invalid seq=0 bcb_slot=A valid0=0 valid1=0 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0 trials=0
   vgota: install stage=0 error=0 bytes=0
   ls /mnt/emmc/VGOTA
   /mnt/emmc/VGOTA:
   ```
   A 槽出厂固件运行稳定正常，eMMC 与 QSPI 均处于清洁基线。
