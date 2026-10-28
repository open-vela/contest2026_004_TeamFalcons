# AI generated configuration requires device-side confirmation

AI可生成候选配置，但只有板端完成确定性校验并经人工本地确认后才能生效。板载Agent不执行配置写入，也不能替人确认。

2026-10-18用户更正：新增手机MQTT接入不试读，校验后在板端二次确认，直接添加；替代本ADR原先对该入口要求试读的部分，见 [ADR 0007](0007-mqtt-candidate-import-with-local-confirmation.md)。现行NSH仍按原协议先试读、展示结果并等人确认，再单独apply。添加成功与采集成功分别记录，不能在添加前虚构读数。
