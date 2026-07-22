# VelaGuard Domain

VelaGuard is an independent industrial edge AI gateway context. This glossary fixes the project language used in manuals, plans, firmware modules, cloud services, and UI copy.

## Language

**VelaGuard**:
The STM32H750B-DK based industrial edge gateway that owns local sensor acquisition, local alarm decisions, local UI, and controlled cloud AI interaction.
_Avoid_: AI screen, chat terminal, PC sidecar demo

**Local Safety Loop**:
The offline-capable loop of Modbus acquisition, local rule evaluation, UI alarm display, local logging, and local alarm sound.
_Avoid_: demo loop, fallback-only path

**AI Bridge**:
The cloud service that subscribes to VelaGuard MQTT requests, calls MiMo or related cloud AI services, and publishes structured MQTT responses.
_Avoid_: sidecar, MiMo API, broker

**MQTT Broker**:
The message broker that connects VelaGuard and AI Bridge; it routes topics and enforces client access rules.
_Avoid_: AI Bridge, cloud API

**Device ID**:
The stable identity used in MQTT topics, authentication, logs, and cloud routing for one physical VelaGuard device.
_Avoid_: display name, sensor id

**Display Name**:
A user-facing label for a device or sensor that may change without changing identity or access rights.
_Avoid_: device id

**Sensor Configuration**:
The executable definition of how VelaGuard reads a sensor and evaluates its local alarm rules.
_Avoid_: prompt result, manual notes

**Candidate Configuration**:
A proposed sensor configuration that has not yet passed device-side validation, test read, and local confirmation.
_Avoid_: active config, AI config

**Manual Profile**:
A structured summary of a sensor manual that VelaGuard can use to generate or validate sensor configurations.
_Avoid_: PDF, raw manual

**Local Confirmation**:
An explicit operator approval on the VelaGuard device before a configuration or risky action becomes active.
_Avoid_: remote approval, AI approval

**Active Alarm**:
An alarm instance that has triggered and has not yet met its restore condition.
_Avoid_: acknowledged alarm, historical event

**Acknowledgement**:
The operator action that records awareness of an active alarm; it does not mean the condition has recovered.
_Avoid_: resolution, close

**Resolution**:
The state transition when an alarm's restore condition has been satisfied.
_Avoid_: acknowledgement, silence

**Offline**:
A Modbus device state where normal reads have failed and VelaGuard switches to low-frequency probing while preserving prior alarm state.
_Avoid_: disabled, removed

**Recovering**:
A Modbus device state where full reads have resumed but VelaGuard waits for consecutive successes before returning to online.
_Avoid_: online, degraded

**Pending Event**:
A locally stored event that still needs reliable cloud delivery after network or MQTT recovery.
_Avoid_: MQTT session, retained message

**Retained Status**:
The latest retained MQTT status message that represents current device state for late subscribers.
_Avoid_: retained request, retained telemetry

**MQTT-only OTA**:
A firmware update flow where VelaGuard receives OTA control messages and pulls firmware chunks through the existing MQTT/TLS connection.
_Avoid_: HTTPS OTA, direct firmware push

**OTA Offer**:
A cloud-published proposal that describes an available firmware update before VelaGuard accepts or downloads any chunks.
_Avoid_: firmware image, forced update

**Staging Image**:
A downloaded firmware image stored outside the currently running firmware slot until signature verification and boot switching.
_Avoid_: active firmware
