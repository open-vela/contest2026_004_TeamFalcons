# MQTT-only pull based OTA

状态：已被替代（2026-10-18）。当前决策为 [ADR 0006：MQTTS控制与HTTPS固件下载](0006-mqtts-control-https-download-ota.md)。以下保留旧决策及当时理由，不再作为实现约束。

VelaGuard uses MQTT/TLS for OTA control and firmware chunk transfer instead of adding a board-side HTTPS downloader. HTTPS would be a common production choice for large firmware delivery, but this project values a single authenticated MQTT path, simpler embedded networking, Broker ACL reuse, and tighter integration with local confirmation, progress reporting, and rollback.
