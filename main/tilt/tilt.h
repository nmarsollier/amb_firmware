#pragma once

#include <stdbool.h>

/*
 * Tilt subsystem — measures the ADXL345's orientation (pitch/roll) from the
 * gravity vector, for the telescope's inclination.
 *
 * A single sample is read on each tilt_update() call (from the runtime loop)
 * and converted to pitch/roll (degrees). A missing sensor or sustained I2C
 * fault leaves the module disabled and, through the led module, flags the
 * board LED as error.
 */

typedef struct {
    bool valid;
    float pitch_deg;  /* X-axis tilt from horizontal, degrees */
    float roll_deg;   /* Y-axis tilt from horizontal, degrees */
    float ax_g;       /* gravity, X axis (g) */
    float ay_g;       /* gravity, Y axis (g) */
    float az_g;       /* gravity, Z axis (g) */
} TiltReading;

void tilt_init(void);

/* Read one sample and refresh the tilt (called from the runtime loop). */
void tilt_update(void);

/* Snapshot the current pitch/roll (degrees). */
void tilt_get_reading(TiltReading *out);

bool tilt_get_enabled(void);

/* True when the ADXL345 is present and its reads are not currently failing —
 * the signal the led module uses to flag a sensor error. */
bool tilt_get_healthy(void);
