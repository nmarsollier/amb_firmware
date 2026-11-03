#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * SHT31-D temperature/humidity sensor on the I2C bus (SDA = GPIO 8, SCL = GPIO 9).
 *
 * One sensor, ADDR tied to GND (address 0x44). Provides ambient temperature and
 * relative humidity; the environment module turns them into the reported
 * figures and computes the dew point.
 */

/*
 * Attach the sensor to the shared I2C bus, probe it and prime it if present.
 * Returns ESP_OK when the sensor is present, or an error otherwise.
 */
esp_err_t sht31_init(void);

/* True once the sensor answered the boot probe. */
bool sht31_is_present(void);

/*
 * Run one single-shot measurement and fill `temp_c` / `humidity_pct`.
 * Returns ESP_OK on success, or an error on an I2C or CRC failure.
 */
esp_err_t sht31_read(float *temp_c, float *humidity_pct);
