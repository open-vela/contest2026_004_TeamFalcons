# MQTT Broker and AI Bridge instead of board direct MiMo

VelaGuard talks to an MQTT Broker, and a separate AI Bridge service calls MiMo, TTS, ASR, and manual parsing over HTTPS. This adds a small cloud component, but keeps heavyweight HTTPS/API handling, retries, credentials, and large AI workflows off the H750B-DK while preserving an independent gateway shape.
