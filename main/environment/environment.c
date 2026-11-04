/* Environment — environment.c — ambient temperature and humidity.
 *
 * Ambient temperature and humidity come from the SHT31 over I2C (driver: sht31).
 * The dew point is computed from the ambient temperature and humidity (Magnus
 * formula).
 */
#include "environment.h"

#include <math.h>

#include "esp_log.h"
#include "sht31.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ENVIRONMENT";

/* Seed values used until the first successful SHT31 read. */
#define AMBIENT_C_SEED  25.0f
#define HUMIDITY_SEED   50.0f

static float s_ambient_c;
static float s_humidity_pct;
static float s_dew_point_c;
static bool  s_ambient_valid;

/* Guards the four cached fields so environment_get_reading() returns a coherent
 * snapshot (all from the same measurement). */
static SemaphoreHandle_t s_mutex;

/* Magnus-formula dew point.  Returns NAN when the domain is invalid (humidity
 * <= 0) or the result is not finite. */
static float dew_point_c(float temp_c, float humidity_pct) {
    if (humidity_pct <= 0.0f) {
        return NAN;
    }

    const float a = 17.62f;
    const float b = 243.12f;
    float gamma = logf(humidity_pct / 100.0f) + a * temp_c / (b + temp_c);
    float dew = b * gamma / (a - gamma);
    return isfinite(dew) ? dew : NAN;
}

void environment_init(void) {
    s_ambient_c = AMBIENT_C_SEED;
    s_humidity_pct = HUMIDITY_SEED;
    s_dew_point_c = dew_point_c(s_ambient_c, s_humidity_pct);
    s_ambient_valid = false;

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create mutex");
    }

    if (sht31_init() != ESP_OK) {
        ESP_LOGW(TAG, "SHT31 unavailable — ambient/humidity reporting disabled");
    }

    environment_update();
}

void environment_update(void) {
    /* Do the I2C transaction outside the lock; only the cache update is
     * serialised. */
    float t = s_ambient_c;
    float h = s_humidity_pct;
    bool ok = false;

    if (sht31_is_present()) {
        /* Retry once — a single-shot can fail transiently. */
        esp_err_t err = sht31_read(&t, &h);
        if (err != ESP_OK) {
            err = sht31_read(&t, &h);
        }
        ok = (err == ESP_OK);
    }

    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (ok) {
            s_ambient_c = t;
            s_humidity_pct = h;
            s_dew_point_c = dew_point_c(t, h);
            s_ambient_valid = true;
        } else {
            s_ambient_valid = false;
        }
        xSemaphoreGive(s_mutex);
    }
}

bool environment_ambient_valid(void) {
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        bool v = s_ambient_valid;
        xSemaphoreGive(s_mutex);
        return v;
    }
    return false;
}

EnvironmentReading environment_get_reading(void) {
    EnvironmentReading r = { 0 };
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        r.ambient_c = s_ambient_c;
        r.humidity_pct = s_humidity_pct;
        r.dew_point_c = s_dew_point_c;
        r.valid = s_ambient_valid;
        xSemaphoreGive(s_mutex);
    }
    return r;
}

float environment_get_ambient_c(void) {
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        float v = s_ambient_c;
        xSemaphoreGive(s_mutex);
        return v;
    }
    return s_ambient_c;
}

float environment_get_humidity_pct(void) {
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        float v = s_humidity_pct;
        xSemaphoreGive(s_mutex);
        return v;
    }
    return s_humidity_pct;
}

float environment_get_dew_point_c(void) {
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        float v = s_dew_point_c;
        xSemaphoreGive(s_mutex);
        return v;
    }
    return s_dew_point_c;
}
