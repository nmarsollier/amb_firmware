/* Accelerometer — accelerometer_read.c
 *
 * Purpose: read raw acceleration from the ADXL345 and convert it to g.
 *
 * The 6 data bytes at DATAX0..DATAZ1 hold X, Y, Z as little-endian, two's
 * complement 16-bit values. Acceleration is scaled by 256 LSB/g (±2 g range).
 */
#include "accelerometer_internal.h"

esp_err_t accelerometer_read_sample(AccelSample *out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!accel_sensor.present || accel_sensor.dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg = ADXL345_REG_DATAX0;
    uint8_t data[6] = { 0 };

    esp_err_t err = i2c_master_transmit_receive(accel_sensor.dev_handle, &reg, 1,
                                                data, 6, ACCEL_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    int16_t raw_x = (int16_t)((data[1] << 8) | data[0]);
    int16_t raw_y = (int16_t)((data[3] << 8) | data[2]);
    int16_t raw_z = (int16_t)((data[5] << 8) | data[4]);

    out->x_g = raw_x / ADXL345_LSB_PER_G;
    out->y_g = raw_y / ADXL345_LSB_PER_G;
    out->z_g = raw_z / ADXL345_LSB_PER_G;

    return ESP_OK;
}
