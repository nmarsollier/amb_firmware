/* sht31_internal.h — internal API shared among the sht31 files. */

#pragma once

#include <stdint.h>

#include "config.h"
#include "driver/i2c_master.h"
#include "sht31.h"

/* ── I2C configuration ────────────────────────────────────── */

#define SHT31_I2C_PORT    I2C_NUM_0
#define SHT31_CLK_HZ      100000    /* 100 kHz standard mode */
#define SHT31_TIMEOUT_MS  100

/* ── SHT31 commands ───────────────────────────────────────── */

/* Single shot, high repeatability, clock stretching enabled. */
#define SHT31_CMD_MEASURE_HIGH_CS_MSB  0x2C
#define SHT31_CMD_MEASURE_HIGH_CS_LSB  0x06

/* ── Sensor state ─────────────────────────────────────────── */

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t dev_handle;
    bool present;
} Sht31Sensor;

extern Sht31Sensor sht31_sensor;
