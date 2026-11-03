/* Tilt — tilt.c
 *
 * Purpose: measure the ADXL345's orientation (pitch/roll) from the gravity
 * vector, for the telescope's inclination.
 *
 * A single sample is read on each tilt_update() call (from the runtime loop)
 * and converted to pitch/roll in degrees.
 */
#include "tilt.h"

#include <math.h>

#include "accelerometer.h"
#include "esp_log.h"

static const char *TAG = "TILT";

#define TILT_FAIL_THRESHOLD     2
#define RAD_TO_DEG              (180.0f / 3.14159265358979f)

static bool s_enabled;
static bool s_valid;
static bool s_failed;
static uint32_t s_consec_fail;

static float s_pitch_deg;
static float s_roll_deg;
static float s_ax_g, s_ay_g, s_az_g;

void tilt_init(void) {
    s_enabled = (accelerometer_init() == ESP_OK);
    s_valid = false;
    s_failed = false;
    s_consec_fail = 0;
    s_pitch_deg = 0.0f;
    s_roll_deg = 0.0f;
    s_ax_g = s_ay_g = s_az_g = 0.0f;

    if (s_enabled) {
        ESP_LOGI(TAG, "accelerometer present, measuring tilt");
    } else {
        ESP_LOGW(TAG, "accelerometer absent — tilt unavailable");
    }
}

void tilt_update(void) {
    if (!s_enabled) {
        return;
    }

    AccelSample s;
    if (accelerometer_read_sample(&s) != ESP_OK) {
        s_consec_fail++;
        if (s_consec_fail >= TILT_FAIL_THRESHOLD) {
            s_failed = true;
        }
        return;
    }
    s_consec_fail = 0;
    s_failed = false;

    s_ax_g = s.x_g;
    s_ay_g = s.y_g;
    s_az_g = s.z_g;

    s_pitch_deg = atan2f(s.x_g, sqrtf(s.y_g * s.y_g + s.z_g * s.z_g)) * RAD_TO_DEG;
    s_roll_deg  = atan2f(s.y_g, sqrtf(s.x_g * s.x_g + s.z_g * s.z_g)) * RAD_TO_DEG;
    s_valid = true;
}

void tilt_get_reading(TiltReading *out) {
    if (out == NULL) {
        return;
    }
    out->valid = s_valid;
    out->pitch_deg = s_pitch_deg;
    out->roll_deg = s_roll_deg;
    out->ax_g = s_ax_g;
    out->ay_g = s_ay_g;
    out->az_g = s_az_g;
}

bool tilt_get_enabled(void)  { return s_enabled; }
bool tilt_get_healthy(void)  { return s_enabled && !s_failed; }
