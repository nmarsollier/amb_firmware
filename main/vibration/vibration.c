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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "VIBRATION";

#define G_TO_MPS2               9.80665f
#define MS2_TO_MG               (1000.0f / G_TO_MPS2)   /* m/s² → milli-g */
#define M_TO_UM                 1.0e6f                  /* m/s → µm/s */
#define VIB_FAIL_THRESHOLD      2
#define VIB_STALE_US            250000
#define VIB_RECOVERY_US         1000000
#define VIB_TASK_PERIOD_MS      10
#define VIB_TASK_STACK_SIZE     8192
#define VIB_TASK_PRIORITY       10
#define VIB_TASK_CORE           1
#define VIB_FIFO_MAX_SAMPLES    32

/* Derived DSP constants (compile time). */
#define PI_F            3.14159265358979f
#define VIB_DT          (1.0f / (float) VIB_SAMPLE_RATE_HZ)
#define VIB_HP_RC       (1.0f / (2.0f * PI_F * VIB_HP_CUTOFF_HZ))
#define VIB_HP_ALPHA    (VIB_HP_RC / (VIB_HP_RC + VIB_DT))

#define RAD_TO_DEG              (180.0f / 3.14159265358979f)
#define GRAV_TAU_S              1.0f
#define GRAV_ALPHA              (VIB_DT / GRAV_TAU_S)

/* Leaky velocity integrator.  A gravity reorientation feeds a decaying transient
 * into the integrator, which would otherwise leave a permanent DC velocity
 * offset (see #5).  A small leak — time constant 1 s — removes that offset once
 * the transient decays, without materially affecting vibration frequencies well
 * above 1/(2π·τ) ≈ 0.16 Hz. */
#define VIB_VEL_TAU_S           1.0f
#define VIB_VEL_LEAK            (1.0f - VIB_DT / VIB_VEL_TAU_S)

/* ── State (the accumulators are guarded by s_mutex) ────────── */

static SemaphoreHandle_t s_mutex;

static bool s_enabled;
static bool s_failed;
static uint32_t s_consec_fail;
static int64_t s_last_sample_us;
static int64_t s_last_recovery_us;
static bool s_sensor_ready; /* sampling task only */

/* High-pass (DC-block) + integrator state, per axis.  These are continuous:
 * they are never reset on read, only the statistics below are. */
static float s_hp_in_x, s_hp_out_x, s_vel_x;
static float s_hp_in_y, s_hp_out_y, s_vel_y;
static float s_hp_in_z, s_hp_out_z, s_vel_z;

/* Accumulators since the last read/reset. */
static uint32_t s_count;
static double s_sum_a2_x, s_sum_a2_y, s_sum_a2_z;
static double s_sum_v2_x, s_sum_v2_y, s_sum_v2_z;
static float s_peak_a_x, s_peak_a_y, s_peak_a_z;       /* max |accel|, m/s² */
static float s_peak_v_x, s_peak_v_y, s_peak_v_z;       /* max |velocity|, m/s */
static float s_peak_a_total;                            /* max vector norm, m/s² */
static float s_peak_v_total;                            /* max vector norm, m/s */

/* Gravity (DC) low-pass — for orientation. */
static float s_grav_x, s_grav_y, s_grav_z;

/* True until the first sample is processed; the filters are seeded with it. */
static bool s_first_sample;

/* ── DSP (mutex held) ──────────────────────────────────────── */

static float process_axis(float a, float *hp_in, float *hp_out, float *vel,
                          double *sum_a2, double *sum_v2,
                          float *peak_a, float *peak_v) {
    float hp = VIB_HP_ALPHA * (*hp_out + a - *hp_in);
    *hp_in = a;
    *hp_out = hp;
    *vel = *vel * VIB_VEL_LEAK + hp * VIB_DT;

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

    return hp;
}

static void process_sample(const AccelSample *s) {
    if (s_first_sample) {
        /* Seed the high-pass filter and the gravity low-pass with the first
         * sample so the initial gravity step does not inject a transient into
         * the velocity (see #5). */
        s_hp_in_x = s->x_g * G_TO_MPS2;
        s_hp_in_y = s->y_g * G_TO_MPS2;
        s_hp_in_z = s->z_g * G_TO_MPS2;
        s_hp_out_x = s_hp_out_y = s_hp_out_z = 0.0f;
        s_vel_x = s_vel_y = s_vel_z = 0.0f;
        s_grav_x = s->x_g;
        s_grav_y = s->y_g;
        s_grav_z = s->z_g;
        s_first_sample = false;
        return;
    }

    float ax = s->x_g * G_TO_MPS2;
    float ay = s->y_g * G_TO_MPS2;
    float az = s->z_g * G_TO_MPS2;

    float hp_x = process_axis(ax, &s_hp_in_x, &s_hp_out_x, &s_vel_x,
                              &s_sum_a2_x, &s_sum_v2_x, &s_peak_a_x, &s_peak_v_x);
    float hp_y = process_axis(ay, &s_hp_in_y, &s_hp_out_y, &s_vel_y,
                              &s_sum_a2_y, &s_sum_v2_y, &s_peak_a_y, &s_peak_v_y);
    float hp_z = process_axis(az, &s_hp_in_z, &s_hp_out_z, &s_vel_z,
                              &s_sum_a2_z, &s_sum_v2_z, &s_peak_a_z, &s_peak_v_z);

    /* Total peak is the maximum per-sample vector norm, not the norm of the
     * per-axis maxima (which may occur at different instants). */
    float a_norm = sqrtf(hp_x * hp_x + hp_y * hp_y + hp_z * hp_z);
    if (a_norm > s_peak_a_total) {
        s_peak_a_total = a_norm;
    }
    float v_norm = sqrtf(s_vel_x * s_vel_x + s_vel_y * s_vel_y + s_vel_z * s_vel_z);
    if (v_norm > s_peak_v_total) {
        s_peak_v_total = v_norm;
    }

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

/* Reset the statistics accumulators (keeps the filter and integrator state). */
static void reset_accumulators(void) {
    s_count = 0;
    s_sum_a2_x = s_sum_a2_y = s_sum_a2_z = 0.0;
    s_sum_v2_x = s_sum_v2_y = s_sum_v2_z = 0.0;
    s_peak_a_x = s_peak_a_y = s_peak_a_z = 0.0f;
    s_peak_v_x = s_peak_v_y = s_peak_v_z = 0.0f;
    s_peak_a_total = 0.0f;
    s_peak_v_total = 0.0f;
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
    out->accel_peak_total = s_peak_a_total * MS2_TO_MG;

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
    out->vel_peak_total = s_peak_v_total * M_TO_UM;

    out->valid = true;

    reset_accumulators();
}

/* ── Sampling task ─────────────────────────────────────────── */

/* One polling cycle, separated from scheduling so failures can be exercised
 * with a fake sensor. I2C never runs while holding the public-state mutex. */
static void vibration_poll(void) {
    uint8_t entries = 0;
    AccelSample buf[VIB_FIFO_MAX_SAMPLES];
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_sensor_ready) {
        err = accelerometer_fifo_entries(&entries);
        if (err == ESP_OK && entries > 0) {
            if (entries > VIB_FIFO_MAX_SAMPLES) {
                err = ESP_ERR_INVALID_RESPONSE;
            } else {
                err = accelerometer_read_fifo(buf, entries);
            }
        }
    }

    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        s_consec_fail = 0;
        if (entries > 0) {
            /* A long gap invalidates both the interval and the filter state. */
            if (now - s_last_sample_us >= VIB_STALE_US || s_failed) {
                reset_accumulators();
                s_first_sample = true;
            }
            for (uint8_t i = 0; i < entries; i++) {
                process_sample(&buf[i]);
            }
            s_last_sample_us = now;
            s_failed = false;
        }
    } else {
        if (s_consec_fail < VIB_FAIL_THRESHOLD) {
            s_consec_fail++;
        }
        /* A partial FIFO read may have consumed samples; do not integrate
         * across an unknown gap, even when the next transaction succeeds. */
        reset_accumulators();
        s_first_sample = true;
    }
    bool needs_recovery = !s_sensor_ready || s_consec_fail >= VIB_FAIL_THRESHOLD ||
                          now - s_last_sample_us >= VIB_STALE_US;
    if (needs_recovery) {
        s_failed = true;
        reset_accumulators();
        s_first_sample = true;
    }
    xSemaphoreGive(s_mutex);

    if (needs_recovery && now - s_last_recovery_us >= VIB_RECOVERY_US) {
        s_sensor_ready = (accelerometer_recover() == ESP_OK);
        s_last_recovery_us = esp_timer_get_time();
        if (s_sensor_ready) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_enabled = true;
            s_consec_fail = 0;
            s_last_sample_us = s_last_recovery_us;
            /* Remain unhealthy until actual samples arrive. */
            xSemaphoreGive(s_mutex);
        }
    }
}

static void vibration_task(void *arg) {
    (void) arg;
    const TickType_t period = pdMS_TO_TICKS(VIB_TASK_PERIOD_MS);
    while (true) {
        /* The hardware FIFO preserves the 100 Hz sample cadence. Always sleep
         * after I/O: missed deadlines must not produce catch-up polling bursts. */
        vTaskDelay(period > 0 ? period : 1);
        vibration_poll();
    }
}

/* ── Public API ────────────────────────────────────────────── */

void vibration_init(void) {
    s_enabled = (accelerometer_init() == ESP_OK);
    s_sensor_ready = s_enabled;
    s_failed = true;
    s_consec_fail = 0;
    s_first_sample = true;
    s_last_sample_us = s_last_recovery_us = esp_timer_get_time();

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create mutex — vibration disabled");
        s_enabled = false;
        return;
    }

    s_count = 0;
    s_sum_a2_x = s_sum_a2_y = s_sum_a2_z = 0.0;
    s_sum_v2_x = s_sum_v2_y = s_sum_v2_z = 0.0;
    s_peak_a_x = s_peak_a_y = s_peak_a_z = 0.0f;
    s_peak_v_x = s_peak_v_y = s_peak_v_z = 0.0f;
    s_peak_a_total = 0.0f;
    s_peak_v_total = 0.0f;
    s_hp_in_x = s_hp_out_x = s_vel_x = 0.0f;
    s_hp_in_y = s_hp_out_y = s_vel_y = 0.0f;
    s_hp_in_z = s_hp_out_z = s_vel_z = 0.0f;
    s_grav_x = s_grav_y = s_grav_z = 0.0f;

    /* Start even when boot probing failed, so a temporarily absent sensor can
     * recover without rebooting the ESP32. */
    if (xTaskCreatePinnedToCore(vibration_task, "vibration", VIB_TASK_STACK_SIZE,
                                NULL, VIB_TASK_PRIORITY, NULL, VIB_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "failed to create vibration task — vibration disabled");
        s_enabled = false;
    }
}

void vibration_take_reading(VibrationReading *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (s_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_enabled && !s_failed) {
        compute_reading(out);
    }
    xSemaphoreGive(s_mutex);
}

bool vibration_get_enabled(void) {
    if (s_mutex == NULL) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool enabled = s_enabled;
    xSemaphoreGive(s_mutex);
    return enabled;
}

bool vibration_get_healthy(void) {
    if (s_mutex == NULL) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool healthy = s_enabled && !s_failed;
    xSemaphoreGive(s_mutex);
    return healthy;
}

void vibration_get_orientation(OrientationReading *out) {
    if (out == NULL) {
        return;
    }
    out->pitch_deg = 0.0f;
    out->roll_deg = 0.0f;
    out->ax_g = out->ay_g = out->az_g = 0.0f;
    if (s_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_enabled || s_failed) {
        xSemaphoreGive(s_mutex);
        return;
    }
    out->ax_g = s_grav_x;
    out->ay_g = s_grav_y;
    out->az_g = s_grav_z;
    out->pitch_deg = atan2f(s_grav_x, sqrtf(s_grav_y * s_grav_y + s_grav_z * s_grav_z)) * RAD_TO_DEG;
    out->roll_deg  = atan2f(s_grav_y, sqrtf(s_grav_x * s_grav_x + s_grav_z * s_grav_z)) * RAD_TO_DEG;
    xSemaphoreGive(s_mutex);
}
