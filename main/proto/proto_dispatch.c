/* Proto — proto_dispatch.c — route a request body to its family handler. */
#include "proto_internal.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "PROTO_DISPATCH";

static void send_error(const char *type, uint32_t id, const char *session,
                       const char *code, const char *message) {
    char out[PROTO_MAX_FRAME];
    size_t len = 0;
    out[0] = '\0';
    proto_env_begin(out, sizeof(out), &len, type, id, session, false);
    proto_env_error(out, sizeof(out), &len, code, message);
    proto_send_body(out);
}

void proto_dispatch(const char *body) {
    char type[PROTO_MAX_TYPE_LEN] = {0};
    char session[PROTO_MAX_SESSION_LEN] = {0};
    uint32_t id = 0;

    char out[PROTO_MAX_FRAME];
    out[0] = '\0';   /* ensure a response is always produced */

    /* Reject a structurally invalid request (bad JSON, duplicate top-level
     * keys, trailing bytes) before it can reach any handler. */
    if (!proto_json_validate(body)) {
        ESP_LOGW(TAG, "rejecting invalid request body");
        send_error("error", 0, "", "INVALID_ARGUMENT", "invalid request");
        return;
    }

    /* Enforce the contract: `proto` must be 2. */
    uint32_t proto_ver = 0;
    if (!proto_json_get_u32(body, "proto", &proto_ver) || proto_ver != PROTO_VERSION) {
        ESP_LOGW(TAG, "rejecting unsupported proto");
        send_error("error", 0, "", "INVALID_ARGUMENT", "unsupported proto");
        return;
    }

    proto_json_get_string(body, "type", type, sizeof(type));
    proto_json_get_string(body, "session", session, sizeof(session));

    {
        char target[PROTO_MAX_TARGET_LEN] = {0};
        proto_json_get_string(body, "target", target, sizeof(target));
    }

    /* `id` is optional; when present it must be a valid unsigned integer
     * (rejects negatives, fractions and overflow). */
    if (proto_json_has_key(body, "id") && !proto_json_get_u32(body, "id", &id)) {
        ESP_LOGW(TAG, "rejecting invalid id");
        send_error("error", 0, session, "INVALID_ARGUMENT", "invalid id");
        return;
    }

    /* `id` is a non-zero uint32 — 0 is reserved for events. */
    if (id == 0) {
        ESP_LOGW(TAG, "rejecting missing/zero id");
        send_error("error", 0, session, "INVALID_ARGUMENT", "id must be non-zero");
        return;
    }

    if (strcmp(type, "capabilities") == 0) {
        proto_handle_capabilities(body, id, out, sizeof(out));
    } else if (strcmp(type, "state") == 0) {
        proto_handle_state(body, id, out, sizeof(out));
    } else if (strcmp(type, "config") == 0) {
        proto_handle_config(body, id, out, sizeof(out));
    } else if (strcmp(type, "control") == 0) {
        proto_handle_control(body, id, out, sizeof(out));
    } else if (strcmp(type, "action") == 0) {
        proto_handle_action(body, id, out, sizeof(out));
    } else {
        const char *name = type[0] != '\0' ? type : "unknown";
        ESP_LOGW(TAG, "unknown request type '%s'", name);
        send_error(name, id, session, "NOT_SUPPORTED", "unknown type");
        return;
    }

    /* Defensive: a handler must always have written a response. */
    if (out[0] == '\0') {
        ESP_LOGE(TAG, "handler produced no response for type '%s'", type);
        send_error(type[0] != '\0' ? type : "error", id, session,
                   "INTERNAL", "no response");
        return;
    }

    proto_send_body(out);
}
