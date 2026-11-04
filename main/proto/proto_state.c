/* Proto — proto_state.c — STATE: coherent snapshots per target.
 *
 * Routed by `target`: device, environment (ambient only), vibration.
 */
#include "proto_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_system.h"

#include "config.h"
#include "environment.h"
#include "sht31.h"
#include "vibration.h"

static bool get_session(const char *req, char *out, size_t cap) {
    return proto_json_get_string_field(req, "session", out, cap);
}

static const char *reset_reason_str(void) {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_SW:      return "software";
        case ESP_RST_PANIC:   return "panic";
        case ESP_RST_BROWNOUT:return "brownout";
        case ESP_RST_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:return "watchdog";
        case ESP_RST_USB:     return "usb";
        default:              return "unknown";
    }
}

/* ── state / device ────────────────────────────────────────── */

static void state_device(char *out, size_t cap, uint32_t id, const char *session) {
    /* Current sensor health, not just boot-time presence: the LED uses the same
     * signals so the protocol and the status LED can never disagree. */
    bool sensor_fault = !vibration_get_healthy() || !environment_ambient_valid();

    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, "state", id, session, true);
    proto_buf_append(out, cap, &len,
        ",\"data\":{\"health\":\"%s\",\"sensor_health\":\"%s\","
        "\"reset_reason\":\"%s\",\"faults\":[]}}",
        sensor_fault ? "fault" : "ok",
        sensor_fault ? "fault" : "ok",
        reset_reason_str());
}

/* ── state / environment (ambient only) ────────────────────── */

static void state_environment(char *out, size_t cap, uint32_t id, const char *session) {
    EnvironmentReading e = environment_get_reading();

    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, "state", id, session, true);

    const char *error = NULL;
    if (!sht31_is_present()) {
        error = "SENSOR_ABSENT";
    } else if (!e.valid) {
        error = "SENSOR_FAULT";
    }

    if (error != NULL) {
        proto_buf_append(out, cap, &len,
            ",\"data\":{\"error\":\"%s\",\"ambient_c\":null,"
            "\"humidity_pct\":null,\"mirror_c\":null,\"dew_point_c\":null,"
            "\"mirror_ambient_delta_c\":null,\"mirror_dew_margin_c\":null}}",
            error);
        return;
    }

    /* The dew point can be non-finite (e.g. humidity exactly zero); report it
     * as null rather than emitting `nan`/`inf`, which is not valid JSON. */
    char dew_str[32];
    if (isfinite(e.dew_point_c)) {
        snprintf(dew_str, sizeof(dew_str), "%.2f", e.dew_point_c);
    } else {
        snprintf(dew_str, sizeof(dew_str), "null");
    }

    proto_buf_append(out, cap, &len,
        ",\"data\":{\"error\":null,\"ambient_c\":%.2f,\"humidity_pct\":%.2f,"
        "\"mirror_c\":null,\"dew_point_c\":%s,"
        "\"mirror_ambient_delta_c\":null,\"mirror_dew_margin_c\":null}}",
        e.ambient_c, e.humidity_pct, dew_str);
}

/* ── state / vibration ─────────────────────────────────────── */

/* Emit the vibration response with every metric null.  Never uses `%c` for the
 * zone here, so a null zone cannot truncate the JSON (see #3). */
static void emit_vibration_nulls(char *out, size_t cap, size_t *len,
                                 bool enabled, const char *error_json) {
    proto_buf_append(out, cap, len,
        ",\"data\":{\"enabled\":%s,\"valid\":false,\"error\":%s,"
        "\"sample_rate_hz\":null,"
        "\"rms_x\":null,\"rms_y\":null,\"rms_z\":null,\"rms_total\":null,"
        "\"accel_peak_x\":null,\"accel_peak_y\":null,\"accel_peak_z\":null,\"accel_peak_total\":null,"
        "\"vel_rms_x\":null,\"vel_rms_y\":null,\"vel_rms_z\":null,\"vel_rms_total\":null,"
        "\"vel_peak_x\":null,\"vel_peak_y\":null,\"vel_peak_z\":null,\"vel_peak_total\":null,"
        "\"iso_zone\":null,\"processing\":\"since_last_read\"}}",
        enabled ? "true" : "false", error_json);
}

static void state_vibration(char *out, size_t cap, uint32_t id, const char *session) {
    bool enabled = vibration_get_enabled();
    bool healthy = vibration_get_healthy();
    VibrationReading r;
    vibration_take_reading(&r);

    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, "state", id, session, true);

    if (!enabled) {
        emit_vibration_nulls(out, cap, &len, false, "\"SENSOR_ABSENT\"");
        return;
    }
    if (!healthy) {
        /* Present at boot but currently failing reads. */
        emit_vibration_nulls(out, cap, &len, true, "\"SENSOR_FAULT\"");
        return;
    }
    if (!r.valid) {
        /* No samples accumulated since the previous read — not a fault. */
        emit_vibration_nulls(out, cap, &len, true, "null");
        return;
    }

    proto_buf_append(out, cap, &len,
        ",\"data\":{\"enabled\":true,\"valid\":true,\"error\":null,"
        "\"sample_rate_hz\":%d,"
        "\"rms_x\":%.3f,\"rms_y\":%.3f,\"rms_z\":%.3f,\"rms_total\":%.3f,"
        "\"accel_peak_x\":%.3f,\"accel_peak_y\":%.3f,\"accel_peak_z\":%.3f,\"accel_peak_total\":%.3f,"
        "\"vel_rms_x\":%.3f,\"vel_rms_y\":%.3f,\"vel_rms_z\":%.3f,\"vel_rms_total\":%.3f,"
        "\"vel_peak_x\":%.3f,\"vel_peak_y\":%.3f,\"vel_peak_z\":%.3f,\"vel_peak_total\":%.3f,"
        "\"iso_zone\":\"%c\","
        "\"processing\":\"since_last_read\"}}",
        VIB_SAMPLE_RATE_HZ,
        r.rms_x, r.rms_y, r.rms_z, r.rms_total,
        r.accel_peak_x, r.accel_peak_y, r.accel_peak_z, r.accel_peak_total,
        r.vel_rms_x, r.vel_rms_y, r.vel_rms_z, r.vel_rms_total,
        r.vel_peak_x, r.vel_peak_y, r.vel_peak_z, r.vel_peak_total,
        r.iso_zone);
}

/* ── state / orientation ──────────────────────────────────── */

static void state_orientation(char *out, size_t cap, uint32_t id, const char *session) {
    bool enabled = vibration_get_enabled();
    bool healthy = vibration_get_healthy();
    OrientationReading o;
    vibration_get_orientation(&o);

    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, "state", id, session, true);

    const char *error = NULL;
    if (!enabled) {
        error = "SENSOR_ABSENT";
    } else if (!healthy) {
        error = "SENSOR_FAULT";
    }

    if (error != NULL) {
        proto_buf_append(out, cap, &len,
            ",\"data\":{\"enabled\":%s,\"error\":\"%s\","
            "\"pitch_deg\":null,\"roll_deg\":null,"
            "\"ax_g\":null,\"ay_g\":null,\"az_g\":null}}",
            enabled ? "true" : "false", error);
        return;
    }

    proto_buf_append(out, cap, &len,
        ",\"data\":{\"enabled\":true,\"error\":null,"
        "\"pitch_deg\":%.2f,\"roll_deg\":%.2f,"
        "\"ax_g\":%.3f,\"ay_g\":%.3f,\"az_g\":%.3f}}",
        o.pitch_deg, o.roll_deg, o.ax_g, o.ay_g, o.az_g);
}

/* ── dispatcher ────────────────────────────────────────────── */

void proto_handle_state(const char *req, uint32_t id, char *out, size_t cap) {
    char target[PROTO_MAX_TARGET_LEN] = {0};
    char session[PROTO_MAX_SESSION_LEN] = {0};
    if (!proto_json_get_string_field(req, "target", target, sizeof(target)) ||
        !get_session(req, session, sizeof(session))) {
        size_t len = 0;
        out[0] = '\0';
        proto_env_begin(out, cap, &len, "state", id, session, false);
        proto_env_error(out, cap, &len, "INVALID_ARGUMENT", "string field too long");
        return;
    }

    if (strcmp(target, "device") == 0) {
        state_device(out, cap, id, session);
    } else if (strcmp(target, "environment") == 0) {
        state_environment(out, cap, id, session);
    } else if (strcmp(target, "vibration") == 0) {
        state_vibration(out, cap, id, session);
    } else if (strcmp(target, "orientation") == 0) {
        state_orientation(out, cap, id, session);
    } else {
        size_t len = 0;
        out[0] = '\0';
        proto_env_begin(out, cap, &len, "state", id, session, false);
        proto_env_error(out, cap, &len, "INVALID_ARGUMENT", "unknown target");
    }
}
