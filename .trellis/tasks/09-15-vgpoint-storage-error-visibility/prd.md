# 板端存储故障定位与 vgpoint 存储错误可见性加固

命令与稳定应答以 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 为唯一口径；
`/data` 的挂载与软链接布局以 `../nuttx` 板级 bringup 为准。本文件不另开命令表。

## 目标

恢复被存储故障卡住的点表导入通道，并让「存储不可读」在协议层不再和「表本来就是空的」同形。

## 背景

上位机（`F:\Project\uppercomputer`）通过 COM3 导入 `vgpoint_scene_room.json` 的 14 个点，实测：

- `vgpoint list` → `OK cmd=list table=committed n=0`
- `vgpoint get` → `OK cmd=get table=committed n=0`
- 14 条 `vgpoint add` 全部 → `ERR cmd=add code=io msg=candidate_io`

代码可定死的部分：`candidate_io` 只在 `vg_point_table_ensure_candidate()` 非 0 时产生
（`app/velaguard/vgpoint.c:492`），而该函数三条分支最终都走 `vg_point_table_write_candidate()`
（`app/velaguard/vg_point_table.c:1446-1476`），其唯一失败出口是 `fopen(cand_path, "w")` 返回 NULL
（同文件 `:301-304`）。候选表已读出来、只是最后写盘失败时回的是 `io/write_fail`
（`vgpoint.c:532-536`），因此本次失败发生在打开阶段。结论：板子打不开
`/data/velaguard/discover/point_table_candidate.json` 准备写入。

尚需板端证据才能定死的部分：`/data`（软链接到 `/mnt/emmc/data`）为什么写不进去。

`vgpoint list` / `get` 在读取失败时 `memset` 后照常回 `OK n=0`（`vgpoint.c:458-462`、`cmd_get`），
是这次排查绕远路的原因，也是本任务要修的产品缺陷。

## 需求

R1. **定位**。按只读 NSH 命令确定 `/data` 不可写的分支，命令与原始输出记入任务目录：
eMMC 未挂载、卷损坏、空间不足、目录/软链接缺失、堆分配失败。

R2. **恢复**。按定位到的分支把 `/data` 恢复到可读写，使上位机导入可以重跑。
重建卷属破坏性操作，会连带清掉已确认点表、`/data/agent` 配置与密钥、`/data/velaguard/reports`，
必须先人工确认。

R3. **读侧不再吞错**。`vgpoint list` 与 `vgpoint get` 读已确认表失败时：文件不存在
**且存储根可达**时仍回 `OK n=0`（演示流程要求「删掉 points.json 拍空表」合法）；
其他失败回 `ERR code=io msg=<token>`。

R3a. **存储根探测**（2026-09-15 追加，经用户确认改动契约）。文件不存在时再探一次存储根
（`/data`）：路径不可达回 `enoent`；落在 RAM 伪文件系统上回 `enodev`——这种存储重启即丢，
不能算可用。没有这一步，软链接悬空这类故障仍会被显示成 0 点，本次事故在新固件下照样看不出来。

R4. **写侧按实际路径建目录**。`vg_point_table_write_candidate()` 的目录创建改为从入参路径推导父目录
（复用同文件已有的 `mkdir_parent()`），并检查返回值；末级目录已存在视为成功，不算失败。
不再使用写死的 `/data/velaguard/discover`。

R5. **写侧检查写入结果**。`fprintf` 返回负值或 `fclose` 非 0 时返回 `-EIO` 并清掉半成品文件，
不再出现「写满/写坏却回 OK」。

R6. **区分文件不可读与内容损坏**。文件打开成功但找不到 `"points"` 数组时返回 `-EINVAL`，
不再静默当作 0 点表；读取中途出错返回 `-EIO`；文件不存在仍返回 `-ENOENT`。

R6a. **`ensure_candidate` 不把「读不了」当成「没有」**（2026-09-15 追加，经用户确认）。
只有 `-ENOENT` 才回退到「复制已确认表 / 从空表开始」；其他读失败直接返回。
原行为会把损坏的已确认表当成空表，下一次 `apply --confirm` 就把已确认表清空。

R7. **errno 转稳定 token**。新增一个把负 errno 转成 `[A-Za-z0-9_.-]` token 的函数：
`enoent` / `erofs` / `enospc` / `enomem` / `eacces` / `eio` / `efbig` / `einval`，
未识别的归 `eio`。

R8. **板端错误码带病因**。`cmd_add` / `cmd_set` / `cmd_del` 的 `code=io` 保持不变，
`msg` 由写死的 `candidate_io` 改为 R7 的 token。上位机只读 `code`，改名不影响解析。

R9. **文档同步**。协议文档补上现状缺失的 `candidate_io` / `write_fail`，说明 `io` 的 `msg` 携带
诊断 token，并说明 `list` 在存储不可读时回 `ERR`。

R10. **用例**。`app/velaguard/host_tests/test_vgpoint.c` 覆盖：父目录不可写时写候选返回负值；
父目录已存在时按成功处理；缺 `"points"` 的文件读出 `-EINVAL`。

## 验收标准

- [ ] AC1 R1 的分支结论连同原始命令输出记入任务目录，结论有板端证据支撑，不是推测
- [ ] AC2 恢复后 `vgcfg probe` 回 `ok`；上位机重新导入 14 点全部成功，并经人工确认 `apply --confirm`
- [ ] AC3 存储不可读时 `vgpoint list` 回 `ERR code=io msg=<token>`，不再回 `OK n=0`；
  含「文件不存在但 `/data` 悬空或落进 RAM」这一种
- [ ] AC4 已确认表文件不存在、且存储根可达时，`vgpoint list` 仍回
  `OK cmd=list table=committed n=0`
- [ ] AC5 `make -C app/velaguard/host_tests test` 全绿（含 R10 新增用例）
- [ ] AC6 `bash scripts/build.sh` 作品主线编译通过
- [ ] AC7 协议文档第 5.1 节与第 7 节与实际输出一致
- [ ] AC8 破坏性操作（如重建卷）由人确认后执行，未由开发助手自动代答

## 范围外

- 上位机 `main.py` 打印板端 `msg`（另开）
- `../nuttx` 未提交的 `stm32_sdmmc.c` / `wd_start.c` / 板级 defconfig 的处置，
  除非 AC1 落到「挂载正常但写路径本身异常」
- 延期范围：生产级 OTA、ASR/TTS、屏幕配置编辑、手册上传解析
- 新增 NSH 诊断命令（`vgcfg probe` 已能覆盖配置目录的可写性探测）

## 关键决定

| 项 | 决定 |
| --- | --- |
| `candidate_io` | 保留 `code=io`，`msg` 换成 errno token；不新增 code |
| 文件不存在 | 存储根可达时是合法的空表语义，回 `OK n=0`；存储根不可达（悬空 / 落进 RAM）回 `ERR io` |
| 目录创建 | 从入参路径推导，不再写死 `/data/velaguard/discover` |
| 诊断入口 | 复用已有 `vgcfg probe`，不新增命令 |
