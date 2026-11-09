#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * ADXL345 accelerometer on its own I2C bus (SDA = GPIO 12, SCL = GPIO 13).
 *
 * One sensor, with SDO tied to GND (address 0x53). Provides acceleration in g;
 * the vibration module turns it into movement figures.
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

/* Reset the dedicated bus and restore sensor configuration. Call only from
 * the sampling task, with no concurrent reads. Also retries an absent sensor. */
esp_err_t accelerometer_recover(void);

/* True once the sensor answered the boot probe. */
bool accelerometer_is_present(void);

/*
 * Read one sample (x/y/z in g). Returns ESP_OK and fills `out`, or an error
 * if the sensor is absent or the I2C transaction fails.
 */
esp_err_t accelerometer_read_sample(AccelSample *out);

/*
 * Read `n_samples` samples from the FIFO and convert them to g.
 * Returns ESP_OK on success.
 */
esp_err_t accelerometer_read_fifo(AccelSample *samples, size_t n_samples);

/* Number of samples currently available in the FIFO (0-32). */
esp_err_t accelerometer_fifo_entries(uint8_t *out);
