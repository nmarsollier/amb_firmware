#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * I2C master bus factory.
 *
 * Creates an I2C master bus on the given port and pins. The SHT31 and the
 * ADXL345 are wired to different pins, so each driver creates its own bus
 * during init with the pins from config.
 */

esp_err_t i2c_bus_create(i2c_port_t port, gpio_num_t sda, gpio_num_t scl,
                         i2c_master_bus_handle_t *out_bus);
