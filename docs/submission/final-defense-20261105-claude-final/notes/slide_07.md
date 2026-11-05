（本页约 28 秒）

只读不是提示词，是 C 代码。注册层只放 5 个只读工具，vgpoint、vgcfg、vgdiscover apply 根本不注册。运行层在 C 里强制参数和频率：点位查询超过 48 字节直接拒绝，输出上限 2048 字节，列表最多 12 条。审计层每次执行都计数，写 /data/velaguard/logs/agent_tools.log，单文件 64 KiB 轮转。
