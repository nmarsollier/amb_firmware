/* i2c_bus — i2c_bus.c — I2C master bus factory.
 *
 * Creates an I2C master bus for a sensor. The SHT31 and the ADXL345 are wired
 * to different pins, so each driver creates its own bus during init.
 */
#include "i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "I2C_BUS";

#define I2C_CLK_HZ  100000    /* 100 kHz standard mode */

esp_err_t i2c_bus_create(i2c_port_t port, gpio_num_t sda, gpio_num_t scl,
                         i2c_master_bus_handle_t *out_bus) {
    if (out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port               = port,
        .sda_io_num             = sda,
        .scl_io_num             = scl,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, out_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed (port %d): %s", (int) port, esp_err_to_name(err));
        *out_bus = NULL;
        return err;
    }

    ESP_LOGI(TAG, "I2C master bus ready (port %d, SDA %d, SCL %d, %d kHz)",
             (int) port, (int) sda, (int) scl, I2C_CLK_HZ / 1000);
    return ESP_OK;
}
