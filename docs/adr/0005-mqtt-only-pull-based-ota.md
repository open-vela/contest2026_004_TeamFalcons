# MQTT-only pull based OTA

VelaGuard uses MQTT/TLS for OTA control and firmware chunk transfer instead of adding a board-side HTTPS downloader. HTTPS would be a common production choice for large firmware delivery, but this project values a single authenticated MQTT path, simpler embedded networking, Broker ACL reuse, and tighter integration with local confirmation, progress reporting, and rollback.
