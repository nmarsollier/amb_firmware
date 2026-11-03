/* Vibration — vibration.c
 *
 * Purpose: turn ADXL345 acceleration into a per-axis movement figure — the
 * demeaned RMS of the acceleration in m/s², over one second. Queries return
 * the last completed second's measurement.
 *
 * The mean is removed per axis first so gravity (DC) is not reported as
 * vibration. Sampling runs at the runtime-loop cadence (5 Hz); a faster
 * sampler can be added later.
 */
#include "vibration.h"

#include <math.h>

#include "accelerometer.h"
#include "esp_log.h"

static const char *TAG = "VIBRATION";

#define G_TO_MPS2               9.80665f
#define VIB_SAMPLES_PER_SECOND  5       /* 5 Hz runtime loop → 1 s */
#define VIB_FAIL_THRESHOLD      2       /* consecutive failed reads → sensor fault */

static bool s_enabled;
static bool s_valid;
static bool s_failed;
static float s_rms_x;
static float s_rms_y;
static float s_rms_z;
static float s_rms_total;

/* Per-second accumulator. */
static uint32_t s_count;
static uint32_t s_consec_fail;
static double s_sum_x, s_sum_y, s_sum_z;
static double s_sum_xx, s_sum_yy, s_sum_zz;

void vibration_init(void) {
    s_enabled = (accelerometer_init() == ESP_OK);
    s_valid = false;
    s_failed = false;
    s_consec_fail = 0;
    s_count = 0;
    s_sum_x = s_sum_y = s_sum_z = 0.0;
    s_sum_xx = s_sum_yy = s_sum_zz = 0.0;

    if (s_enabled) {
        ESP_LOGI(TAG, "accelerometer present");
    } else {
        ESP_LOGW(TAG, "accelerometer absent — vibration unavailable");
    }
}

void vibration_update(void) {
    if (!s_enabled) {
        return;
    }

    AccelSample s;
    if (accelerometer_read_sample(&s) != ESP_OK) {
        s_consec_fail++;
        if (s_consec_fail >= VIB_FAIL_THRESHOLD) {
            s_failed = true;
        }
        return;
    }
    s_consec_fail = 0;
    s_failed = false;

    s_sum_x += s.x_g;
    s_sum_y += s.y_g;
    s_sum_z += s.z_g;
    s_sum_xx += (double) s.x_g * s.x_g;
    s_sum_yy += (double) s.y_g * s.y_g;
    s_sum_zz += (double) s.z_g * s.z_g;
    s_count++;

    if (s_count < VIB_SAMPLES_PER_SECOND) {
        return;
    }

    /* Per-second, per-axis demeaned RMS. */
    float n = (float) s_count;
    float mean_x = (float)(s_sum_x / n);
    float mean_y = (float)(s_sum_y / n);
    float mean_z = (float)(s_sum_z / n);

    float var_x = (float)(s_sum_xx / n) - mean_x * mean_x;
    float var_y = (float)(s_sum_yy / n) - mean_y * mean_y;
    float var_z = (float)(s_sum_zz / n) - mean_z * mean_z;

    s_rms_x = sqrtf(var_x > 0.0f ? var_x : 0.0f) * G_TO_MPS2;
    s_rms_y = sqrtf(var_y > 0.0f ? var_y : 0.0f) * G_TO_MPS2;
    s_rms_z = sqrtf(var_z > 0.0f ? var_z : 0.0f) * G_TO_MPS2;
    s_rms_total = sqrtf(s_rms_x * s_rms_x + s_rms_y * s_rms_y + s_rms_z * s_rms_z);
    s_valid = true;

    /* Reset the per-second accumulator. */
    s_count = 0;
    s_sum_x = s_sum_y = s_sum_z = 0.0;
    s_sum_xx = s_sum_yy = s_sum_zz = 0.0;
}

bool vibration_get_enabled(void)   { return s_enabled; }
bool vibration_get_valid(void)     { return s_valid; }
bool vibration_get_healthy(void)   { return s_enabled && !s_failed; }
float vibration_get_rms_x(void)    { return s_rms_x; }
float vibration_get_rms_y(void)    { return s_rms_y; }
float vibration_get_rms_z(void)    { return s_rms_z; }
float vibration_get_rms_total(void){ return s_rms_total; }
