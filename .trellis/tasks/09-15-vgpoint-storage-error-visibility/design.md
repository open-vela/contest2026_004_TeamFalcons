# 设计：存储错误可见性

## 边界

改动只落在本仓，不碰 `../nuttx`：

| 文件 | 改什么 |
| --- | --- |
| `app/velaguard/vg_point_table.c` | 目录创建、写入结果检查、损坏文件判定、errno token |
| `app/velaguard/vg_discover.h` | 新函数的声明，与其它 `vg_point_table_*` 放一起 |
| `app/velaguard/vgpoint.c` | `cmd_list` / `cmd_get` 不再吞错；`cmd_add` / `cmd_set` / `cmd_del` 的 msg |
| `docs/velaguard-host-nsh-protocol.md` | 第 5.1 节、第 7 节 |
| `app/velaguard/host_tests/test_vgpoint.c` | 新增用例 |

板端 eMMC 的恢复是操作，不是代码改动，按 R2 走人工确认。

## 契约

### errno token

```c
/* vg_point_table.c 实现，vg_discover.h 声明 */
FAR const char *vg_point_table_err_token(int ret);
```

入参是 `vg_point_table_*` 系列已经返回的负 errno，出参是协议允许的 `[A-Za-z0-9_.-]` 短串：

| ret | token |
| --- | --- |
| `-ENOENT` | `enoent` |
| `-EROFS` | `erofs` |
| `-ENOSPC` | `enospc` |
| `-ENOMEM` | `enomem` |
| `-EACCES` | `eacces` |
| `-EIO` | `eio` |
| `-EFBIG` | `efbig` |
| `-EINVAL` | `einval` |
| 其他 | `eio` |

不直接打印数字，是因为 errno 数值随架构和 libc 变化，token 才是稳定可检索的。

### 稳定应答

协议形态不变，只换 `msg` 内容：

```text
vgpoint: ERR cmd=list code=io msg=enoent
vgpoint: ERR cmd=add code=io msg=erofs
```

`code` 集合不新增。`msg` 按协议本来就是自由文本，上位机只读 `code`（
`F:\Project\uppercomputer\main.py:853-888` 只对 `dup_id` 分支特殊处理），所以换 msg 不影响解析。

## 关键改动

### 1. 目录创建（R4）

`mkdir_p()` 现在把逐级 `mkdir` 的失败全部丢弃，末级返回 `mkdir()` 的原始结果——目录已存在时返回
`-1/EEXIST`，调用方无法区分「已存在」和「建不出来」。改为：

- 末级 `mkdir` 成功或 `errno == EEXIST` 都返回 0；
- 其他 errno 返回 `-errno`。

`vg_point_table_write_candidate()` 第 299 行写死的 `mkdir_p("/data/velaguard/discover")` 换成
已存在的 `mkdir_parent(out)`（`vg_point_table.c:129-148`，目前只在第 897 行被调用且丢弃返回值），
并检查返回值。这样：

- 写候选时建的是候选路径的父目录；
- `vg_point_table_apply()` 走同一个函数写已确认表，父目录 `/data/velaguard/config` 不再隐式依赖
  板级 bringup 预先建好；
- host 测试可以传临时目录下的路径，不再受写死的 `/data` 影响。

`mkdir_parent()` 对不含 `/` 的路径（host 测试用的 `vgpoint_roundtrip.json`）原样返回 0，行为不变。

### 2. 写入结果（R5）

`write_candidate()` 现在完全不看 `fprintf` / `fclose` 的返回值
（`vg_point_table.c:307-352`），写满或写坏都回 0。改为在 `fprintf(fp, "]}\n")` 之后：

```c
  if (ferror(fp) != 0)
    {
      fclose(fp);
      unlink(out);
      return -EIO;
    }

  if (fclose(fp) != 0)
    {
      unlink(out);
      return -EIO;
    }

  return 0;
```

用 `ferror` + `fclose` 返回值覆盖，而不是给十几个 `fprintf` 逐个加判断。半成品文件用 `unlink`
清掉，避免留下一个能被读成 0 点的截断 JSON。`unlink` 在同文件第 908 行已经用过。

### 3. 损坏文件（R6）

`vg_point_table_read()` 在第 1378-1388 行找不到 `"points"` 键或缺 `[` 时走 `goto out`，
此时 `ret` 仍是 0，等于把截断或损坏的文件读成合法的空表。改为置 `ret = -EINVAL` 再 `goto out`。

连带影响（都是往安全方向）：

- `cmd_list` 读已确认表遇到损坏文件时能报错，而不是显示 0 点；
- `cmd_apply` 读到损坏的候选文件会得到 `no_candidate` 并拒绝，不再把 0 点写进已确认表——
  这条现在是真实存在的破坏路径：旧代码会用一个「读出来是空」的损坏候选覆盖掉整张已确认表；
- `vg_live_points_load()`（第 617 行）本来就对任何非 0 返回做 `memset`，行为不变。

文件不存在仍是 `-ENOENT`：演示流程要求删掉 `points.json` 后能看到空表，这条不能变。

### 4. 命令层（R3、R8）

`cmd_list`（`vgpoint.c:458-462`）：

```c
  ret = vg_point_table_read(&sum, path);
  if (ret != 0 && ret != -ENOENT)
    {
      return reply_err("list", "io", vg_point_table_err_token(ret));
    }

  if (ret != 0)
    {
      memset(&sum, 0, sizeof(sum));
    }
```

`cmd_get`（第 743 行）对已确认表的读取做同样处理。`cmd_add` / `cmd_set` / `cmd_del`
把 `msg` 由字面量 `candidate_io` 换成 `vg_point_table_err_token(rc)`，`code=io` 不动。

`cmd_test` / `cmd_apply` 不改：候选读失败时它们本来就回 `no_candidate`，
不写盘，不会造成破坏。

## 兼容性

- 协议：`code` 集合不变，`table` / `n` 字段不变，只有 `msg` 内容变化。
- 上位机：只解析 `code`；`list` 在存储正常时的输出逐字不变。
- 演示流程：删掉 `points.json` 仍是 `OK n=0`。
- host 测试：`test_vgpoint.c:245-255` 的空表用例由 `write_candidate` 写出，含 `"points":[]`，
  不会被 R6 判成损坏。

## 风险与回滚

| 风险 | 处置 |
| --- | --- |
| 新 token 让某些旧脚本按 msg 匹配失败 | 全仓与上位机都只按 `code` 判断，已核对 |
| R6 把某个合法但无 `points` 键的文件判成损坏 | `write_candidate` 永远写出 `points` 数组；板端与上位机都不产出无 `points` 的文件 |
| R5 让「写一半」从静默成功变成失败 | 这是目标行为；半成品文件会被 `unlink` |
| 板端恢复失败 | 代码改动与存储恢复解耦：AC5/AC6 在主机就能验，板端验证另算 |

回滚：三处代码改动互相独立，`git revert` 单个提交即可，不涉及数据迁移。

## 待板端确认

R1 的分支结论会决定是否需要追加改动。若落到「挂载正常但写路径本身异常」，
就要把 `../nuttx` 里未提交的 `stm32_sdmmc.c` 一并纳入，那超出本任务边界，需重新确认。
