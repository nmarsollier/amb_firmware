#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * Shared I2C bus — owns the single master bus (SDA = GPIO 9, SCL = GPIO 8)
 * that the ADXL345 (vibration) and the SHT31 (environment) attach to.
 *
 * i2c_bus_init() is idempotent: the bus is created on the first call, and each
 * driver calls it before adding its own device handle.
 */

esp_err_t i2c_bus_init(void);

/* Handle to the shared bus; NULL if i2c_bus_init() failed. */
i2c_master_bus_handle_t i2c_bus_get(void);
