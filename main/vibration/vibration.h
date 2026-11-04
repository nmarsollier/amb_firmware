#pragma once

#include <stdbool.h>

/*
 * Vibration subsystem — high-rate ADXL345 sampling (100 Hz) into per-axis
 * movement figures accumulated since the previous read: acceleration RMS/peak
 * (milli-g) and velocity RMS/peak (µm/s), plus an ISO 10816-1 severity zone.
 *
 * Sampling runs in a dedicated FreeRTOS task. vibration_take_reading() returns
 * the figures for the period since the previous call and resets the
 * accumulators, so each read reflects only new vibrations. A missing sensor or
 * sustained I2C fault leaves the module disabled and, through the led module,
 * flags the board LED as error.
 */

typedef struct {
    bool valid;
    float rms_x, rms_y, rms_z, rms_total;                           /* accel RMS, milli-g */
    float accel_peak_x, accel_peak_y, accel_peak_z, accel_peak_total; /* accel peak, milli-g */
    float vel_rms_x, vel_rms_y, vel_rms_z, vel_rms_total;           /* velocity RMS, µm/s */
    float vel_peak_x, vel_peak_y, vel_peak_z, vel_peak_total;       /* velocity peak, µm/s */
    char  iso_zone;
} VibrationReading;

void vibration_init(void);

/* Snapshot the figures accumulated since the previous call, then reset them.
 * Thread-safe. */
void vibration_take_reading(VibrationReading *out);

bool vibration_get_enabled(void);

/* True when the ADXL345 is present and its reads are not currently failing —
 * the signal the led module uses to flag a sensor error. */
bool vibration_get_healthy(void);

/* Orientation (rotation angles) from the gravity vector. */
typedef struct {
    float pitch_deg;  /* X-axis tilt from horizontal, degrees */
    float roll_deg;   /* Y-axis tilt from horizontal, degrees */
    float ax_g;       /* gravity, X axis (g) */
    float ay_g;       /* gravity, Y axis (g) */
    float az_g;       /* gravity, Z axis (g) */
} OrientationReading;

/* Snapshot the current orientation (gravity low-passed over ~1 s). */
void vibration_get_orientation(OrientationReading *out);
