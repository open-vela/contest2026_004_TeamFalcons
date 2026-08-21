# ESP_AT_Lib 评估（khoih-prog）

来源：https://github.com/khoih-prog/ESP_AT_Lib  
对照：本仓库已验证 ESP-01S AT 1.7.4.0 + 上游 `apps/netutils/esp8266`（`lesp_*`）。

## 结论

**本轮不引入、不 vendor、不移植该库。** 只把它当 AT 命令对照表。Wi-Fi bearer 继续用 NuttX `CONFIG_NETUTILS_ESP8266`。

## 库是什么

- Arduino C++ 封装，基于 ITEADLIB_Arduino_WeeESP8266。
- 面向 ESP8266/ESP32/WizFi360 **AT 透传盾**，经 UART 发 AT。
- MIT；**2023-01-29 起 GitHub archive，只读、无维护**。
- README 写明 AT Firmware **v1.7.4.0** 可用，与当前模组版本一致。这是唯一对 VelaGuard 有价值的信息。

API 与 `lesp_*` 高度同构：

| ESP_AT_Lib | NuttX `lesp_*` |
|---|---|
| `kick()` / `AT` | `lesp_initialize` |
| `restart()` / `AT+RST` | `lesp_soft_reset` |
| `joinAP(ssid,pwd)` | `lesp_ap_connect` |
| `getLocalIP()` | `lesp_get_net` |
| `createTCP` / `send` / `recv` | `lesp_socket` / `lesp_connect` / `lesp_send` / `lesp_recv` |

## 为什么不能用

1. **运行时绑定 Arduino**：`HardwareSerial`、`String`、`Arduino.h`。openvela/NuttX 应用是 C + POSIX，没有 Arduino core。
2. **C++ 堆字符串** 不适合 H750 上常驻故障转移线程。
3. **与已有栈重复**：`lesp_*` 已覆盖 station、TCP、复位；再引进一份 AT 解析会争用 `/dev/ttyS1`。
4. **已停止维护**，AT 1.7.4 之后的行为无上游修复。
5. **许可证虽 MIT 可兼容 Apache 2.0**，但移植成本高于收益；竞赛交付应落在 contest 仓 C 代码 + 既有 NuttX netutils。

## 可借鉴、不抄代码

- AT 命令集合与超时习惯（joinAP、RST、kick）。
- 不借鉴：Arduino 示例工程、`String` API、HTTP GET 助手、多 UART 板级补丁。

## 后续 MQTT

即使以后做 MQTT over ESP，也应在 `lesp_*` 上做 transport 适配，而不是引入 ESP_AT_Lib。
