# 执行计划

## 前置：板端定位（人工，不阻塞下面几步）

在 COM3 的 NSH 里按序执行，输出贴回任务目录：

```text
vgcfg probe
ls /mnt/emmc
ls /data
ls /data/velaguard
mount
df
```

判读：`vgcfg probe` 回 `ok dir=...` 说明文件系统正常，转查堆；回
`data/emmc mount missing errno=` 或 `open ... fail errno=` 直接给出病因。
结论与原始输出写进 `research/storage-diagnosis.md`（AC1）。

破坏性操作（`mkfatfs /dev/mmcsd0`）只在人工确认后执行（AC8）。

## 步骤

1. **R4 目录创建** — `app/velaguard/vg_point_table.c`
   - `mkdir_p()`：末级 `mkdir` 成功或 `EEXIST` 返回 0，其他返回 `-errno`。
   - `write_candidate()` 第 299 行：`mkdir_p("/data/velaguard/discover")` 改为
     `mkdir_parent(out)` 并检查返回值。
   验证：`make -C app/velaguard/host_tests test_vgpoint && ./app/velaguard/host_tests/test_vgpoint`

2. **R5 写入结果** — 同文件 `write_candidate()` 结尾
   - `ferror` / `fclose` 检查，失败 `unlink(out)` 并返回 `-EIO`。
   验证：同上，现有 roundtrip 用例仍绿。

3. **R6 损坏文件** — 同文件 `vg_point_table_read()` 第 1378-1388 行
   - 两处 `goto out` 前置 `ret = -EINVAL`。

4. **R7 errno token** — `vg_point_table.c` 新增 `vg_point_table_err_token()`，
   `vg_discover.h` 声明。
   验证：步骤 6 的用例直接断言映射结果。

5. **R3 / R8 命令层** — `app/velaguard/vgpoint.c`
   - `cmd_list`、`cmd_get` 读失败且非 `-ENOENT` 时回 `ERR code=io msg=<token>`。
   - `cmd_add` / `cmd_set` / `cmd_del` 的 `candidate_io` 换成 token。
   验证：`bash scripts/build.sh` 编译通过。

6. **R10 用例** — `app/velaguard/host_tests/test_vgpoint.c`
   - 父目录不可写（用只读目录下的路径）时 `vg_point_table_write_candidate()` 返回负值；
   - 父目录已存在时按成功处理；
   - 缺 `"points"` 的文件 `vg_point_table_read()` 返回 `-EINVAL`；
   - `vg_point_table_err_token(-ENOENT) == "enoent"` 等映射。
   Makefile 不用改。

7. **R9 文档** — `docs/velaguard-host-nsh-protocol.md`
   - 第 5.1 节补 `list` 在存储不可读时回 `ERR`；
   - 第 7 节补 `candidate_io` / `write_fail`，说明 `io` 的 `msg` 携带诊断 token。

8. **R3a / R6a 追加**（质量检查后经用户确认的契约改动）
   - `vg_point_table.c`：`vg_point_table_read` 加 `ferror` 检查判 `-EIO`；
     `ensure_candidate` 只在 `-ENOENT` 时回退到空表；新增
     `vg_point_store_root_ok()`（`statfs` + `PROC_SUPER_MAGIC` 判 RAM 伪文件系统）。
   - `vgpoint.c`：`cmd_list` / `cmd_get` 在文件缺失时探存储根；
     新增 `CONFIG_VG_STORE_ROOT` 兜底定义。
   - `docs/`：第 3 节、5.1、5.6、7.1 同步，token 表加 `enodev`。
   - `test_vgpoint.c`：存储根探测 4 项 + `ensure_candidate` 不一并被当成空表。

## 验证门

| 门 | 命令 | 通过标准 |
| --- | --- | --- |
| 单测 | `make -C app/velaguard/host_tests test` | 12 个用例全绿（AC5） |
| 编译 | `bash scripts/build.sh` | 产出 `nuttx.hex`（AC6） |
| 文档 | 对照代码实际输出复核第 5.1 / 7 节 | 一致（AC7） |
| 板端 | 烧录后 NSH：`vgcfg probe` → 上位机导入 → `vgpoint test` → 人工 `apply --confirm` | 14 点全部落地（AC2、AC3、AC4）；确认闸门不合并、不代答 |

板端验证需要人参与：`vgpoint test` 出结果后停下确认，再单独发 `vgpoint apply --confirm`。

## 回滚点

- 步骤 1-3 一组、步骤 4-5 一组、步骤 6-7 一组，各自可单独 revert。
- 步骤 1 改了 `mkdir_p` 的返回语义，连带影响第 163 行的 `vg_discover_state_save()`（丢弃返回值）
  与第 897 行的实时值写盘，回滚时一起看。
