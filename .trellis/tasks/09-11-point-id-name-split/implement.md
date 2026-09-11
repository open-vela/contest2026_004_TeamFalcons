# 点表 id+name 动手顺序

依赖：先改 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 第 4/5/6/7 节，再改代码。不实现告警比较。

## 清单

- [x] 规范：主键 `id`、显示 `name`；命令 `-i`/`-N`；`POINT`/`READ`；错误码 `dup_id`/`no_id`；删掉作为主键的 `tag`
- [x] `.trellis/spec/backend/host-nsh-protocol.md`：补一句「索引按 id，禁止按 name；不兼容旧 tag」
- [x] `vg_point_entry` / `vg_runtime_point_t`：`id[24]` + `name[48]`；校验与 `find_id`
- [x] `vg_point_table` 读写、推断 `add_point`、`format_point`；仅有 `tag` 的对象丢弃
- [x] `vgpoint.c` 全部 verb 改按 `id`；`dup_id`/`no_id`
- [x] `vg_ui_backend_board.c` + `vg_model_import_runtime_points`：HMI `id`/`name` 分栏
- [x] Host 单测：同名两点、中文 name 往返、旧 tag JSON → 0 点、稳定行 `id=`
- [x] `scripts/vgpoint_demo_points.json` + `vgpoint_host_apply.ps1`：`id`/`name`、UTF-8、超长则拆 `set -N`
- [x] `make -C app/velaguard/host_tests test`
- [x] `bash scripts/build.sh`

## 校验

```bash
make -C app/velaguard/host_tests test
bash scripts/build.sh
```

COM3 可用且用户同意改已确认表时（本任务不默认自动 apply）：

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts/vgpoint_host_apply.ps1
```

## 风险文件

- `docs/velaguard-host-nsh-protocol.md`：命令表唯一来源
- `app/velaguard/vg_discover.h`、`vg_point_table.c`、`vgpoint.c`
- `gui/main/ui/model/vg_model.h`、`vg_model.c`
- `app/velaguard/host_tests/test_vgpoint.c`
- `scripts/vgpoint_host_apply.ps1`、`scripts/vgpoint_demo_points.json`

## 回滚

1. 恢复 `tag` 字段与 `find_tag`（git 还原上述文件）
2. 上位机改回 `-t`
3. 板上重新灌旧 demo 点表
