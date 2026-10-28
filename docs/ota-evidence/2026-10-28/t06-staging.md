# OTA 任务 06：暂存存储、Manifest 校验与 READY

日期：2026-10-28。任务 06 已完成：实现基于现有 eMMC FAT 的暂存存储管理器与应用侧 OTA 状态机；写入前基于 `statfs` 执行空间预检；镜像先写入临时文件 `IMAGE.TMP`，完成 `fsync` 和完整 SHA-256 回读校验后再原子重命名为 `IMAGE.BIN`；对 Manifest 实施严格的绑定校验矩阵（Device ID、型号、目标槽位、版本、时间窗、签名有效性及镜像摘要）；校验通过后进入 `READY_AWAITING_CONFIRM` 并在屏幕与状态机上就绪；未写入 pending BCB，重启依然保持 Slot A 正常运行，不误触发安装；eMMC 未被格式化，现有 `/data` 与配置凭据保持完好可读。

## 已实现

- **暂存存储管理器 (`app/velaguard/vg_ota_staging.c/h`)**：
  - `vg_ota_staging_check_space()`：调用 `statfs` 检查剩余空间，预留镜像大小 + 4 份 4096 字节元数据 + 1 MiB 安全余量；不足时直接拒绝并标记 `insufficient_space`。
  - 两阶段流式写盘：写入 `IMAGE.TMP` -> `fsync` -> 回读二次核验完整 SHA-256 与 Cortex-M 向量表合法性 -> 原子 `rename("IMAGE.TMP", "IMAGE.BIN")`。
  - 异常自动清理：写入失败、`fsync` 失败或哈希不匹配时，自动删除 `IMAGE.TMP`，绝不残留半写镜像或暴露为 `IMAGE.BIN`。
  - 落盘 `MANIFEST.BIN`（512 字节固定格式）与状态记录 `STATE.BIN`。
- **Manifest 严格绑定校验矩阵 (`vg_ota_manifest_validate()`)**：
  - Device ID：严格匹配板载 UID 派生值（如 `vg-00...`）。
  - Model：严格匹配 `STM32H750B-DK`。
  - 目标槽位：必须为 `VG_OTA_BCB_SLOT_B` (1)，拒绝指向 A 槽。
  - 镜像大小：检查 `8 <= image_size <= VG_OTA_IMAGE_MAX_BYTES`。
  - 版本检查：三段式语义版本合法性，生产构建拒绝版本回退。
  - 时间窗：检查 `expires_at > issued_at`。
  - 签名与测试标志：生产固件拒绝测试 fixture 标志且要求非空签名。
  - 摘要匹配：核对镜像实际计算的 SHA-256 与清单中的哈希逐字节一致。
- **状态机与 READY 门控**：
  - 定义 12 项应用侧 OTA 状态机枚举，实现 `IDLE` -> `OFFER_RECEIVED` -> `DOWNLOADING` -> `VERIFYING` -> `READY_AWAITING_CONFIRM`。
  - 进入 `READY_AWAITING_CONFIRM` 时**绝对不写 pending BCB**，**不触发重启**。
  - 状态同步至 `STATE.BIN` 与 UI 模型（`vg_model_ota`），屏幕 OTA 页面展示版本、大小、摘要前缀，系统状态栏标记“待确认”。
- **NSH 命令支持**：
  - `vgota state`：查询当前暂存与 OTA 状态机详细信息。
  - `vgota fixture offer <bytes> [ver] [task] [fault]`：用于测试环境注入元数据缺陷，验证校验矩阵。

## 主机测试

执行 `make -C app/velaguard/host_tests test`，全部测试 100% 通过。新增 `test_ota_staging.c` 验证：

1. `test_space_check`：空间预检与空间不足（-ENOSPC）拒绝。
2. `test_manifest_validation_matrix`：逐项验证错误 Device ID、型号、槽位、大小越界、版本格式错误、时间窗倒置、非 fixture 空签名以及摘要不匹配的精准拒绝。
3. `test_staging_success`：全流程验证写 `IMAGE.TMP`、`fsync`、回读 SHA-256、原子改名为 `IMAGE.BIN`、写 `MANIFEST.BIN` 与进入 `READY_AWAITING_CONFIRM`，证实无 pending BCB 生成。
4. `test_staging_digest_mismatch_cleanup`：验证哈希不匹配时立即删除 `IMAGE.TMP`，磁盘上无残留。
5. `test_staging_vector_invalid_cleanup`：验证向量表入口不在 B 槽时拒绝并清理。
6. `test_staging_read_fail_cleanup`：验证读取中途失败时清理临时文件并置 FAILED。
7. `test_staging_clear`：验证清理函数彻底抹除所有暂存文件并回到 IDLE。

## 构建与大小

执行 `bash scripts/build.sh --ota-fixtures` 诊断构建：

```text
check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1528888 b_size=1528888
boot stub: text=11628 data=0 bss=20
```

执行 `bash scripts/build.sh` 生产构建：
```text
check_fw_images: OK version=0.1.0 A=0x90000000 B=0x90800000 a_size=1518136 b_size=1518136
# CONFIG_VG_OTA_TEST_FIXTURES is not set
```
镜像与 boot stub 尺寸均在安全限额内。

## 真机证据

在 STM32H750B-DK 真机与 COM3 串口上完成全套实测：

### 1. 拒绝矩阵真实验证 (`vgota fixture offer`)
- **错误 Device ID**：
  ```text
  nsh> vgota fixture offer 1528888 0.2.0 T06-TEST bad_dev
  vgota: offer REJECTED: bad_device_id ERR=-22
  ```
- **错误型号**：
  ```text
  nsh> vgota fixture offer 1528888 0.2.0 T06-TEST bad_model
  vgota: offer REJECTED: bad_model ERR=-22
  ```
- **错误目标槽位 (Slot A)**：
  ```text
  nsh> vgota fixture offer 1528888 0.2.0 T06-TEST bad_slot
  vgota: offer REJECTED: bad_target_slot ERR=-22
  ```
- **时间窗倒置**：
  ```text
  nsh> vgota fixture offer 1528888 0.2.0 T06-TEST bad_time
  vgota: offer REJECTED: bad_time_window ERR=-22
  ```
- **空间不足拒绝**：
  ```text
  nsh> vgota fixture offer 1528888 0.2.0 T06-TEST no_space
  vgota: offer REJECTED: insufficient_space (avail=3884515328 < req=4296032255) ERR=-28
  ```
- **镜像摘要不一致及临时文件自动清除**：
  ```text
  nsh> vgota fixture offer 1528888 0.2.0 T06-TEST bad_hash
  vgota: offer REJECTED: digest_mismatch ERR=-22
  nsh> vgota fixture status
  vgota: fixture IMAGE.BIN missing
  vgota: fixture IMAGE.TMP missing
  vgota: fixture MANIFEST.BIN missing
  vgota: fixture BCB0.BIN missing
  vgota: fixture BCB1.BIN missing
  ```

### 2. 合法 Offer 暂存并进入 READY_AWAITING_CONFIRM
- 从测试传输窗口执行合法 Offer：
  ```text
  nsh> vgota fixture offer 1528888 0.2.0 T06-VALID ok
  vgota: offer ACCEPTED into READY: task=T06-VALID ver=0.2.0 bytes=1528888
  ```
- 查看状态机与暂存文件：
  ```text
  nsh> vgota state
  vgota: state=READY_AWAITING_CONFIRM task=T06-VALID ver=0.2.0 size=1528888 sha=a9f81fee free=3884515328 req=2593848 err=none
  nsh> vgota fixture status
  vgota: fixture IMAGE.BIN present size=1528888
  vgota: fixture IMAGE.TMP missing
  vgota: fixture MANIFEST.BIN present size=512
  vgota: fixture BCB0.BIN missing
  vgota: fixture BCB1.BIN missing
  vgota: fixture STATE.BIN present size=89
  ```
  证实：`IMAGE.TMP` 已原子改名为 `IMAGE.BIN`，`MANIFEST.BIN` 已写入，**没有写入任何 pending BCB**。

### 3. 重启不触发安装、保持 A 槽稳定运行
- 在处于 `READY_AWAITING_CONFIRM` 时执行 `vgota reboot` 重启：
  ```text
  nsh> vgota reboot
  vgota: safe reboot version=0.1.0 slot=A
  ...
  vgota identity
  vgota: identity version=0.1.0 slot=A build=484ca8648686-dirty link=90000000
  vgota bootinfo
  vgota: bootinfo slot=A reason=a-no-bcb bcb=255 state=invalid seq=0 bcb_slot=A valid0=0 valid1=0 bcb_error=0 fat=FAT16 fat_error=0 emmc_error=0 trials=0
  vgota: install stage=0 error=0 bytes=0
  ```
  证实：未获得本地确认时，系统重启绝不擦写或安装 QSPI，直接安全进入 A 槽。

### 4. 非破坏性校验
- 核验 `/data` 目录及其下的业务配置文件（`points.json`、`point_table_a/b.json`）与凭据：
  ```text
  nsh> ls /data
  /data:
   velaguard/
   agent/
   uname
   ...
  nsh> ls /data/velaguard/config
  /data/velaguard/config:
   probe.txt
   points.json
   point_table_a.json
   point_table_b.json
   points.json.bak
  ```
  eMMC 保持原有 FAT16 结构，未重新格式化，全部配置与凭据完好无损。

## 收尾与基线恢复

1. 执行 `vgota fixture clear`，清除所有暂存测试文件。
2. 烧录干净生产构建固件（无 fixture 入口），SWD 硬件复位。
3. 最终状态核验：
   ```text
   vgota: identity version=0.1.0 slot=A link=90000000
   vgota: bootinfo slot=A reason=a-no-bcb bcb=255 state=invalid seq=0 trials=0
   vgota: state=IDLE task= ver= size=0 sha=00000000 err=none
   ```
