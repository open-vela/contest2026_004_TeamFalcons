# Independent gateway with local safety loop

VelaGuard is an independent STM32H750B-DK gateway, not a PC-tethered sidecar demo. The local safety loop of Modbus acquisition, rule evaluation, UI alarm display, local logging, and local alarm sound must continue without network, MQTT, AI Bridge, MiMo, TTS, ASR, or a connected computer because the project's industrial value depends on the board remaining useful at the site when cloud capabilities fail.
