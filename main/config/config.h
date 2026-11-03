#pragma once

#include "driver/gpio.h"

/*
 * TEMP hardware configuration.
 */

/* Temperature/vibration module identifier — reported in the serial protocol
 * `capabilities`. */
#define DEVICE_NAME "TEMP"

/* =========================================================================
 * Pin assignments
 * ========================================================================= */

/* ADXL345 I2C address (SDO tied to GND). */
#define ADXL345_ADDR    0x53

/* SHT31-D I2C address (ADDR pin tied to GND). Use 0x45 when ADDR is tied high. */
#define SHT31_ADDR      0x44

/* I2C bus — shared by the ADXL345 (vibration) and the SHT31 (environment). */
#define I2C_SDA_GPIO    GPIO_NUM_8
#define I2C_SCL_GPIO    GPIO_NUM_9

/* Status LED — direct GPIO, LEDC PWM. */
#define LED_GPIO        GPIO_NUM_12

/* Onboard RGB LED (WS2812) — held low at boot so the board's own blue/status
 * light stays off. GPIO 48 on the common Super Mini (clone variants may use
 * GPIO 21). Not the module's status LED (LED_GPIO). */
#define ONBOARD_LED_GPIO    GPIO_NUM_48
