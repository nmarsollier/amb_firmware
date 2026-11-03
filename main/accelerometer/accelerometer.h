#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * ADXL345 accelerometer on the I2C bus (SDA = GPIO 8, SCL = GPIO 9).
 *
 * One sensor, with SDO tied to GND (address 0x53). Provides acceleration in g;
 * the tilt module turns it into tilt angles.
 */

/* One physical reading, already converted to g. */
typedef struct {
    float x_g;
    float y_g;
    float z_g;
} AccelSample;

/*
 * Bring up the I2C bus, probe the sensor and configure it if present.
 * Returns ESP_OK when the sensor is present, or an error otherwise.
 */
esp_err_t accelerometer_init(void);

/* True once the sensor answered the boot probe. */
bool accelerometer_is_present(void);

/*
 * Read one sample (x/y/z in g). Returns ESP_OK and fills `out`, or an error
 * if the sensor is absent or the I2C transaction fails.
 */
esp_err_t accelerometer_read_sample(AccelSample *out);
