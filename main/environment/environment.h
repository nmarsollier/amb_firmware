#pragma once

#include <stdbool.h>

/*
 * Environment sensors — ambient temperature/humidity from the SHT31 (I2C). The
 * dew point is computed from the ambient temperature and humidity.
 *
 * Measurements are refreshed by environment_update() from the runtime loop and
 * cached; the getters return the last cached values.  environment_get_reading()
 * returns a coherent snapshot (all fields from the same measurement) so a
 * response never mixes a new temperature with a stale humidity.
 */

/* One coherent ambient measurement. */
typedef struct {
    float ambient_c;
    float humidity_pct;
    float dew_point_c;
    bool valid;
} EnvironmentReading;

void environment_init(void);

/* Refresh the SHT31 read into the cache. */
void environment_update(void);

/* True when the last SHT31 read succeeded (ambient/humidity/dew are valid). */
bool environment_ambient_valid(void);

/* Coherent snapshot of the last measurement, taken atomically. */
EnvironmentReading environment_get_reading(void);

float environment_get_ambient_c(void);
float environment_get_humidity_pct(void);
float environment_get_dew_point_c(void);
