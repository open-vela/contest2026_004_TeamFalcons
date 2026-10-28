# VelaGuard MQTT 候选接入协议草案 v0.3

日期：2026-10-28。状态：通信协议设计，未实现、未部署、未板验。

本文是云端配置服务与板端之间候选接入消息的唯一规范入口，替代本文件 v0.2 草案。配合 [网页接入业务协议](velaguard-web-sensor-onboarding-protocol.md)、[实施计划](velaguard-web-sensor-onboarding-plan.md) 和 [ADR 0007](adr/0007-mqtt-candidate-import-with-local-confirmation.md) 使用。

v0.3 补齐设备能力查询、启动与连接标识、open 防重放、候选对象、回执、恢复和字节预算。文件草案版本与线上 `schema_version` 分开：此前无已实现的候选接口，首版线上结构仍定义为 1；不承诺兼容 v0.2 草案的消息形状。首版投产后，不兼容修改必须增加线上版本。

## 1. 权限与业务边界

- 浏览器通过受鉴权 HTTPS 访问配置服务；不直接连接 Broker。PDF、页面图片、AI 请求响应、语音和音频不走候选主题。
- 云端模型只输出候选数据。云端配置服务负责权限与会话编排，板端确定性代码负责校验和提交；设备端 Agent 无权调用配置接口。
- 仅新增一个从机地址上的传感器，保留全部已有点位。已有从机地址、重复点位 ID、空选择、不支持的类型或容量不足整批拒绝。
- 配置校验不打开 RS485、不发 Modbus 请求。板端校验通过后仅弹出人工二次确认；确认后直接添加，再由正常采集产生真实读数和在线状态。
- 远程进度、等待和结果只在手机或电脑网页展示。板端不增加远程进度页、等待页或结果提示，本地告警优先。
- 不提供 test、apply、confirm、NSH 字符串、寄存器写入、清除告警或输出控制操作。既有 NSH 仍按 [NSH 协议](velaguard-host-nsh-protocol.md) 的独立人工确认流程使用。

## 2. 传输与 Broker 策略

首版使用 MQTT 3.1.1 over TLS。板端主动建立出站连接，校验 Broker 证书链和主机身份；每设备独立凭证，需接入现有 provision 加密存储机制，MQTT 凭证装载能力仍待实现。证书校验所需的可信时间与信任材料在实施时验证，失败时远程配置不可用，不关闭校验绕过。明文公网链路不开放候选功能。

| 主题 | 发布者 | 订阅者 | QoS | retained |
|---|---|---|---|---|
| `vg/{device_id}/config/request` | 获授权的配置服务 | 对应板端 | 1 | false |
| `vg/{device_id}/config/state` | 对应板端 | 获授权的配置服务 | 1 | false |

ACL 同时隔离设备范围和方向；遥测看板仍只读，配置服务不能伪造板端状态。消息内 `device_id` 必须匹配主题和已授权身份；payload 标识不替代认证。

板端使用 `CleanSession=1`，每次连接完成 CONNACK 与配置主题 SUBACK 后才接受请求；配置服务订阅就绪后才能下发。MQTT 接收回调只做有界复制/入队，校验、存储和 UI 工作在后台完成。

配置发布端禁止 retained；Broker 必须通过可验证的策略或插件拒绝配置主题 retained 发布，并清理该命名空间的历史 retained 数据。板端仍拒绝收到 `RETAIN=1` 的配置消息，但不能只靠此检查：MQTT 3.1.1 对匹配既有订阅的转发会使用 `RETAIN=0`，与发布者原标志无关。`CleanSession=1` 也不会删除 retained 数据。[MQTT 3.1.1 第 3.1.2、3.3.1 节](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)

QoS 1 允许重复投递；PUBACK 仅是传输确认。应用层按请求标识、会话和版本去重，不声称端到端只执行一次。现有 status/telemetry/alarm/point_table 主题的用途保持，不能用它们代替本协议的实时会话查询或提交回执。

## 3. 编码与身份

全部业务 payload 为无 BOM 的 UTF-8 JSON。拒绝非法 UTF-8、未配对 Unicode 代理项、任意层级的重复对象键、未知版本、未知操作、非有限数、越界整数及本消息结构未定义的字段。解码后的所有字符串禁止 U+0000；型号和标识不含控制字符，evidence.quote 只额外允许 LF 换行。不同请求按 op/scope 分别校验，不能把缺失字段补成有副作用的操作。

| 字段 | 含义与格式 |
|---|---|
| `schema_version` | 请求和响应均为整数 1 |
| `device_id` | 现有网关身份，`vg-` 加 24 位小写十六进制，不是传感器地址 |
| `req_id` | 单条应用请求标识，`[A-Za-z0-9_-]{1,64}`；在对应设备的保留任务范围内唯一，重发保持相同标识和内容 |
| `task_id` | 一次用户下发对应的配置任务，格式同 req_id；等于 HTTPS 的 dispatch_id |
| `boot_id` | 配置服务使用的启动身份，128 位随机值编码为 32 位小写十六进制；每次板端启动重建 |
| `link_id` | 当前配置 MQTT 连接身份，同上；每次重新连接并订阅成功后重建 |
| `session_id` | 板端接受 open 后生成的独立 128 位随机会话身份 |
| `confirmation_id` | 板端为当前候选生成的本地确认关联值；不是远程确认凭证 |
| `revision / state_seq` | 无符号 32 位整数；不得回绕复用 |
| `*_hash / *_sha256` | SHA-256 的 64 位小写十六进制；允许 null 的场景单独说明 |

随机身份必须使用经验证的随机来源，不能仅由 UID、启动毫秒数或可回绕计数拼接。当前遥测/告警代码中的数值型启动标记不具备本规范语义，不能直接拿来作为配置 boot_id，也不要求改动既有告警 ID。

单网关只允许一个配置会话。云端解析任务不占板端会话；登录会话、解析任务、配置会话和 MQTT 连接分别管理。

## 4. 先查询设备能力

`query` 为只读操作，`scope` 只允许 device 或 task，不新建/续租会话、不弹窗、不占 RS485。两个查询形状固定：

- `scope=device`：只有 schema_version、device_id、req_id、op、scope。用于取得当前身份、能力、运行表和占用情况。
- `scope=task`：增加 task_id，读取该任务当前状态或最近提交回执；不要求旧 boot/session，便于重启、断线后核对结果。

### 4.1 设备查询示例

以下全部标识、页码、摘要与数据为协议示例，不是真机或手册识别证据。

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "req_id": "rq-device-001",
  "op": "query",
  "scope": "device"
}
```

设备返回 `kind=device_snapshot`，包含相同 req_id，以及：

| 字段 | 必需内容 |
|---|---|
| `boot_id / link_id` | 当前配置启动与连接身份 |
| `next_open_seq` | 本启动下下次 open 所需序号，初值 1，接受 open 后严格递增 |
| `config_state` | valid、empty 或 unavailable；unavailable 禁止 open |
| `base_hash` | 当前持久化已确认表原始字节摘要；仅干净空配置为 null |
| `point_count / point_ids / slave_addresses` | 当前运行表的数量、ID 和从机地址；数量均有界 |
| `active_task_id` | 活动任务标识，无则 null |
| `capabilities` | remote_config、protocol_version、candidate_schema_version、功能码、类型、数量、固定波特率、字节上限与租约预算 |
| `uptime_ms / time_quality` | 本地单调运行时间；UTC 不可用时明确 unsynced |

字段形状示例；其中容量为待验证的目标预算，实际能力必须如实上报：

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "kind": "device_snapshot",
  "req_id": "rq-device-001",
  "boot_id": "11111111111111111111111111111111",
  "link_id": "33333333333333333333333333333333",
  "next_open_seq": 1,
  "config_state": "empty",
  "base_hash": null,
  "point_count": 0,
  "point_ids": [],
  "slave_addresses": [],
  "active_task_id": null,
  "capabilities": {
    "remote_config": true,
    "protocol_version": 1,
    "candidate_schema_version": 1,
    "function_codes": [
      3,
      4
    ],
    "data_types": [
      "int16",
      "uint16"
    ],
    "quantities": [
      1
    ],
    "baud": 9600,
    "max_points": 32,
    "max_request_bytes": 16384,
    "max_state_bytes": 4096,
    "lease_ms": 600000
  },
  "uptime_ms": 10000,
  "time_quality": "synced"
}
```


首版能力集合为 `fc=[3,4]`、`dtype=[int16,uint16]`、`qty=[1]`、`baud=9600`。qty=1 是新远程入口对单个 16 位测点的明确约束；不修改既有 NSH 的数量范围。上报实际有效预算，不能按希望支持的容量上报。

运行表文件缺失且内存/配置状态一致为空时可报 empty/null；文件损坏、存储不可达或内存/持久化不一致必须报 unavailable，不能伪装成空表。

云端必须匹配本次查询 req_id，并在建议 10 秒的新鲜度窗口内使用响应。保留的 status 只能辅助显示连接情况，不能作为当前 boot_id、link_id、base_hash 或 next_open_seq 的来源。

## 5. open：建立会话

open 必须携带当前查询得到的 boot_id、link_id、next_open_seq 对应的 `expected_open_seq` 和 base_hash。task_id 此前不能已经获得其他配置会话，且 open 不携带候选；明确拒绝、尚未获会话的任务可以按重试规则再次尝试。

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "req_id": "rq-open-001",
  "task_id": "cfg-example-001",
  "op": "open",
  "boot_id": "11111111111111111111111111111111",
  "link_id": "33333333333333333333333333333333",
  "expected_open_seq": 1,
  "base_hash": null
}
```

板端在同一互斥区校验身份、连接、序号、当前表摘要与配置入口占用；接受后消耗该 open 序号并生成 session_id。序号在本次启动内不因任务结束或重连而归零。达到上限时停止接受新远程会话，不能回绕。

当前身份和连接通过检查后，先匹配重复请求，再检查新请求的 open 序号。重复的同 req_id/同内容返回已有处理结果与当前状态，不延长租约。旧任务结果已不在缓存时，消耗过的 expected_open_seq 仍阻止旧 open 再次建立会话。相同序号的并发 open 只能有一个成功。被明确拒绝的请求不获得会话。

租约从 open 接受时按板端单调时钟开始，建议 600000 ms，不续租。初始 revision=0，candidate_hash、payload_sha256、confirmation_id 为 null，state=opened。租约包括校验和等待本地确认，PDF 解析在此之前完成。

## 6. put：发送完整候选

put 必须携带当前 task、boot、link、session、base_hash，以及 expected_revision、revision、payload_sha256 和 candidate_json。不允许局部补丁或写文件路径。

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "req_id": "rq-put-001",
  "task_id": "cfg-example-001",
  "op": "put",
  "boot_id": "11111111111111111111111111111111",
  "link_id": "33333333333333333333333333333333",
  "session_id": "22222222222222222222222222222222",
  "expected_revision": 0,
  "revision": 1,
  "base_hash": null,
  "payload_sha256": "a90dc6eef1ccc60a8eb8c660d85d49d2d8c8089262495742d9f652790659434a",
  "candidate_json": "{\"candidate_schema_version\":1,\"mode\":\"add_sensor\",\"bus\":{\"baud\":9600},\"sensor\":{\"slave_address\":1,\"model\":\"TH-01\"},\"manual\":{\"manual_id\":\"man-example-01\",\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},\"evidence\":[{\"id\":\"ev1\",\"page\":12,\"quote\":\"温度寄存器 0x0000，int16，数值乘以 0.1，单位 C；地址从 0 开始。\"}],\"points\":[{\"id\":\"s1_f3_r0\",\"name\":\"温度\",\"addr\":1,\"fc\":3,\"reg\":0,\"qty\":1,\"dtype\":\"int16\",\"scale\":0.1,\"offset\":0,\"unit\":\"C\",\"source\":{\"evidence_ids\":[\"ev1\"],\"original_address\":\"0x0000\",\"address_base\":0}}]}"
}
```

- 初次 put 为 expected_revision=0、revision=1；之后只能 expected_revision=n、revision=n+1。重复请求返回原处理结果，不能重复落盘或弹窗。
- `candidate_json` 是 JSON 字符串。先解开外层字符串，再对得到的原始 UTF-8 字节计算 payload_sha256，核对后解析候选。禁止重新排序、格式化或 Unicode 归一化后再验签名式摘要。
- payload_sha256 是完整性与版本关联摘要，不是数字签名；信任边界来自 TLS、设备认证与 ACL。
- 新版请求通过身份、期望版本、摘要及受理检查后，立即使旧 confirmation_id 失效并关闭旧确认，再进入 validating。完整校验和候选存储成功才推进 revision。
- validating 中串行处理候选；后来的 put 可返回 validation_busy。取消、会话失效和旧后台结果用处理代次隔离，迟到结果不能复活确认。
- 新版内容校验失败时，保留旧内容用于审计，revision 不提升，进入 opened 且没有可确认候选；不会恢复旧弹窗。修正后以同一 n+1、新 req_id 发送完整候选。
- 在身份/摘要/版本检查阶段就被拒绝的消息不视为新版受理，不改变当前候选或确认。

### 6.1 candidate_json 对象

以下对象为上一 put 的内容，排版用于阅读。put 内实际使用紧凑序列化字节；示例 payload_sha256 仅对应 put 字符串解码后的那份字节。

```json
{
  "candidate_schema_version": 1,
  "mode": "add_sensor",
  "bus": {
    "baud": 9600
  },
  "sensor": {
    "slave_address": 1,
    "model": "TH-01"
  },
  "manual": {
    "manual_id": "man-example-01",
    "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
  },
  "evidence": [
    {
      "id": "ev1",
      "page": 12,
      "quote": "温度寄存器 0x0000，int16，数值乘以 0.1，单位 C；地址从 0 开始。"
    }
  ],
  "points": [
    {
      "id": "s1_f3_r0",
      "name": "温度",
      "addr": 1,
      "fc": 3,
      "reg": 0,
      "qty": 1,
      "dtype": "int16",
      "scale": 0.1,
      "offset": 0,
      "unit": "C",
      "source": {
        "evidence_ids": [
          "ev1"
        ],
        "original_address": "0x0000",
        "address_base": 0
      }
    }
  ]
}
```

根对象只允许下列字段：

| 字段 | 约束 |
|---|---|
| `candidate_schema_version` | 整数 1；与线上消息版本、现有 points.json 版本分别解释 |
| `mode` | 固定 add_sensor |
| `bus` | 只允许 baud=9600；不接受设备路径、扫描参数或其他串口设置 |
| `sensor` | slave_address 为 1..247；model 为有依据的型号，UTF-8 1..64 字节 |
| `manual` | manual_id 为 `[A-Za-z0-9_-]{1,64}`；sha256 是原始 PDF 摘要 |
| `evidence` | 1..32 条；每条 id 唯一，格式 `[A-Za-z0-9_-]{1,32}`；page 为从 1 开始的 PDF 物理页序；quote 为 1..160 UTF-8 字节的原文摘录 |
| `points` | 1..32 条新增测点；最终数量受已有表剩余容量约束 |

每点只允许如下字段，不接受 cmp、warn、crit、fail_n、命令或任意扩展执行字段：

| 字段 | 约束 |
|---|---|
| `id` | `[A-Za-z0-9_]{1,23}`，全运行表唯一；云端按 `s{addr}_f{fc}_r{reg}` 稳定生成 |
| `name` | 1..47 UTF-8 字节；沿用既有点名检查，不含 ASCII 空白、控制符、等号、双引号和反斜杠 |
| `addr` | 与 sensor.slave_address 一致 |
| `fc / reg` | fc 为 3 或 4；reg 为 0..65535 的协议地址 |
| `qty / dtype` | qty 固定 1；dtype 为 int16 或 uint16 |
| `scale` | 有限、非零 JSON 数，转换到实际运行浮点类型后仍有限且非零；不能溢出或下溢后静默改变含义 |
| `offset` | 显式为 0；非零偏移不支持 |
| `unit` | 0..7 UTF-8 字节；不含 ASCII 空白、控制符 |
| `source` | 必需 evidence_ids、original_address、address_base；不能缺少依据 |

source.evidence_ids 为 1..2 个存在的 evidence ID。original_address 最多 32 ASCII 字节，只接受十进制数字或以 0x 开头的十六进制数字，数值须在 0..105536；解析须检测溢出，不能回绕。address_base 只允许 0、1、30001、40001。板端核对 `reg = 原文地址数值 - address_base`，且结果在 0..65535；30001 必须对应 FC04，40001 必须对应 FC03。选用的地址基准必须有手册依据，无法确认时不给出可下发测点。

每条 evidence 必须被至少一个所选测点引用；不得在候选中附带整份手册、页面图像、私有下载 URL、密钥或无关内容。选择之外的完整解析结果留在云端。

### 6.2 校验、合并与持久化边界

板端核对整批候选的结构、字段字节数、能力、地址换算、来源引用、已有地址/ID 冲突、运行表基础摘要、容量和消息大小。任一点失败整批拒绝，不自动删点、改地址、改倍率、改类型或截断字段。

已有运行点保持不变。新增运行点由确定性代码转换为现有 points.json 字段，来源信息与原候选独立持久化，按任务和摘要关联；不要求现有运行表直接识别 source、evidence 或 offset。新增点不推导模拟量告警阈值，既有规则与离线逻辑保持。

设备预先生成将要提交的完整运行表字节并计算 candidate_hash；确认后提交同一份字节，不在点击时重新读取一个可变的最新候选。本地确认绑定 task、session、revision、payload_sha256、base_hash、candidate_hash 和 confirmation_id。

## 7. 状态、响应与取消

state 主题只允许四类 `kind`：

| kind | 内容 |
|---|---|
| device_snapshot | 设备查询的实时能力/配置快照，req_id 必须匹配查询 |
| state | 某活动会话或同启动终态的完整状态快照；由请求或本地事件触发 |
| request_error | 某请求被拒绝的结果，关联 req_id，包含 code、retryable 和有界字段问题；不能把请求拒绝误当成原任务失败 |
| receipt | 已持久化的最近提交结果，用于重启后的查询；注明原始启动/会话身份 |

对已安全识别的拒绝请求，request_error 使用以下完整形状。无法取得的 task_id/session_id 为 null；error 结构与 state.error 一致。本类不含 state_seq，也不作为任务状态推进依据。

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "kind": "request_error",
  "req_id": "rq-put-002",
  "boot_id": "11111111111111111111111111111111",
  "link_id": "33333333333333333333333333333333",
  "task_id": "cfg-example-001",
  "session_id": "22222222222222222222222222222222",
  "error": {
    "code": "revision_conflict",
    "retryable": false,
    "field_errors": [
      {
        "path": "/expected_revision",
        "reason": "期望版本与当前已受理版本不一致"
      }
    ]
  },
  "uptime_ms": 46000,
  "time_quality": "synced"
}
```


### 7.1 state 示例及字段

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "kind": "state",
  "req_id": "rq-put-001",
  "boot_id": "11111111111111111111111111111111",
  "link_id": "33333333333333333333333333333333",
  "task_id": "cfg-example-001",
  "session_id": "22222222222222222222222222222222",
  "state_seq": 3,
  "state": "awaiting_local_confirm",
  "revision": 1,
  "pending_revision": null,
  "base_hash": null,
  "payload_sha256": "a90dc6eef1ccc60a8eb8c660d85d49d2d8c8089262495742d9f652790659434a",
  "candidate_hash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "confirmation_id": "44444444444444444444444444444444",
  "lease_remaining_ms": 575000,
  "error": null,
  "uptime_ms": 45000,
  "time_quality": "unsynced"
}
```

state 必需包含示例所列字段。req_id 在请求触发时为对应请求标识，本地确认等主动事件时为 null；pending_revision、摘要、确认身份、error 未产生时为 null。error 非空时为 `{code,retryable,field_errors}`，field_errors 最多 8 条，每条含 JSON 路径和简短原因，不附带完整候选或凭证。

state_seq 在会话内从 1 严格递增，仅在状态机或候选有变化时推进；运行时间、剩余租约和请求关联字段变化不要求推进序号。云端以 device、boot、session 为范围保存快照，只接纳较新状态；同序号可更新请求关联、观测时间和剩余租约估计，不能覆盖不同状态。终态不被等待状态覆盖。不同 kind 分开处理。

响应中的 boot_id、link_id 为报告时的当前身份；重连后查询同启动的旧会话终态时 link_id 可以更新。变更请求仍必须满足会话原连接绑定，不能拿查询返回的新 link_id 恢复旧确认。不同启动的提交结果只通过 receipt 返回。

| state | 允许的主要后续 |
|---|---|
| opened | put、cancel 或租约过期 |
| validating | 校验成功到 awaiting_local_confirm；可修正错误回 opened；取消/过期结束 |
| awaiting_local_confirm | 本地确认到 committing；新版 put 重新校验；取消/断线/过期结束 |
| committing | 完成落盘到 applied；可证明未生效的失败到 failed；结果不明到 recovery_unknown；拒绝 cancel/put |
| applied | 终态；配置生效，尚不能据此宣称采集在线 |
| cancelled / expired / failed | 终态；查询保留实际结果，不恢复弹窗或自动重试提交 |
| recovery_unknown | 结果待核对；禁止新配置和自动补交，依据运行表与提交记录恢复 |

取消使用 op=cancel，携带 schema_version、device_id、req_id、task_id、boot_id、link_id、session_id；取消整个会话，不提供远程确认参数。处于 committing 或 applied 时返回 too_late；recovery_unknown 返回 recovery_unknown，均需查询实际结果。用户在板端点击取消与远程 cancel 走同一状态互斥区。

租约过期、断线或取消在 committing 前使确认立即失效并关闭；断线按 cancelled/link_lost 结束，重连不自动恢复。若板端因重启无法恢复非提交阶段的任务事实，则返回 unknown_task，云端不能据此补报成功。

所有确认操作在同一提交互斥区重新检查身份、版本、租约、基础表与确认绑定。进入 committing 后租约结束或网络断开不打断已开始的本地提交；结果由实际持久化决定。板端确认的双击只进入一次提交。

### 7.2 任务查询示例

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "req_id": "rq-query-001",
  "task_id": "cfg-example-001",
  "op": "query",
  "scope": "task"
}
```

query 不改变状态或延长租约。未知任务返回 unknown_task；这只表示设备当前无法提供结果，不等同于明确未应用。

## 8. 断线、去重与异常恢复

- 同连接、同会话内的传输超时，可按 1、2、4 秒退避最多重发三次，保持 req_id 与完整 payload 不变。重复请求必须返回原结果，不重复 I/O、不推进 revision、不延长租约。
- 已明确收到 retryable=true 的业务拒绝后，若用户下发任务仍有效，可重新查询并以新 req_id 做有界尝试；最多三次且仍受租约约束。不能把业务重试与结果未知时的原请求重传混为一谈。
- 相同 req_id 不同内容返回 req_conflict。哈希按解码后字符串原字节计算；不得生成新 req_id 来绕过结果不明的请求。
- 网络重连、配置服务重启后，只自动 query 对账，不自动 replay open/put。新的用户下发使用新的 task_id、实时能力与 open 序号。
- 终态在本次启动内至少保留到下一个 open；任务去重结果被回收后，已消耗的 open 序号、不同 link_id、过期/未知 session 仍阻止旧操作再次提交。
- query 按设备限流，建议每秒不超过一次；云端聚合多个浏览器的查询，不把网页两秒轮询直接透传为 MQTT 查询。
- 单会话建议最多缓存 31 个不同 open/put 请求和一个专用 cancel 结果槽；query 无副作用，不计入变更请求去重预算。取消不能因普通请求预算耗尽而被阻止。

提交前必须保存有界提交记录，关联 task、origin_boot_id、session、revision、payload_sha256、base_hash、candidate_hash 与来源。落盘成功且运行表可验证后才回报 applied，并保存回执；来源与候选字节也必须可核对。

重启后核对实际运行表、提交记录及来源摘要：

| 情况 | 返回 |
|---|---|
| 实际表与已记录预期 candidate_hash 一致，完整可读且来源记录有效 | receipt/applied，包含 applied_hash |
| 可证明仍是旧表且未生效 | receipt/failed，说明实际失败原因 |
| 记录缺失、损坏、表异常或无法判定 | recovery_unknown；不盲目补交，不推测成功 |

receipt 以当前 boot_id/link_id 报告，并单独携带 origin_boot_id、原 task/session/revision、payload_sha256、base_hash、candidate_hash、applied_hash 和 outcome。云端只按匹配的原始任务接受该结果，不能用它更新新会话的 state_seq。板端只要求持久保存最近一次提交回执；更早结果主要由云端保存，找不到时仍须返回 unknown_task。

receipt 完整形状示例；applied 时 applied_hash 必须等于已核对的 candidate_hash；failed/unknown 时 applied_hash 为 null，error 说明原因：

```json
{
  "schema_version": 1,
  "device_id": "vg-00112233445566778899aabb",
  "kind": "receipt",
  "req_id": "rq-query-002",
  "boot_id": "55555555555555555555555555555555",
  "link_id": "66666666666666666666666666666666",
  "origin_boot_id": "11111111111111111111111111111111",
  "task_id": "cfg-example-001",
  "session_id": "22222222222222222222222222222222",
  "revision": 1,
  "payload_sha256": "a90dc6eef1ccc60a8eb8c660d85d49d2d8c8089262495742d9f652790659434a",
  "base_hash": null,
  "candidate_hash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "applied_hash": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "outcome": "applied",
  "error": null,
  "uptime_ms": 5000,
  "time_quality": "synced"
}
```


临时文件、备份和提交记录不等于 FAT 掉电原子性；实现与板验必须证明实际恢复行为。无法核对的结果在网页显示待核对，不能展示添加失败后立即自动重试。

## 9. 错误码与处理

| code | 语义与处理 |
|---|---|
| protocol_unsupported / bad_message | 版本、字段、JSON 或操作不支持；不执行 |
| device_mismatch / retained_request | 错设备或保留消息；拒绝并有界记录 |
| stale_boot / stale_link / stale_open_seq | 查询最新身份和序号；旧变更请求不重放 |
| config_unavailable / base_conflict | 配置不可读或运行表变化；核对后重新开始 |
| config_busy / validation_busy | 配置入口或校验忙；只有明确可重试时有界重试 |
| session_unknown / lease_expired | 原会话不能再使用；不能恢复旧确认 |
| revision_conflict / req_conflict | 版本或请求内容冲突；重新查询 |
| invalid_candidate / unsupported_point | 候选字段、来源或能力不满足；回到网页预览处理 |
| address_conflict / point_id_conflict / capacity_exceeded | 与现有表冲突或容量不足；不自动覆盖 |
| payload_too_large / request_limit | 完整消息或请求预算超限；不截断 |
| too_late | 已进入提交或已应用，取消无效；查询结果 |
| storage_error | 存储失败；按实际恢复结果处理 |
| unknown_task / recovery_unknown | 无法确定结果；保持待核对，不自动补交 |

错误返回不能改变一个未被该请求合法更新的活动任务。无效身份请求若能解析出合法关联字段，可返回最小错误；无法安全解析或超过包长时允许丢弃并限流记录。任何错误日志都不输出完整凭证、候选全文或 PDF。

## 10. 容量与当前实现差距

下表为规划预算与已知事实，实际交付取固件能力、Broker 配置和云端限制的最小值：

| 项目 | 首版设计 |
|---|---|
| 完整 MQTT 请求 payload | 最多 16384 UTF-8 字节，包含外层 JSON、转义后的 candidate_json 与所有来源字段 |
| 单条配置状态 payload | 最多 4096 UTF-8 字节；只返回摘要和有界问题，不回显候选 |
| 运行表点数 | 既有点位加新增点位最多 32 |
| 租约 | 建议 600000 ms，不自动延长 |
| 并发 | 每网关一个配置会话和一个活动候选；新旧候选短暂共存须计入内存预算 |
| MQTT 查询 | 建议每网关每秒最多一次；正常等待状态由事件主动上报，必要时补查 |
| 证据预算 | 每条摘录最多 160 UTF-8 字节；每个候选最多 32 条，仍受完整包长约束 |

外层 JSON 的双引号/换行转义会增加消息大小，不能把 candidate_json 的未转义长度直接当作 MQTT 包长。MQTT 固定头、主题和包标识还会增加接收缓冲需求。

当前 `vg_mqtt_session.c` 的 RX 缓冲只有 512 字节，不能接收上述候选预算；当前数值启动标记、忽略消息的接收回调、明文连接和既有点表解析也不具备本协议能力。这些是后续实施项，本轮不修改代码，不以文档证明已支持。

## 11. 实施验收

实现前用黄金消息检查编码、候选摘要、范围与状态转换；实现后至少覆盖：

1. query → open → put → 本地确认 → applied 的真实闭环，确认前零新增候选 Modbus 请求。
2. 未确认、取消、超时、告警抢占、旧确认、新候选校验失败均不能错误应用。
3. 同 req_id 重复、异内容、乱序、旧 open 序号、旧 link/boot、缓存回收后的重放均不能再提交。
4. retained 发布端、Broker 策略、重订阅、CleanSession 与延迟消息组合，不能绕过应用会话检查。
5. 表缺失为空与表损坏、来源不足、超长中文、多字节单位、float32/32 位数据、地址基准和整包容量的明确拒绝。
6. NSH/扫描入口互斥，原 apply 不能读取远程候选，设备端 Agent 不可访问。
7. 断网、云端重启、板端重启、落盘失败、提交与取消竞争以及结果未知的真实恢复证据。
8. 网页状态与实际运行表、采集和告警一致，配置应用与采集在线分别记录。
