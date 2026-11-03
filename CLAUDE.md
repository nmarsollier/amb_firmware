# temp_firmware Development Rules

Keep this file in a simple format so it can be read and edited quickly.

## Reference projects

- **`~/Dev/bft_firmware`** — ESP32-S3 fork equatorial mount firmware (the mount
  this sensor module pairs with). Reference for the USB CDC-ACM serial protocol.
- **`~/Dev/mount_panel`** — Go gateway (Alpaca/UI). Exposes the Alpaca
  ObservingConditions for this project.

## Project definition

- Ambient temperature/humidity and tilt module for a Newtonian telescope, on
  ESP32-S3.
- Functions: ambient temperature/humidity (SHT31, I2C), tilt (ADXL345, I2C),
  dew point (computed).
- No 12 V sensor and no buttons — a sensor module only.
- Status LED (onboard RGB/WS2812): green when healthy, green beacon on sensor fault.
- USB CDC-ACM communication with JSON messages.
- Hardware detail in `README.md`.

## Architecture

Layers, outside-in:

```
Desktop app (Go) (Alpaca/UI) → USB CDC-ACM → Proto → Sensors → I2C (hardware)
```

- **config** (`main/config/`) — single source of truth for pins and build constants.
- **i2c_bus** (`main/i2c_bus/`) — shared I2C master bus (ADXL345 + SHT31).
- **sht31** (`main/sht31/`) — SHT31-D over I2C (ambient temp + humidity).
- **accelerometer** (`main/accelerometer/`) — ADXL345 over I2C.
- **tilt** (`main/tilt/`) — telescope inclination (pitch/roll) from the ADXL345.
- **environment** (`main/environment/`) — ambient/humidity (SHT31) + dew point.
- **led** (`main/led/`) — status LED (onboard RGB/WS2812, RMT): green (solid/beacon) from sensor health.
- **usb_cdc** (`main/usb_cdc/`) — USB CDC-ACM transport.
- **proto** (`main/proto/`) — `proto=2` protocol (framing, dispatch, handlers).
- **runtime** (`main/runtime/`) — init and main loop.

## Current state

Implemented: `i2c_bus` (shared I2C master bus), `sht31` (SHT31-D), `accelerometer`
(ADXL345) + `tilt` (telescope inclination), `environment` (ambient
temp/humidity + dew point), `led` (status LED: green), `usb_cdc` (transport)
and `proto` (`proto=2` protocol, `device_id="temp"`, with
capabilities/state/action/config/control handlers). Board: ESP32-S3 Super Mini
(4 MB flash, I2C GPIO8/9, onboard RGB status LED); `action/device/bootloader` enables
button-less flashing.

Pending: events/extended session, `config/set` with persistence, and the Go gateway.
Contract in `main/proto/README.md`.

## General rules

- Never commit or push to GitHub without explicit authorization.
- Commit only when the whole feature is verified and compiling.
- Always base on code on disk as the single source of truth. Source code is the
  only valid reference for implementation details.
- Before any change, read the current state of the file to modify.
- Project written in C, following a functional style when possible.
- Modules separated by domain, each domain in a folder under `main/`.
- Each domain exposes:
    - `domain.h` — public API (functions other modules may call)
    - `domain_internal.h` — internal API (functions shared only among the module's files)
- One `.c` file per use case within each module.
- Public functions start with the module name followed by `_`.
- A module's state variables are never accessed directly from outside, only
  through the module's public functions.
- Respect: DRY - YAGNI - KISS.
- Header documentation describes the business problem or use case the function
  solves, not implementation details.
- Only comment code when it is hard for a human to understand.
- Code, comments and documentation (`.c`, `.h` and `.md`) are written in English.

## Code conventions

- **Language**: C (not C++)
- **snake_case** for functions and variables
- **UPPER_CASE** for macros and defines
- **Headers**: `#pragma once`, includes organized: the module's own first, then framework libraries
- **Logging**: `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()` with a static tag per file
- **Results between layers**: use per-module `Result` types with the `{ ok, message }` shape
