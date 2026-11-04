# BOARD — ESP32-S3 Super Mini

Reference board for this firmware.

- Listing: <https://www.amazon.com/dp/B0GY3M83NL> (10-pack)
- Board label: ESP32-S3 Super Mini
- SoC: ESP32-S3 (Xtensa LX7)

## SoC — ESP32-S3

| Property | Value |
|---|---|
| CPU | Xtensa LX7, dual-core, up to 240 MHz |
| SRAM | 512 KB (listing says 400 KB) |
| ROM | 384 KB |
| Flash | 4 MB, onboard (FH4, no PSRAM) |
| Wi-Fi | 802.11 b/g/n, 2.4 GHz (Station + SoftAP) |
| Bluetooth | Bluetooth 5 (LE) |
| Security | AES-128/256, RSA, SHA, secure boot, flash encryption |
| USB | Native USB OTG (Full-Speed) + USB Serial/JTAG, shared pins GPIO19 (D-) / GPIO20 (D+) |

## Interfaces

I2C, SPI, UART, I2S, PWM and ADC — available on the broken-out GPIOs.

## Pinout (front, USB connector up)

| Left side | Function | Right side | Function |
|---|---|---|---|
| TX | UART0 TX | 5V | 5 V power |
| RX | UART0 RX | GND | Ground |
| GP1 | SPI, I2C, ADC, I2S, PWM, UART | 3V3 (OUT) | 3.3 V output |
| GP2 | SPI, I2C, ADC, I2S, PWM, UART | GP13 | SPI, I2C, ADC, I2S, PWM, UART |
| GP3 | SPI, I2C, ADC, I2S, PWM, UART | GP12 | SPI, I2C, ADC, I2S, PWM, UART |
| GP4 | SPI, I2C, ADC, I2S, PWM, UART | GP11 | SPI, I2C, ADC, I2S, PWM, UART |
| GP5 | SPI, I2C, ADC, I2S, PWM, UART | GP10 | SPI, I2C, ADC, I2S, PWM, UART |
| GP6 | SPI, I2C, ADC, I2S, PWM, UART | GP9 | SPI, I2C, ADC, I2S, PWM, UART |
| GP7 | SPI, I2C, ADC, I2S, PWM, UART | GP8 | SPI, I2C, ADC, I2S, PWM, UART |

Broken-out GPIOs: GP1–GP13 (plus UART0 TX/RX). The native USB D+/D- pins
(GPIO19/20) are not broken out; they are wired to the USB-C connector.

## Notes / discrepancies vs. datasheet

- The Amazon bullet says "single-core @ 160 MHz"; the ESP32-S3 is dual-core @ up to
  240 MHz. 160 MHz is the ESP-IDF default CPU frequency, not a limit.
- The bullet says 400 KB SRAM; the ESP32-S3 has 512 KB SRAM.
- Flash is 4 MB (FH4), no PSRAM.

## Firmware relevance

- I2C: SDA = GPIO8, SCL = GPIO9 — both broken out, no conflict with flash/PSRAM.
- Status LED: onboard RGB (WS2812) on GPIO48, driven via RMT (green = healthy,
  green beacon = fault).
- ADXL345: 100 Hz FIFO stream mode over the shared 100 kHz I2C bus (vibration).
- USB CDC-ACM: native USB on the USB-C connector (GPIO19/20), driven by TinyUSB.
- Flash size: 4 MB → `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"`.
