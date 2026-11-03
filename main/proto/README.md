# Temperature/vibration protocol (USB CDC-ACM)

Serial contract between the PC (Go gateway) and the temperature/vibration module's
ESP32-S3. It uses the unified transport and identifies itself through the
`capabilities` handshake (`device_type="accessories"`, `device_id="temp"`, `proto=2`).

## Transport and framing

- USB CDC-ACM (TinyUSB), a single serial port.
- Frame: `[u32 little-endian length][JSON UTF-8]`, up to 2048 bytes per body, no
  application CRC (USB is reliable).
- The receiver accepts partial reads and several frames together; on error it drops a
  byte and resynchronizes.

## Message envelope

Request:

```json
{"proto":2,"type":"state","id":42,"session":"g-7f2a","boot_id":"a1b2c3d4",
 "target":"environment"}
```

Response (`ok:true` + `data`, or `ok:false` + `error`):

```json
{"proto":2,"type":"state","id":42,"session":"g-7f2a","boot_id":"a1b2c3d4",
 "ok":true,"data":{"error":null,"ambient_c":18.42,"humidity_pct":55.10,
 "mirror_c":null,"dew_point_c":9.31,
 "mirror_ambient_delta_c":null,"mirror_dew_margin_c":null}}
```

- `type`: `capabilities` | `state` | `config` | `control` | `action`.
- `target`: `device` | `environment` | `vibration`.
- `action` (for `type=action`) or `op` (for control/config) names the operation.
- `params` is an object; `id` is a non-zero uint32 (0 is reserved for events, not yet
  implemented); `session`/`boot_id` are strings.

`capabilities` is the exception: it uses the unified identification header below
instead of the session/data envelope.

## Unified capabilities

```json
{"type":"capabilities","id":1,"ok":true,
 "boot_id":"0e86a327","proto":2,
 "device_type":"accessories","device_id":"temp",
 "name":"TEMP","firmware_version":"0.1.0",
 "capabilities":{
   "sensors":{"ambient":"SHT31","vibration":"ADXL345"}}}
```

- `device_type`: `mount` | `accessories` — the discriminator.
- `device_id`, `name`, `firmware_version`, `proto`, `boot_id`: shared identity.

## Implemented catalog

| Operation | Status |
|---|---|
| `capabilities` | Unified handshake header + sensor capabilities (real). |
| `state / device` | `health`, `sensor_health`, `reset_reason` (real). |
| `state / environment` | Ambient only: `ambient_c`, `humidity_pct`, `dew_point_c` (real); mirror/delta/margin are always `null`. |
| `state / vibration` | Per-axis RMS from the ADXL345 (real). |
| `config / <group> / get` | Read-only mock (empty config); `set` → `NOT_SUPPORTED`. |
| `action / device / restart` | Deferred normal reboot (real). |
| `action / device / bootloader` | Reboot into ROM download mode (real, for button-less flashing). |
| `control / device / session_open`, `heartbeat` | Minimal, no real lease enforcement. |

## Reboot into bootloader (button-less flashing)

`action / device / bootloader` reboots the module into the ROM download mode so
the host can reflash it over USB-Serial-JTAG without pressing the BOOT button.
The module answers `accepted`, then reboots ~200 ms later.

Request (framed: `[u32 little-endian length][JSON]`):

```json
{"proto":2,"type":"action","id":1,"session":"s","target":"device","action":"bootloader"}
```

Response:

```json
{"proto":2,"type":"action","id":1,"session":"s","boot_id":"0e86a327","ok":true,
 "data":{"op_id":"","status":"accepted"}}
```

After the reset the CDC port disappears and the USB-Serial-JTAG port enumerates;
flash that new port. The download-mode flag is one-shot: it forces only the next
boot.

## Error codes (strings)

`INVALID_ARGUMENT`, `INTERNAL`, `NOT_SUPPORTED`.

## State error model

Every `state/<target>` response carries `ok:true` and a `data.error` field that
reports the subsystem's own condition:

- `null` — healthy.
- `"SENSOR_ABSENT"` — the sensor was not detected at boot (e.g. SHT31); the value
  fields are `null`.

A `state` request **always answers**: a sensor fault is returned as an error state,
never as a hang or a fabricated reading (no 0 °C / 0 m/s² placeholders).

## Pending (out of this iteration)

Events/subscription and push; real lease/heartbeat; `config/set` with persistence;
`environment/refresh`, `vibration/enable|baseline|capture`.
