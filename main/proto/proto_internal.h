/* proto_internal.h — internal API shared among the proto module files. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Protocol identity ────────────────────────────────────── */

#define PROTO_VERSION       2
#define PROTO_SCHEMA        "amb"
#define PROTO_ROLE          "accessories"

#define PROTO_MAX_FRAME     2048
#define PROTO_MAX_TYPE_LEN  16
#define PROTO_MAX_TARGET_LEN 16
#define PROTO_MAX_SESSION_LEN 64
#define PROTO_MAX_ACTION_LEN 24

/* ── Session / identity ───────────────────────────────────── */

void proto_session_init(void);
uint32_t proto_boot_id(void);
void proto_boot_id_str(char *out, size_t cap);
uint32_t proto_config_rev(void);
void proto_config_rev_bump(void);

/* ── Idempotency (ring keyed by request id) ───────────────── */

typedef struct { uint32_t id; bool ok; const char *error; } proto_seen_entry_t;
bool proto_action_lookup(uint32_t id, proto_seen_entry_t *out);
void proto_action_record(uint32_t id, bool ok, const char *error);
void proto_session_reset(void);

/* ── JSON parser ──────────────────────────────────────────── */

bool proto_json_validate(const char *json);
bool proto_json_has_key(const char *json, const char *key);
bool proto_json_get_string(const char *json, const char *key, char *out, size_t cap);
bool proto_json_get_float(const char *json, const char *key, float *out);
bool proto_json_get_int(const char *json, const char *key, int *out);
bool proto_json_get_u32(const char *json, const char *key, uint32_t *out);
bool proto_json_get_int64(const char *json, const char *key, int64_t *out);
bool proto_json_get_bool(const char *json, const char *key, bool *out);
bool proto_json_get_object(const char *json, const char *key, char *out, size_t cap);
/* Escape a string for embedding in JSON output; returns false if truncated. */
bool proto_json_escape(const char *src, char *out, size_t cap);
bool proto_buf_append(char *buf, size_t cap, size_t *len, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* ── Framing ──────────────────────────────────────────────── */

void proto_feed(const uint8_t *data, size_t len);
void proto_frame_reset(void);
void proto_send_body(const char *body);
void proto_dispatch(const char *body);

/* ── Response envelope helpers ────────────────────────────── */

/*
 * Write the response prefix: {"proto":2,"type":...,"id":...,"session":...,
 * "boot_id":...,"ok":true|false. The caller then appends `,"data":{...}` and
 * closes with `}`, or calls proto_env_error().
 */
static inline void proto_env_begin(char *out, size_t cap, size_t *len,
                                   const char *type, uint32_t id,
                                   const char *session, bool ok) {
    char boot[9];
    char type_esc[PROTO_MAX_TYPE_LEN * 6 + 1];
    char session_esc[PROTO_MAX_SESSION_LEN * 6 + 1];
    proto_boot_id_str(boot, sizeof(boot));
    proto_json_escape(type, type_esc, sizeof(type_esc));
    proto_json_escape(session, session_esc, sizeof(session_esc));
    proto_buf_append(out, cap, len,
        "{\"proto\":%d,\"type\":\"%s\",\"id\":%lu,\"session\":\"%s\","
        "\"boot_id\":\"%s\",\"ok\":%s",
        PROTO_VERSION, type_esc, (unsigned long) id, session_esc, boot,
        ok ? "true" : "false");
}

/* Append the error object and close the envelope. */
static inline void proto_env_error(char *out, size_t cap, size_t *len,
                                   const char *code, const char *message) {
    proto_buf_append(out, cap, len,
        ",\"error\":{\"code\":\"%s\",\"message\":\"%s\",\"field\":null}}",
        code, message);
}

/* ── Handlers (one per message family) ────────────────────── */

void proto_handle_capabilities(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_state(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_action(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_config(const char *req, uint32_t id, char *out, size_t cap);
void proto_handle_control(const char *req, uint32_t id, char *out, size_t cap);
