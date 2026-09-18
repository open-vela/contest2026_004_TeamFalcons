# 执行记录：点表目录丢失导致 vgpoint add 全部 io/eio

## 现象

上位机（`F:\Project\uppercomputer`，`vgpoint_host_apply.ps1` 同族流程）在 COM3 上导入
14 点，全部失败：

```text
vgpoint: OK cmd=list table=committed n=0
vgpoint: ERR cmd=add code=io msg=eio     # ×14
[import] 完成 ok=0 fail=14 / 14
```

`vgpoint list -c` 同时回 `ERR cmd=list code=io msg=eio`。注意 `msg=eio` 是
`vg_point_table_err_token()` 的兜底分支，说明真实 errno 不在映射表里，不是磁盘满之类的常见错。

## 板端定位（COM3 NSH，2026-09-17 实测）

```text
mkdir /data/velaguard/discover/diag2
nsh: /data/velaguard/discover/diag2: mkdir failed: 20     ← ENOTDIR，父级不是目录
cat /data/velaguard/nodir/nope.txt
nsh: cat: open failed: 20                                  ← 同样 ENOTDIR
cat /data/velaguard/config/nope.txt
nsh: cat: open failed: 2                                   ← 文件缺失才是 ENOENT
ls -l /data/velaguard
 live/  config/  clock.txt  hmi_fleet.txt  reports/         ← discover/ 不存在
```

判读：NuttX 对「路径中间某级不存在」返回 **ENOTDIR**，而代码只把 **ENOENT** 当
「表还不存在」。

### 根因

`vg_point_table.c` 里两处只认 ENOENT：

1. `vg_point_table_read()`：`fopen` 失败直接把 errno 上抛，于是缺目录 →
   `-ENOTDIR` → token 兜底成 `eio`。`cmd_list` 只在 `-ENOENT` 时探存储根并当作空表，
   所以 `list -c` 报错。
2. `vg_point_table_ensure_candidate()`：只在 `vg_point_table_read()` 回 `-ENOENT`
   时才继续走「建目录 + 写候选表」。缺目录时拿到 `-ENOTDIR`，直接 `return ret`，
   **自愈分支永远走不到**，`cmd_add` 于是回 `code=io msg=eio`。

一个丢失的目录因此把整张点表打成永久失败，而错误信息里看不出原因。

## 改动

`app/velaguard/vg_point_table.c`：

- `vg_point_table_read()`：`fopen` 的 `ENOTDIR` 归一到 `-ENOENT`。
  「这个路径下还没有表」和「表在的目录没了」对调用者是同一件事——接下来都该建表。
- `vg_point_table_err_token()`：新增 `enotdir` token，不再被兜底成 `eio`。
- `mkdir_p()`：`EEXIST` 后用 `stat` 确认确实是目录；同名普通文件返回 `-ENOTDIR`，
  比 `fopen` 后续报一个裸 `write_fail` 更接近原因。

`app/velaguard/host_tests/test_vgpoint.c`：三组新用例。

- 路径中间是普通文件（host libc 下产生 ENOTDIR，与板端同一 errno）时 read 回 `-ENOENT`；
- `enotdir` token 存在；
- 目录丢失后 `write_candidate()` / `ensure_candidate()` 能重建目录树并留下可用的表。

### 反向对照

host libc 对「父目录不存在」回 ENOENT，只有「中间是普通文件」才回 ENOTDIR，所以
第一组用例必须用后者构造。把 `read()` 的修复还原后跑：

```text
FAIL: ENOTDIR from a path component reads as no table
test_vgpoint: 1 failure(s)   (exit=1)
```

修复在位时全绿。这确认用例真的覆盖了这个缺陷，而 host 的 ENOENT 形态覆盖不到。

## 验收

| 门 | 命令 | 结果 |
| --- | --- | --- |
| 单测 | `make -C app/velaguard/host_tests test` | 14 个目标全绿，含三组新用例 |
| 反向对照 | 还原 read 修复后单跑 `test_vgpoint` | 如预期失败（exit=1） |
| 编译 | `bash scripts/build.sh` | exit=0，0 warning，`nuttx.hex` 3515776 字节 |
| 板端 | `rmdir /data/velaguard/discover` 后 `vgpoint list -c` / `add` | `OK cmd=list table=candidate n=0`；`add` 回 `OK cmd=add table=candidate n=1`，并重建出 `discover/point_table_candidate.json` |
| 掉电保持 | SWD 硬复位后 `vgpoint list -c` | `OK cmd=list table=candidate n=14`，14 点与阈值完整 |
| 回归 | 路径中间是普通文件时 write | 回负值（既有用例，未破坏） |

板端自愈路径实测输出：

```text
rmdir /data/velaguard/discover
vgpoint list -c          → vgpoint: OK cmd=list table=candidate n=0
vgpoint add -i diagx -a 1 -r 0
                         → vgpoint: OK cmd=add table=candidate n=1
ls -l /data/velaguard/discover
                         → point_table_candidate.json
```

## 点表恢复（人工闸门）

14 点已重新写入候选表（`ok=14 fail=0`），阈值与
`scripts/vgpoint_scene_room.json` 一致，`vgpoint list -c` 显示 `n=14`。

**没有执行 `apply --confirm`。** 按 V5，试读与落盘必须由人分两步完成：
先 `vgpoint test` 看 READ 行，再单独发 `vgpoint apply --confirm`。

`/data/velaguard/config/` 与 `/data/velaguard/reports/` 经 `dd` 实测可读写，
`apply` 写 `points.json` 与建议落盘 `alarm_advice.txt` 的通路正常。

## 仍未解决：存储本身发生过损坏

这次不只是目录缺失，`/mnt/emmc/data/velaguard/config/` 与 `reports/` 都被清空
（`points.json` 不见了），`/mnt/emmc` 的 `ls` 还出现 **重复的 `data/` 条目**。
固件里没有任何删除卷内容的代码（无 `mkfatfs`、挂载不带 format），所以这是
FAT 目录项在掉电或写失败时被破坏的迹象，与 09-15 那次 eMMC 运行期失效同源。

`CONFIG_STM32H7_SDMMC_IDMA` 当前未开，SDMMC 走 PIO，驱动自己就带警告
`"Large Non-DMA transfer may result in RX overrun failures"`，而 09-15 已确认
PIO 通路的缓冲区判定误伤会中途丢弃块写、之后卡对一切操作回 ETIMEDOUT。

本次修复解决的是「一次目录损坏就把点表打成死局」，没有解决存储为何损坏。
建议后续单独立项：打开 IDMA 复测掉电/长跑，并给出卷健康检查与恢复流程。
