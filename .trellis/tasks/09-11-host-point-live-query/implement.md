# 点位当前值查询动手顺序

依赖：先改 [`docs/velaguard-host-nsh-protocol.md`](../../../docs/velaguard-host-nsh-protocol.md) 增加 `get` / `VALUE` / `no_sample`。

## 清单

- [x] 规范第 1/5/6/7 节：`vgpoint get`、`VALUE` 行、`no_sample`；`test`/`READ` 不变
- [x] `.trellis/spec/backend/host-nsh-protocol.md`：查询读快照、不占总线
- [x] 快照读写与 `vg_point_format_value`（`vg_point_table.c` / 头文件）
- [x] HMI 采集线程每轮结束后原子写 `/data/velaguard/live/values.txt`
- [x] `vgpoint get` [`<id>`]：读快照，不 `vg_bus_try_lock`
- [x] Host 单测：解析、过滤 id、缺文件、`VALUE` 格式
- [x] `make -C app/velaguard/host_tests test`
- [x] `bash scripts/build.sh`

## 校验

```bash
make -C app/velaguard/host_tests test
bash scripts/build.sh
```

板端（可选，需已烧录且 HMI 在采）：`vgpoint get` 与 `vgpoint get temp` 打出 `VALUE` 且无 `bus_busy`。

## 风险文件

- `docs/velaguard-host-nsh-protocol.md`
- `app/velaguard/vg_point_table.c`、`vg_discover.h`、`vgpoint.c`
- `app/velaguard/vg_ui_backend_board.c`
- `app/velaguard/host_tests/test_vgpoint.c`

## 回滚

去掉 `get` 与快照写出；采集与写点表路径保持不变。
