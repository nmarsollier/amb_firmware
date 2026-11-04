/* Proto — proto_config.c — CONFIG: mock, read-only for now.
 *
 * Real persistent configuration is not ported yet, so `get` returns defaults
 * and `set` is rejected with NOT_SUPPORTED.
 */
#include "proto_internal.h"

#include <string.h>

void proto_handle_config(const char *req, uint32_t id, char *out, size_t cap) {
    char op[PROTO_MAX_ACTION_LEN] = {0};
    char session[PROTO_MAX_SESSION_LEN] = {0};
    if (!proto_json_get_string_field(req, "op", op, sizeof(op)) ||
        !proto_json_get_string_field(req, "session", session, sizeof(session))) {
        size_t len = 0;
        out[0] = '\0';
        proto_env_begin(out, cap, &len, "config", id, session, false);
        proto_env_error(out, cap, &len, "INVALID_ARGUMENT", "string field too long");
        return;
    }

    size_t len = 0;
    out[0] = '\0';

    if (strcmp(op, "get") == 0) {
        proto_env_begin(out, cap, &len, "config", id, session, true);
        proto_buf_append(out, cap, &len,
            ",\"data\":{\"config_rev\":%lu,\"saved_rev\":%lu,"
            "\"config\":{}}}",
            (unsigned long) proto_config_rev(),
            (unsigned long) proto_config_rev());
    } else if (strcmp(op, "set") == 0) {
        proto_env_begin(out, cap, &len, "config", id, session, false);
        proto_env_error(out, cap, &len, "NOT_SUPPORTED",
                        "config set not implemented yet");
    } else {
        proto_env_begin(out, cap, &len, "config", id, session, false);
        proto_env_error(out, cap, &len, "INVALID_ARGUMENT",
                        "op must be get or set");
    }
}
