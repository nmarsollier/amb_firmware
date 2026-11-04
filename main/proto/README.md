# Temperature/vibration protocol (USB CDC-ACM)

Serial contract between the PC (Go gateway) and the temperature/vibration module's
ESP32-S3. It uses the unified transport and identifies itself through the
`capabilities` handshake (`device_type="accessories"`, `device_id="amb"`, `proto=2`).

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
- `target`: `device` | `environment` | `vibration` | `orientation`.
- `action` (for `type=action`) or `op` (for control/config) names the operation.
- `params` is an object; `id` is a non-zero uint32 (0 is reserved for events, not yet
  implemented); `session`/`boot_id` are strings.

`capabilities` is the exception: it uses the unified identification header below
instead of the session/data envelope.

## Unified capabilities

```json
{"type":"capabilities","id":1,"ok":true,
 "boot_id":"0e86a327","proto":2,
 "device_type":"accessories","device_id":"amb",
 "name":"AMB","firmware_version":"0.1.0",
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
| `state / vibration` | Accel RMS/peak (milli-g) and velocity RMS/peak (µm/s) since the previous read, plus ISO 10816 zone (100 Hz ADXL345). |
| `state / orientation` | Pitch/roll (degrees) and the gravity vector (g) from the ADXL345. |
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

`state/vibration` is read-and-reset: each response reports the figures accumulated
since the previous `state/vibration` request (`processing:"since_last_read"`), then
resets the accumulators. The `*_peak_*` fields are latching maxima over that
period, so they reveal whether a large vibration happened between two reads.

Response (`state / vibration`):

```json
{"proto":2,"type":"state","id":42,"session":"g-7f2a","boot_id":"0e86a327","ok":true,
 "data":{"enabled":true,"valid":true,"error":null,
 "sample_rate_hz":100,
 "rms_x":2.13,"rms_y":1.87,"rms_z":3.01,"rms_total":4.13,
 "accel_peak_x":8.4,"accel_peak_y":6.2,"accel_peak_z":12.7,"accel_peak_total":16.3,
 "vel_rms_x":340.2,"vel_rms_y":291.4,"vel_rms_z":512.7,"vel_rms_total":680.9,
 "vel_peak_x":1240.5,"vel_peak_y":980.1,"vel_peak_z":2100.3,"vel_peak_total":2600.8,
 "iso_zone":"A","processing":"since_last_read"}}
```

Units: `rms_*` / `accel_peak_*` are in **milli-g**; `vel_rms_*` / `vel_peak_*` are
in **µm/s**. `iso_zone` is `A`/`B`/`C`/`D` (ISO 10816-1, Class II velocity-RMS
boundaries).

`state/orientation` reports the current rotation angles and the gravity vector:

```json
{"proto":2,"type":"state","id":42,"session":"g-7f2a","boot_id":"0e86a327","ok":true,
 "data":{"enabled":true,"error":null,
 "pitch_deg":-20.70,"roll_deg":41.41,
 "ax_g":-0.353,"ay_g":0.661,"az_g":0.662}}
```

`pitch_deg`/`roll_deg` are the X/Y-axis tilt from horizontal; `ax_g`/`ay_g`/`az_g`
are the gravity vector in g (magnitude ~1).

## Pending (out of this iteration)

Events/subscription and push; real lease/heartbeat; `config/set` with persistence;
`environment/refresh`, `vibration/enable|baseline|capture`.
