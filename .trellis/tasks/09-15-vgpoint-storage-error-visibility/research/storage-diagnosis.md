# 板端存储故障诊断（2026-09-15，COM3 实测）

原始命令输出存在 `.debug/vg_state_run.txt`、`.debug/vg_ab_run.txt`、`.debug/vg_ac4_run.txt`、
`.debug/vg_board_after_flash.txt`。采集脚本在 `.debug/`（`nsh_boot_capture.ps1`、
`vg_storage_diag.ps1`、`nsh_reset_only.ps1`）。

## 结论

**根因是 eMMC/SDMMC 通路会在运行中失效，不是点表数据损坏。** 表一直在，把存储修好就都回来了。

### 1. 数据没有丢

存储健康时（烧录后重启，`.debug/vg_state_run.txt`）：

```
vgcfg probe   → vgcfg: probe OK 0 (ok dir=/data/velaguard/config)
ls /data/velaguard/config → point_table_a.json point_table_b.json probe.txt points.json
df            → 65536 / 59644 blocks, Available 59586
vgpoint list  → OK cmd=list table=committed n=14
vgpoint list -c → OK cmd=list table=candidate n=14
vgpoint get   → VALUE 行，age_ms=7120
```

那 14 点正是上位机要导入的 temp/humid/flood/smoke/acu_ret/acu_set/acu_run/ups_load/
ups_soc/ups_bypass/volt_a/pwr_kw/energy_kwh/door。

### 2. 用户当时看到的现象 = eMMC 超时

绕开 `umount` 之后板子进入稳定的故障态（`.debug/vg_ac4_run.txt`）：

```
vgcfg probe         → FAIL -110 (ensure_basedir -110 errno=110 dir=/data/velaguard/config)
ls /mnt/emmc        → 空目录（不是挂载的 vfat）
ls /data/velaguard  → nsh: ls: stat failed: 110
```

errno 110 = ETIMEDOUT。旧固件在此状态下会打出用户看到的原始症状
（`list` 读失败被 memset 成 `OK n=0`，`add` 写不出候选回 `candidate_io`），
新固件同样状态下打出 `ERR cmd=list code=io msg=eio`。

### 3. eMMC 在开机后几十秒内死亡

同一次启动内：启动日志 `eMMC mounted at /mnt/emmc`、`Symlink /data -> /mnt/emmc/data`、
`vgcfg: OK seq=19 name=vgpoint`（说明 eMMC 读成功），二三十秒后探测已是 ETIMEDOUT。
反复可复现。运行期持续写盘的主要来源是 HMI 每轮采集覆盖 `live/values.txt`。

## 新固件错误可见性验收（真机）

| 场景 | 命令 | 实测输出 |
| --- | --- | --- |
| 存储健康 | `vgpoint list` | `OK cmd=list table=committed n=14` |
| 存储健康 | `vgpoint add -i dtest -a 9 -r 0 -N diagprobe` | `OK cmd=add table=candidate n=15` |
| 存储健康 | `vgpoint del dtest` | `OK cmd=del table=candidate n=14` |
| `umount /mnt/emmc` 后 | `vgpoint list` | `ERR cmd=list code=io msg=enoent` |
| `umount /mnt/emmc` 后 | `vgpoint add -i dtest2 -a 9 -r 1` | `ERR cmd=add code=io msg=enoent` |
| eMMC 超时态 | `vgpoint list` / `list -c` | `ERR cmd=list code=io msg=eio` |
| eMMC 超时态 | `vgpoint get` | `ERR cmd=get code=io msg=eio` |
| eMMC 超时态 | `vgpoint add -i probefa -a 9 -r 0` | `ERR cmd=add code=io msg=eio` |
| eMMC 超时态 | `vgpoint apply` | `ERR cmd=apply code=need_confirm msg=need_confirm`（闸门完好） |

AC3 达成。AC4（文件缺失但存储可达 → `OK n=0`）**未验**：需要在健康板子上删掉候选文件，
但当时 eMMC 已进入超时态，`rm` 报 `unlink failed: 2`。

## 另一处独立缺陷：`ls -l /` 打死板子

两次抓到（新旧固件各一次，PC/LR 仅差 0x100，是同一个缺陷）：

```
HARDFAULT: CFSR=00008200 HFSR=40000000 BFAR=0000003b
stacked PC=90091f6c LR=90077fdd
```

符号解析（`.debug/nuttx.elf`）：

- PC = `getpid+0xc`，LR = `nsh_command+0x59`
- `nsh_command` 在每条命令的 handler 返回后调 `getpid()`（`apps/nshlib/nsh_command.c:1322`）
- `0x90091f70` 处字面量 `0x24000264` = `g_readytorun`
- 反汇编链路：`g_readytorun.head` → `[+0x70]` → `[+0]` → 得到 `0xb` → `[0xb+0x30]` = `0x3b` 才崩

空值检查都在，是**运行任务的 group/info 指针被写坏**（内存损坏）。VCP 的 DTR 脉冲不能复位它，
只能走 SWD 复位（`.debug/nsh_reset_only.ps1`）。

## SDMMC 守卫补丁的两个误判（2026-09-15 晚，经用户确认后修）

那份未提交的 `stm32_sdmmc.c` 改动给 PIO 拷贝加了 `stm32_sdmmc_userbuf_ok()` 白名单，
不在名单里的缓冲区会被 `priv->remaining = 0; break;` 直接丢弃。名单原本只有
DTCM / AXISRAM / SRAM123 / SRAM4，**漏了两类真实缓冲区**：

1. **SDRAM**。`board.h` 里 `BOARD_SDRAM2_SIZE = 8MB`、`BOARD_SDRAM2_HEAP_OFFSET = 2MB`，
   SDRAM 注册进了内核堆，FAT 的扇区缓冲（`fs_heap_malloc`）会落在那里。
2. **QSPI XIP flash**。写卡的数据源可以是 flash 里的 const 对象。
   `vg_agent_seed` 启动时把 skill 文件写进 `/data/agent/skills/`，源就是常量。

直接证据（补上 `syslog` 后第一次启动就抓到）：

```
ERROR: TX buf 0x900c082b rem 512 outside SRAM, drop
```

`0x900c082b` 解析为 `g_modbus_query_skill`（flash 常量）。

而被丢弃的是**块写的中途**，卡之后对一切都回 ETIMEDOUT——这就是「开机几十秒 eMMC 就死」的机制。

同时 `mcerr` 在没有 `CONFIG_DEBUG_MEMCARD_ERROR` / `CONFIG_DEBUG_ERROR` 时是 `_none`，
丢弃完全静默，所以查了很久。已把三处丢弃点改成 `syslog(LOG_ERR, ...)`。

修法与结果：白名单补上 FMC bank4（XIP 窗口）与 bank5/bank6（SDRAM），重烧后
`outside SRAM, drop` 不再出现，agent skill 正常落盘。

## 仍未解决：eMMC 依旧会挂

修完守卫后复测：启动日志正常（`eMMC mounted`、`vgcfg: OK seq=19`），`vgcfg probe` 回 `ok`、
`ls /data/velaguard/*` 正常；但复位后 22 秒内的第一轮探测就不再应答，且**没有任何 panic 输出**
（静默挂死，与 `ls -l /` 那次会打印 dump 的形态不同）。150 秒 soak 期间未捕获到 fault 行。

说明守卫的误判只是原因之一。构建期那条既有警告指向另一半：

```
chip/stm32_sdmmc.c:133:4: warning: #warning "Large Non-DMA transfer may result in RX overrun failures"
```

`CONFIG_STM32H7_SDMMC_IDMA is not set`（纯 PIO），而日志里 Stage0 记录过
「硬件接的是 8-bit MMC，NuttX H7 路径只跑 1-bit」。这部分需要单独立项。

## 待办

- AC4 需在健康板子上补验（`rm` 候选文件 → `list -c` → 期望 `OK n=0`）。上次尝试时
  eMMC 已进入超时态，`rm` 报 `unlink failed: 2`。
- eMMC 运行期失效的根因在 `../nuttx` 的 SDMMC 驱动（PIO/IDMA、总线宽度、时序余量），
  超出本任务范围，需单独立项。
- 板子当前处于静默挂死态。恢复只能走 SWD 复位：`.debug/nsh_reset_only.ps1`。
