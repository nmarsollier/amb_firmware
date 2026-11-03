/* Proto — proto_state.c — STATE: coherent snapshots per target.
 *
 * Routed by `target`: device, environment (ambient only), vibration.
 */
#include "proto_internal.h"

#include <string.h>

#include "esp_system.h"

#include "accelerometer.h"
#include "environment.h"
#include "sht31.h"
#include "tilt.h"

static void get_session(const char *req, char *out, size_t cap) {
    proto_json_get_string(req, "session", out, cap);
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
    bool sensor_fault = !accelerometer_is_present() || !sht31_is_present();

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
    bool ambient_valid = environment_ambient_valid();

    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, "state", id, session, true);

    if (!ambient_valid) {
        proto_buf_append(out, cap, &len,
            ",\"data\":{\"error\":\"SENSOR_ABSENT\",\"ambient_c\":null,"
            "\"humidity_pct\":null,\"mirror_c\":null,\"dew_point_c\":null,"
            "\"mirror_ambient_delta_c\":null,\"mirror_dew_margin_c\":null}}");
        return;
    }

    proto_buf_append(out, cap, &len,
        ",\"data\":{\"error\":null,\"ambient_c\":%.2f,\"humidity_pct\":%.2f,"
        "\"mirror_c\":null,\"dew_point_c\":%.2f,"
        "\"mirror_ambient_delta_c\":null,\"mirror_dew_margin_c\":null}}",
        environment_get_ambient_c(), environment_get_humidity_pct(),
        environment_get_dew_point_c());
}

/* ── state / vibration ─────────────────────────────────────── */

static void state_tilt(char *out, size_t cap, uint32_t id, const char *session) {
    bool enabled = tilt_get_enabled();
    TiltReading r;
    tilt_get_reading(&r);

    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, "state", id, session, true);

    if (!enabled) {
        proto_buf_append(out, cap, &len,
            ",\"data\":{\"enabled\":false,\"valid\":false,\"error\":\"SENSOR_ABSENT\","
            "\"pitch_deg\":null,\"roll_deg\":null}}");
    } else {
        proto_buf_append(out, cap, &len,
            ",\"data\":{\"enabled\":true,\"valid\":%s,\"error\":null,"
            "\"pitch_deg\":%.2f,\"roll_deg\":%.2f}}",
            r.valid ? "true" : "false",
            r.pitch_deg, r.roll_deg);
    }
}

/* ── dispatcher ────────────────────────────────────────────── */

void proto_handle_state(const char *req, uint32_t id, char *out, size_t cap) {
    char target[PROTO_MAX_TARGET_LEN] = {0};
    char session[PROTO_MAX_SESSION_LEN] = {0};
    proto_json_get_string(req, "target", target, sizeof(target));
    get_session(req, session, sizeof(session));

    if (strcmp(target, "device") == 0) {
        state_device(out, cap, id, session);
    } else if (strcmp(target, "environment") == 0) {
        state_environment(out, cap, id, session);
    } else if (strcmp(target, "tilt") == 0) {
        state_tilt(out, cap, id, session);
    } else {
        size_t len = 0;
        out[0] = '\0';
        proto_env_begin(out, cap, &len, "state", id, session, false);
        proto_env_error(out, cap, &len, "INVALID_ARGUMENT", "unknown target");
    }
}
