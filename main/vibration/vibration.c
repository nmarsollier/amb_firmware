/* Vibration — vibration.c
 *
 * Purpose: turn ADXL345 acceleration into per-axis movement figures at a high
 * sample rate (100 Hz): acceleration RMS/peak (milli-g) and velocity RMS/peak
 * (µm/s), plus an ISO 10816-1 severity zone, accumulated between reads.
 *
 * Sampling runs in a dedicated FreeRTOS task that drains the ADXL345 FIFO every
 * 10 ms. Acceleration is high-passed at 5 Hz (removing gravity/DC) and
 * integrated to velocity. vibration_take_reading() returns the figures for the
 * period since the previous read and resets the accumulators (the latching peaks
 * flag whether a large vibration happened between reads).
 */
#include "vibration.h"

#include <math.h>
#include <string.h>

#include "accelerometer.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "VIBRATION";

#define G_TO_MPS2               9.80665f
#define MS2_TO_MG               (1000.0f / G_TO_MPS2)   /* m/s² → milli-g */
#define M_TO_UM                 1.0e6f                  /* m/s → µm/s */
#define VIB_FAIL_THRESHOLD      2
#define VIB_TASK_PERIOD_MS      10
#define VIB_TASK_STACK_SIZE     8192
#define VIB_TASK_PRIORITY       10
#define VIB_TASK_CORE           1
#define VIB_FIFO_MAX_SAMPLES    32
#define VIB_WINDOW_SAMPLES      VIB_SAMPLE_RATE_HZ   /* 1 s fallback reset */

/* Derived DSP constants (compile time). */
#define PI_F            3.14159265358979f
#define VIB_DT          (1.0f / (float) VIB_SAMPLE_RATE_HZ)
#define VIB_HP_RC       (1.0f / (2.0f * PI_F * VIB_HP_CUTOFF_HZ))
#define VIB_HP_ALPHA    (VIB_HP_RC / (VIB_HP_RC + VIB_DT))

#define RAD_TO_DEG              (180.0f / 3.14159265358979f)
#define GRAV_TAU_S              1.0f
#define GRAV_ALPHA              (VIB_DT / GRAV_TAU_S)

/* ── State (the accumulators are guarded by s_mutex) ────────── */

static SemaphoreHandle_t s_mutex;

static bool s_enabled;
static bool s_failed;
static uint32_t s_consec_fail;

/* High-pass (DC-block) + integrator state, per axis. */
static float s_hp_in_x, s_hp_out_x, s_vel_x;
static float s_hp_in_y, s_hp_out_y, s_vel_y;
static float s_hp_in_z, s_hp_out_z, s_vel_z;

/* Accumulators since the last read/reset. */
static uint32_t s_count;
static double s_sum_a2_x, s_sum_a2_y, s_sum_a2_z;
static double s_sum_v2_x, s_sum_v2_y, s_sum_v2_z;
static float s_peak_a_x, s_peak_a_y, s_peak_a_z;   /* max |accel|, m/s² */
static float s_peak_v_x, s_peak_v_y, s_peak_v_z;   /* max |velocity|, m/s */

/* Gravity (DC) low-pass — for orientation. */
static float s_grav_x, s_grav_y, s_grav_z;

/* ── DSP (mutex held) ──────────────────────────────────────── */

static void process_axis(float a, float *hp_in, float *hp_out, float *vel,
                         double *sum_a2, double *sum_v2,
                         float *peak_a, float *peak_v) {
    float hp = VIB_HP_ALPHA * (*hp_out + a - *hp_in);
    *hp_in = a;
    *hp_out = hp;
    *vel += hp * VIB_DT;

    *sum_a2 += (double) hp * hp;
    *sum_v2 += (double) (*vel) * (*vel);

    float a_abs = fabsf(hp);
    if (a_abs > *peak_a) {
        *peak_a = a_abs;
    }
    float v_abs = fabsf(*vel);
    if (v_abs > *peak_v) {
        *peak_v = v_abs;
    }
}

static void process_sample(const AccelSample *s) {
    float ax = s->x_g * G_TO_MPS2;
    float ay = s->y_g * G_TO_MPS2;
    float az = s->z_g * G_TO_MPS2;

    process_axis(ax, &s_hp_in_x, &s_hp_out_x, &s_vel_x,
                 &s_sum_a2_x, &s_sum_v2_x, &s_peak_a_x, &s_peak_v_x);
    process_axis(ay, &s_hp_in_y, &s_hp_out_y, &s_vel_y,
                 &s_sum_a2_y, &s_sum_v2_y, &s_peak_a_y, &s_peak_v_y);
    process_axis(az, &s_hp_in_z, &s_hp_out_z, &s_vel_z,
                 &s_sum_a2_z, &s_sum_v2_z, &s_peak_a_z, &s_peak_v_z);

    /* Gravity low-pass (for orientation) — the raw g, not the high-passed one. */
    s_grav_x += GRAV_ALPHA * (s->x_g - s_grav_x);
    s_grav_y += GRAV_ALPHA * (s->y_g - s_grav_y);
    s_grav_z += GRAV_ALPHA * (s->z_g - s_grav_z);

    s_count++;
}

static char classify_iso(float vel_rms_mm_s) {
    if (vel_rms_mm_s < ISO10816_CLASS_AB) return 'A';
    if (vel_rms_mm_s < ISO10816_CLASS_BC) return 'B';
    if (vel_rms_mm_s < ISO10816_CLASS_CD) return 'C';
    return 'D';
}

/* Reset the accumulators (keeps the high-pass filter state). */
static void reset_accumulators(void) {
    s_count = 0;
    s_sum_a2_x = s_sum_a2_y = s_sum_a2_z = 0.0;
    s_sum_v2_x = s_sum_v2_y = s_sum_v2_z = 0.0;
    s_peak_a_x = s_peak_a_y = s_peak_a_z = 0.0f;
    s_peak_v_x = s_peak_v_y = s_peak_v_z = 0.0f;
    s_vel_x = s_vel_y = s_vel_z = 0.0f;
}

/* Compute the figures from the accumulators and reset them (mutex held). */
static void compute_reading(VibrationReading *out) {
    if (s_count == 0) {
        out->valid = false;
        return;
    }

    float n = (float) s_count;

    out->rms_x = sqrtf((float)(s_sum_a2_x / n)) * MS2_TO_MG;
    out->rms_y = sqrtf((float)(s_sum_a2_y / n)) * MS2_TO_MG;
    out->rms_z = sqrtf((float)(s_sum_a2_z / n)) * MS2_TO_MG;
    out->rms_total = sqrtf(out->rms_x * out->rms_x + out->rms_y * out->rms_y +
                           out->rms_z * out->rms_z);

    out->accel_peak_x = s_peak_a_x * MS2_TO_MG;
    out->accel_peak_y = s_peak_a_y * MS2_TO_MG;
    out->accel_peak_z = s_peak_a_z * MS2_TO_MG;
    out->accel_peak_total = sqrtf(out->accel_peak_x * out->accel_peak_x +
                                  out->accel_peak_y * out->accel_peak_y +
                                  out->accel_peak_z * out->accel_peak_z);

    float vr_x = sqrtf((float)(s_sum_v2_x / n));
    float vr_y = sqrtf((float)(s_sum_v2_y / n));
    float vr_z = sqrtf((float)(s_sum_v2_z / n));
    out->vel_rms_x = vr_x * M_TO_UM;
    out->vel_rms_y = vr_y * M_TO_UM;
    out->vel_rms_z = vr_z * M_TO_UM;
    float vr_total = sqrtf(vr_x * vr_x + vr_y * vr_y + vr_z * vr_z);
    out->vel_rms_total = vr_total * M_TO_UM;
    out->iso_zone = classify_iso(vr_total * 1000.0f);

    out->vel_peak_x = s_peak_v_x * M_TO_UM;
    out->vel_peak_y = s_peak_v_y * M_TO_UM;
    out->vel_peak_z = s_peak_v_z * M_TO_UM;
    out->vel_peak_total = sqrtf(out->vel_peak_x * out->vel_peak_x +
                                out->vel_peak_y * out->vel_peak_y +
                                out->vel_peak_z * out->vel_peak_z);

    out->valid = true;

    reset_accumulators();
}

/* ── Sampling task ─────────────────────────────────────────── */

static void vibration_task(void *arg) {
    (void) arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(VIB_TASK_PERIOD_MS);

    while (true) {
        vTaskDelayUntil(&last_wake, period);

        uint8_t entries = 0;
        bool i2c_ok = (accelerometer_fifo_entries(&entries) == ESP_OK);
        if (i2c_ok && entries > 0) {
            if (entries > VIB_FIFO_MAX_SAMPLES) {
                entries = VIB_FIFO_MAX_SAMPLES;
            }
            AccelSample buf[VIB_FIFO_MAX_SAMPLES];
            if (accelerometer_read_fifo(buf, entries) == ESP_OK) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                for (uint8_t i = 0; i < entries; i++) {
                    process_sample(&buf[i]);
                }
                if (s_count >= VIB_WINDOW_SAMPLES) {
                    reset_accumulators();   /* time-based fallback reset */
                }
                xSemaphoreGive(s_mutex);
            } else {
                i2c_ok = false;
            }
        }

        if (i2c_ok) {
            s_consec_fail = 0;
            s_failed = false;
        } else {
            s_consec_fail++;
            if (s_consec_fail >= VIB_FAIL_THRESHOLD) {
                s_failed = true;
            }
        }
    }
}

/* ── Public API ────────────────────────────────────────────── */

void vibration_init(void) {
    s_enabled = (accelerometer_init() == ESP_OK);
    s_failed = false;
    s_consec_fail = 0;

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create mutex — vibration disabled");
        s_enabled = false;
    }

    s_count = 0;
    s_sum_a2_x = s_sum_a2_y = s_sum_a2_z = 0.0;
    s_sum_v2_x = s_sum_v2_y = s_sum_v2_z = 0.0;
    s_peak_a_x = s_peak_a_y = s_peak_a_z = 0.0f;
    s_peak_v_x = s_peak_v_y = s_peak_v_z = 0.0f;
    s_hp_in_x = s_hp_out_x = s_vel_x = 0.0f;
    s_hp_in_y = s_hp_out_y = s_vel_y = 0.0f;
    s_hp_in_z = s_hp_out_z = s_vel_z = 0.0f;
    s_grav_x = s_grav_y = s_grav_z = 0.0f;

    if (s_enabled) {
        xTaskCreatePinnedToCore(vibration_task, "vibration", VIB_TASK_STACK_SIZE,
                                NULL, VIB_TASK_PRIORITY, NULL, VIB_TASK_CORE);
        ESP_LOGI(TAG, "accelerometer present, sampling at %d Hz", VIB_SAMPLE_RATE_HZ);
    } else {
        ESP_LOGW(TAG, "accelerometer absent — vibration unavailable");
    }
}

void vibration_take_reading(VibrationReading *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!s_enabled) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    compute_reading(out);
    xSemaphoreGive(s_mutex);
}

bool vibration_get_enabled(void) { return s_enabled; }
bool vibration_get_healthy(void) { return s_enabled && !s_failed; }

void vibration_get_orientation(OrientationReading *out) {
    if (out == NULL) {
        return;
    }
    out->pitch_deg = 0.0f;
    out->roll_deg = 0.0f;
    out->ax_g = out->ay_g = out->az_g = 0.0f;
    if (!s_enabled) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    out->ax_g = s_grav_x;
    out->ay_g = s_grav_y;
    out->az_g = s_grav_z;
    out->pitch_deg = atan2f(s_grav_x, sqrtf(s_grav_y * s_grav_y + s_grav_z * s_grav_z)) * RAD_TO_DEG;
    out->roll_deg  = atan2f(s_grav_y, sqrtf(s_grav_x * s_grav_x + s_grav_z * s_grav_z)) * RAD_TO_DEG;
    xSemaphoreGive(s_mutex);
}
