/* Proto — proto_action.c — ACTION: operations with a beginning and an end.
 *
 * device restart: normal reboot. device bootloader: reboot into the ROM
 * download mode so the host can reflash over USB-Serial-JTAG without the BOOT
 * button. A retried request with the same id replays the original result
 * instead of re-executing (see proto_action_lookup/record).
 */
#include "proto_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "usb_cdc.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

static bool get_session(const char *req, char *out, size_t cap) {
    return proto_json_get_string_field(req, "session", out, cap);
}

/* Delayed reboot: the handler returns and the response is flushed before this. */
static void restart_timer_cb(void *arg) {
    (void) arg;
    esp_restart();
}

/* Delayed reboot into ROM download mode (button-less flashing). */
static void bootloader_timer_cb(void *arg) {
    (void) arg;
    if (usb_cdc_prepare_download() != ESP_OK) {
        return;
    }
    REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

static void emit_ok(char *out, size_t cap, const char *type, uint32_t id,
                    const char *session, const char *data_fmt, ...)
    __attribute__((format(printf, 6, 7)));

static void emit_ok(char *out, size_t cap, const char *type, uint32_t id,
                    const char *session, const char *data_fmt, ...) {
    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, type, id, session, true);
    proto_buf_append(out, cap, &len, ",\"data\":{");

    va_list ap;
    va_start(ap, data_fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), data_fmt, ap);
    va_end(ap);
    proto_buf_append(out, cap, &len, "%s}}", buf);
}

static void emit_err(char *out, size_t cap, const char *type, uint32_t id,
                     const char *session, const char *code, const char *message) {
    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, cap, &len, type, id, session, false);
    proto_env_error(out, cap, &len, code, message);
}

/* ── device restart / bootloader ───────────────────────────── */

/* Schedule `cb` 200 ms out so the "accepted" response flushes before it runs.
 * Returns true when the reboot was scheduled (accepted), false on failure. */
static bool schedule_reboot(esp_timer_cb_t cb, const char *name,
                            uint32_t id, const char *session, char *out, size_t cap) {
    esp_timer_handle_t timer = NULL;
    esp_timer_create_args_t args = {
        .callback = cb,
        .name = name,
    };
    esp_err_t err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_once(timer, 200 * 1000);
        if (err != ESP_OK) {
            esp_timer_delete(timer);
        }
    }
    if (err == ESP_OK) {
        emit_ok(out, cap, "action", id, session,
                "\"op_id\":\"\",\"status\":\"accepted\"");
        return true;
    } else {
        emit_err(out, cap, "action", id, session, "INTERNAL",
                 "reboot unavailable");
        return false;
    }
}

static bool handle_restart(uint32_t id, const char *session, char *out, size_t cap) {
    return schedule_reboot(restart_timer_cb, "proto_restart", id, session, out, cap);
}

static bool handle_bootloader(uint32_t id, const char *session, char *out, size_t cap) {
    return schedule_reboot(bootloader_timer_cb, "proto_bootloader", id, session, out, cap);
}

/* ── dispatcher ────────────────────────────────────────────── */

void proto_handle_action(const char *req, uint32_t id, char *out, size_t cap) {
    char target[PROTO_MAX_TARGET_LEN] = {0};
    char action[PROTO_MAX_ACTION_LEN] = {0};
    char session[PROTO_MAX_SESSION_LEN] = {0};

    if (!proto_json_get_string_field(req, "target", target, sizeof(target)) ||
        !proto_json_get_string_field(req, "action", action, sizeof(action)) ||
        !get_session(req, session, sizeof(session))) {
        emit_err(out, cap, "action", id, session, "INVALID_ARGUMENT",
                 "string field too long");
        return;
    }

    /* Idempotency: a retried action id replays the original result instead of
     * re-executing (which would schedule a second reboot timer). */
    proto_seen_entry_t seen;
    if (proto_action_lookup(id, &seen)) {
        if (seen.ok) {
            emit_ok(out, cap, "action", id, session,
                    "\"op_id\":\"\",\"status\":\"accepted\"");
        } else {
            emit_err(out, cap, "action", id, session,
                     seen.error != NULL ? seen.error : "INTERNAL",
                     "action previously failed");
        }
        return;
    }

    if (strcmp(target, "device") == 0 && strcmp(action, "restart") == 0) {
        bool ok = handle_restart(id, session, out, cap);
        proto_action_record(id, ok, ok ? NULL : "INTERNAL");
    } else if (strcmp(target, "device") == 0 && strcmp(action, "bootloader") == 0) {
        bool ok = handle_bootloader(id, session, out, cap);
        proto_action_record(id, ok, ok ? NULL : "INTERNAL");
    } else {
        emit_err(out, cap, "action", id, session, "NOT_SUPPORTED",
                 "unknown action");
        proto_action_record(id, false, "NOT_SUPPORTED");
    }
}
