/* Accelerometer — accelerometer_init.c
 *
 * Purpose: bring up the ADXL345 sensor on the shared I2C bus.
 *
 * Attaches a device handle for address 0x53 to the shared bus (i2c_bus),
 * probes it and configures it if it answers. A missing sensor is not fatal:
 * the vibration module simply reports itself unavailable.
 */
#include "accelerometer_internal.h"

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
    accelerometer_write_register(sensor, ADXL345_REG_DATA_FORMAT, 0x00);
    accelerometer_write_register(sensor, ADXL345_REG_BW_RATE, ADXL345_BW_RATE_100HZ);
    accelerometer_write_register(sensor, ADXL345_REG_FIFO_CTL, ADXL345_FIFO_MODE_STREAM);
    accelerometer_write_register(sensor, ADXL345_REG_POWER_CTL, ADXL345_POWER_CTL_MEASURE);

    ESP_LOGI(TAG, "ADXL345 at 0x%02X ready", sensor->address);
    return ESP_OK;
}

esp_err_t accelerometer_init(void) {
    accel_sensor.address    = ADXL345_ADDR;
    accel_sensor.dev_handle = NULL;
    accel_sensor.present    = false;

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        return err;
    }
    i2c_master_bus_handle_t bus = i2c_bus_get();

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = accel_sensor.address,
        .scl_speed_hz    = ACCEL_CLK_HZ,
    };

    err = i2c_master_bus_add_device(bus, &dev_config, &accel_sensor.dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "addr 0x%02X: device handle failed: %s",
                 accel_sensor.address, esp_err_to_name(err));
        return err;
    }

    /* Probe the sensor with retries — I2C can fail intermittently. */
    err = ESP_ERR_NOT_FOUND;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        err = i2c_master_probe(bus, accel_sensor.address, ACCEL_TIMEOUT_MS);
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
