# amb_firmware — Ambient temperature, humidity and vibration

Measures the ambient temperature/humidity and the telescope vibration. This board
hosts only the two sensors (SHT31 + ADXL345), each on its own I2C bus; the focuser
and the fan live in their own boards (`eaf_firmware` and `fan_firmware`).

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
| `i2c_bus` | I2C master bus factory (one bus per sensor). | — |
| `sht31` | SHT31-D over I2C (driver: init, probe, read). | SDA `SHT31_I2C_SDA_GPIO`, SCL `SHT31_I2C_SCL_GPIO`, addr `SHT31_ADDR` |
| `accelerometer` | ADXL345 over I2C (driver: init, probe, read). | SDA `ADXL345_I2C_SDA_GPIO`, SCL `ADXL345_I2C_SCL_GPIO`, addr `ADXL345_ADDR` |
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
- SDA/SCL on their own bus (GPIO 8 / 9), separate from the ADXL345.
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

`make flash` builds first, sends `action/device/bootloader` over one open USB
CDC connection, and waits for an ESP32-S3 ROM handshake before flashing. The
port may change names or keep the same name. A missing command response is only
accepted if the ROM subsequently responds (including a board already in download
mode). An explicit protocol rejection aborts the operation.

Connect one ESP board and close serial monitors/gateways before flashing. Port
selection requires a single `/dev/cu.usbmodem*`, or an explicit
`make flash PORT=/dev/cu.usbmodem...`. USB location/serial-string matching is not
required. The workflow uses the pyserial and esptool installed with ESP-IDF.

`make enter-flash` requests and verifies download mode without building or writing
flash. `make flash-download` builds and flashes a board already in download mode,
without sending an application command. Both support `PORT=...` and use the same
ROM verification. If software entry fails, use BOOT/RESET to enter download mode
and run `make flash-download`. The wire format is documented in
[`main/proto/README.md`](main/proto/README.md).

The firmware releases TinyUSB and assigns the internal USB PHY to Serial/JTAG
before rebooting into ROM. The first update from older firmware may require
BOOT/RESET followed by `make flash-download` to install this transition.

Run flash-script regression tests in the ESP-IDF environment with
`python3 -B scripts/test_flash.py`. Run sensor/USB runtime regression tests on the
host with `python3 scripts/test_runtime.py` (requires a C11 compiler with
UndefinedBehaviorSanitizer; optionally set `SANITIZERS=address,undefined`).

The vibration task distinguishes I2C errors from a temporarily empty FIFO. It
marks data stale after 250 ms without samples and retries sensor/bus setup at
most once per second, including after a failed boot probe. Filters and interval
statistics restart after sample loss. USB responses are queued whole; host
backpressure has a bounded timeout and does not disable subsequent responses.

## Alpaca

The Go interface (`mount_panel`) exposes the ambient temperature, humidity and dew
point through the Alpaca ObservingConditions V2 interface.
