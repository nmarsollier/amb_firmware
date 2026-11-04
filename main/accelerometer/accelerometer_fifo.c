/* Accelerometer — accelerometer_fifo.c — FIFO read for high-rate sampling.
 *
 * The ADXL345 runs in FIFO stream mode; the vibration task drains the FIFO by
 * reading one sample (6 bytes) at a time, converting raw to g here so the caller
 * never deals with the wire format.
 */
#include "accelerometer_internal.h"

#include "esp_err.h"

esp_err_t accelerometer_read_fifo(AccelSample *samples, size_t n_samples) {
    if (samples == NULL || n_samples == 0 || n_samples > ADXL345_FIFO_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < n_samples; i++) {
        uint8_t reg = ADXL345_REG_DATAX0;
        uint8_t data[6] = { 0 };
        esp_err_t err = i2c_master_transmit_receive(accel_sensor.dev_handle, &reg, 1,
                                                    data, 6, ACCEL_TIMEOUT_MS);
        if (err != ESP_OK) {
            return err;
        }
        samples[i].x_g = (float)(int16_t)(data[0] | (data[1] << 8)) / ADXL345_LSB_PER_G;
        samples[i].y_g = (float)(int16_t)(data[2] | (data[3] << 8)) / ADXL345_LSB_PER_G;
        samples[i].z_g = (float)(int16_t)(data[4] | (data[5] << 8)) / ADXL345_LSB_PER_G;
    }
    return ESP_OK;
}

esp_err_t accelerometer_fifo_entries(uint8_t *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = ADXL345_REG_FIFO_STATUS;
    uint8_t status = 0;
    esp_err_t err = i2c_master_transmit_receive(accel_sensor.dev_handle, &reg, 1,
                                                &status, 1, ACCEL_TIMEOUT_MS);
    if (err == ESP_OK) {
        *out = status & 0x3F;   /* 6-bit FIFO entries field */
    }
    return err;
}
