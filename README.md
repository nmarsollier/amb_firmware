# amb_firmware — Ambient temperature, humidity and vibration

Measures the ambient temperature/humidity and the telescope vibration. This board
hosts only the two sensors (SHT31 + ADXL345) on the shared I2C bus; the focuser and
the fan live in their own boards (`eaf_firmware` and `fan_firmware`).

## Functionality

- Ambient temperature and relative humidity (SHT31-D over I2C).
- Dew point (computed from ambient temperature and humidity).
- Telescope vibration (ADXL345 over I2C, 100 Hz): per-axis acceleration RMS/peak
  (milli-g) and velocity RMS/peak (µm/s), reported since the previous read (the
  peaks latch whether a vibration happened between reads), plus an ISO 10816-1 zone.
- Status LED (onboard RGB): green (solid) when healthy, green beacon when either
  sensor cannot be read.

No 12 V sensor and no buttons — it is a sensor module only.

## Implementation status (November 2026)

| Module | Role | Pins |
|---|---|---|
| `i2c_bus` | Shared I2C master bus (ADXL345 + SHT31). | SDA `I2C_SDA_GPIO`, SCL `I2C_SCL_GPIO` |
| `sht31` | SHT31-D over I2C (driver: init, probe, read). | SDA `I2C_SDA_GPIO`, SCL `I2C_SCL_GPIO`, addr `SHT31_ADDR` |
| `accelerometer` | ADXL345 over I2C (driver: init, probe, read). | SDA `I2C_SDA_GPIO`, SCL `I2C_SCL_GPIO`, addr `ADXL345_ADDR` |
| `vibration` | High-rate (100 Hz) ADXL345: accel RMS/peak (milli-g) and velocity RMS/peak (µm/s), reset on each read. | — |
| `environment` | Ambient temp/humidity from the SHT31 + dew point. | — |
| `led` | Status LED (onboard RGB): green (solid) when healthy, green beacon on sensor fault. | `LED_GPIO` (RMT) |
| `usb_cdc` | USB CDC-ACM transport (TinyUSB). | — |
| `proto` | `proto=2`, `device_id="amb"`; handlers `capabilities`/`state`/`action`/`config`/`control`. | — |

The protocol contract is documented in [`main/proto/README.md`](main/proto/README.md).

## Hardware

| Component | Part |
|---|---|
| Microcontroller | ESP32-S3 Mini (4 MB flash) <https://www.amazon.com/dp/B0GY3M83NL> |
| Ambient temp/humidity | SHT31-D <https://www.amazon.com/dp/B0GZZ9W7CK> |
| Accelerometer | ADXL345 |

## Ambient temperature and humidity — SHT31

- I2C bus, usual address 0x44 (`SHT31_ADDR`).
- SDA/SCL are shared with the ADXL345.
- Must be placed in a ventilated area, away from the ESP32 and regulator heat.
- Provides ambient temperature and humidity (for the dew point, reported to the
  gateway).

## ADXL345 accelerometer

- I2C address `ADXL345_ADDR`.
- Measures telescope vibrations.
- Sampled at 100 Hz (FIFO stream mode over 100 kHz I2C); reported as acceleration
  RMS/peak (milli-g) and velocity RMS/peak (µm/s) since the previous read, plus an
  ISO 10816-1 zone.
- Encapsulated with the temperature/humidity sensor, mounted on the telescope tube.
- Initially statistics only, reported to the gateway. Could later analyze mount
  problems and tracking quality.

## Communication

USB-CDC with JSON messages. Provides the ambient temperature/humidity/dew point and
the vibration figures. The Go gateway (`mount_panel`) implements the Alpaca
ObservingConditions V2 interface (and the ambient-temperature switch channel) on top
of this link.

### Button-less flashing

`make flash` sends the `action/device/bootloader` command over the USB CDC port,
waits for the module to re-enumerate as USB-Serial-JTAG, and reflashes it — no
BOOT button needed. `make enter-flash` does only the first step. The wire format
is documented in [`main/proto/README.md`](main/proto/README.md).

## Alpaca

The Go interface (`mount_panel`) exposes the ambient temperature, humidity and dew
point through the Alpaca ObservingConditions V2 interface.
