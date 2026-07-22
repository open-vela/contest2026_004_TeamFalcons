# Device ID is stable and not runtime editable in production

Production VelaGuard firmware derives `device_id` from the STM32 UID and does not expose any runtime interface to change it. Test firmware may override `device_id` through code or compile-time configuration, but production keeps identity stable so MQTT topics, ACLs, authentication, logs, and cloud routing cannot be changed accidentally from UI, serial, HTTP, or MQTT.
