#pragma once

#include "driver/gpio.h"

/*
 * AMB hardware configuration.
 */

/* Temperature/vibration module identifier — reported in the serial protocol
 * `capabilities`. */
#define DEVICE_NAME "AMB"

/* =========================================================================
 * Pin assignments
 * ========================================================================= */

/* ADXL345 I2C address (SDO tied to GND). */
#define ADXL345_ADDR    0x53

/* SHT31-D I2C address (ADDR pin tied to GND). Use 0x45 when ADDR is tied high. */
#define SHT31_ADDR      0x44

/* I2C bus — shared by the ADXL345 and the SHT31. */
#define I2C_SDA_GPIO    GPIO_NUM_8
#define I2C_SCL_GPIO    GPIO_NUM_9

/* Status LED — onboard RGB (WS2812), driven via RMT. GPIO 48 on the common
 * Super Mini (clone variants may use GPIO 21). */
#define LED_GPIO        GPIO_NUM_48

/* =========================================================================
 * Vibration (ADXL345) — high-rate sampling + ISO 10816
 * ========================================================================= */

/* ADXL345 output data rate (100 Hz). */
#define VIB_SAMPLE_RATE_HZ      100

/* High-pass cutoff (Hz) applied to acceleration before integrating to velocity. */
#define VIB_HP_CUTOFF_HZ        5.0f

/* ISO 10816-1 zone boundaries for a Class II machine, velocity RMS (mm/s). */
#define ISO10816_CLASS_AB       1.12f
#define ISO10816_CLASS_BC       2.8f
#define ISO10816_CLASS_CD       7.1f
