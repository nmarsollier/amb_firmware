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

#include "esp_system.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

static void get_session(const char *req, char *out, size_t cap) {
    proto_json_get_string(req, "session", out, cap);
}

/* Delayed reboot: the handler returns and the response is flushed before this. */
static void restart_timer_cb(void *arg) {
    (void) arg;
    esp_restart();
}

/* Delayed reboot into ROM download mode (button-less flashing). */
static void bootloader_timer_cb(void *arg) {
    (void) arg;
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
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

/* Schedule `cb` 200 ms out so the "accepted" response flushes before it runs. */
static void schedule_reboot(esp_timer_cb_t cb, const char *name,
                            uint32_t id, const char *session, char *out, size_t cap) {
    esp_timer_handle_t timer = NULL;
    esp_timer_create_args_t args = {
        .callback = cb,
        .name = name,
    };
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, 200 * 1000);
        emit_ok(out, cap, "action", id, session,
                "\"op_id\":\"\",\"status\":\"accepted\"");
    } else {
        emit_err(out, cap, "action", id, session, "INTERNAL",
                 "reboot unavailable");
    }
}

static void handle_restart(uint32_t id, const char *session, char *out, size_t cap) {
    schedule_reboot(restart_timer_cb, "proto_restart", id, session, out, cap);
}

static void handle_bootloader(uint32_t id, const char *session, char *out, size_t cap) {
    schedule_reboot(bootloader_timer_cb, "proto_bootloader", id, session, out, cap);
}

/* ── dispatcher ────────────────────────────────────────────── */

void proto_handle_action(const char *req, uint32_t id, char *out, size_t cap) {
    char target[PROTO_MAX_TARGET_LEN] = {0};
    char action[PROTO_MAX_ACTION_LEN] = {0};
    char session[PROTO_MAX_SESSION_LEN] = {0};

    proto_json_get_string(req, "target", target, sizeof(target));
    proto_json_get_string(req, "action", action, sizeof(action));
    get_session(req, session, sizeof(session));

    if (strcmp(target, "device") == 0 && strcmp(action, "restart") == 0) {
        handle_restart(id, session, out, cap);
    } else if (strcmp(target, "device") == 0 && strcmp(action, "bootloader") == 0) {
        handle_bootloader(id, session, out, cap);
    } else {
        emit_err(out, cap, "action", id, session, "NOT_SUPPORTED",
                 "unknown action");
    }
}
