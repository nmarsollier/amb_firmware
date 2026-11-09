/* Accelerometer — accelerometer_init.c
 *
 * Purpose: bring up the ADXL345 sensor on its own I2C bus.
 *
 * Creates a bus for the ADXL345 pins, attaches a device handle for address
 * 0x53, probes it and configures it if it answers. A missing sensor is not
 * fatal: the vibration module simply reports itself unavailable.
 */
#include "accelerometer_internal.h"

#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_bus.h"

static const char *TAG = "ACCELEROMETER_INIT";

AccelSensor accel_sensor;

bool accelerometer_is_present(void) {
    return accel_sensor.present;
}

esp_err_t accelerometer_read_register(const AccelSensor *sensor, uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(sensor->dev_handle, &reg, 1, value, 1, ACCEL_TIMEOUT_MS);
}

esp_err_t accelerometer_write_register(const AccelSensor *sensor, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(sensor->dev_handle, buf, sizeof(buf), ACCEL_TIMEOUT_MS);
}

/* Verify the device identity and switch it to measurement mode. */
static esp_err_t configure_sensor(AccelSensor *sensor) {
    uint8_t devid = 0;
    if (accelerometer_read_register(sensor, ADXL345_REG_DEVID, &devid) != ESP_OK ||
        devid != ADXL345_DEVID_EXPECTED) {
        ESP_LOGW(TAG, "addr 0x%02X: unexpected DEVID 0x%02X (expected 0x%02X)",
                 sensor->address, devid, ADXL345_DEVID_EXPECTED);
        return ESP_ERR_NOT_FOUND;
    }

    /* ±2 g, 10-bit, 100 Hz, FIFO stream mode. */
    static const struct { uint8_t reg; uint8_t val; } cfg[] = {
        { ADXL345_REG_POWER_CTL, 0x00 }, /* standby before reconfiguration */
        { ADXL345_REG_FIFO_CTL, 0x00 },  /* bypass discards stale FIFO samples */
        { ADXL345_REG_DATA_FORMAT, 0x00 },
        { ADXL345_REG_BW_RATE, ADXL345_BW_RATE_100HZ },
        { ADXL345_REG_FIFO_CTL, ADXL345_FIFO_MODE_STREAM },
        { ADXL345_REG_POWER_CTL, ADXL345_POWER_CTL_MEASURE },
    };
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        esp_err_t err = accelerometer_write_register(sensor, cfg[i].reg, cfg[i].val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "addr 0x%02X: write reg 0x%02X failed: %s",
                     sensor->address, cfg[i].reg, esp_err_to_name(err));
            return err;
        }
    }

    /* Verify all final settings, including FIFO and output data rate. */
    for (size_t i = 2; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        uint8_t value = 0;
        esp_err_t err = accelerometer_read_register(sensor, cfg[i].reg, &value);
        if (err != ESP_OK) {
            return err;
        }
        if (value != cfg[i].val) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    ESP_LOGI(TAG, "ADXL345 at 0x%02X ready", sensor->address);
    return ESP_OK;
}

esp_err_t accelerometer_init(void) {
    accel_sensor.address = ADXL345_ADDR;
    accel_sensor.present = false;

    esp_err_t err;
    if (accel_sensor.bus_handle == NULL) {
        err = i2c_bus_create(ADXL345_I2C_PORT, ADXL345_I2C_SDA_GPIO,
                             ADXL345_I2C_SCL_GPIO, &accel_sensor.bus_handle);
        if (err != ESP_OK) {
            return err;
        }
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = accel_sensor.address,
        .scl_speed_hz    = ACCEL_CLK_HZ,
    };

    if (accel_sensor.dev_handle == NULL) {
        err = i2c_master_bus_add_device(accel_sensor.bus_handle, &dev_config,
                                       &accel_sensor.dev_handle);
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Probe the sensor with retries — I2C can fail intermittently. */
    err = ESP_ERR_NOT_FOUND;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        err = i2c_master_probe(accel_sensor.bus_handle, accel_sensor.address, ACCEL_TIMEOUT_MS);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "addr 0x%02X: probe attempt %d/3 failed (%s)",
                 accel_sensor.address, attempt + 1, esp_err_to_name(err));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "addr 0x%02X: not present after 3 attempts (%s) — sensor disabled",
                 accel_sensor.address, esp_err_to_name(err));
        return err;
    }

    err = configure_sensor(&accel_sensor);
    accel_sensor.present = (err == ESP_OK);
    return err;
}

esp_err_t accelerometer_recover(void) {
    if (accel_sensor.bus_handle == NULL || accel_sensor.dev_handle == NULL) {
        return accelerometer_init();
    }
    accel_sensor.present = false;
    esp_err_t err = i2c_master_bus_reset(accel_sensor.bus_handle);
    if (err == ESP_OK) {
        err = configure_sensor(&accel_sensor);
    }
    accel_sensor.present = (err == ESP_OK);
    return err;
}
