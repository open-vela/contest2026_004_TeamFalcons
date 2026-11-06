（本页约 34 秒）

OTA 从一条 offer 开始。MQTTS 8883 订阅 offer，发布 status、result、confirm，九字段不超过 1536 字节。镜像走 HTTPS，Range 续传，先取 512 字节 manifest，再流式下载到 eMMC。校验是 TLS、清单 CRC、ECDSA、流式 SHA-256 和回读。READY 后必须本机长按 2 秒，5 项联锁通过才写 pending BCB。重启后只有 boot stub 擦写 B 槽，BCB 两份 4096 字节。B 自检失败 trial 加一，三次后回 A。
