/* Proto — proto_capabilities.c — CAPABILITIES: unified handshake + device-specific capabilities.
 *
 * The response uses the shared identification header (device_type, device_id,
 * name, firmware_version, proto, boot_id) so a client can tell the temperature
 * module apart from the focuser and fan controllers with one parser. The
 * `capabilities` object then holds the sensor capabilities.
 */
#include "proto_internal.h"

#include "config.h"

void proto_handle_capabilities(const char *req, uint32_t id, char *out, size_t cap) {
    (void) req;

    char boot[9];
    proto_boot_id_str(boot, sizeof(boot));

    size_t len = 0;
    out[0] = '\0';

    proto_buf_append(out, cap, &len,
        "{\"type\":\"capabilities\",\"id\":%lu,\"ok\":true,"
        "\"boot_id\":\"%s\",\"proto\":%d,"
        "\"device_type\":\"accessories\",\"device_id\":\"temp\","
        "\"name\":\"%s\",\"firmware_version\":\"0.1.0\","
        "\"capabilities\":{"
        "\"sensors\":{\"ambient\":\"SHT31\",\"vibration\":\"ADXL345\"}"
        "}}",
        (unsigned long) id, boot, PROTO_VERSION, DEVICE_NAME);
}
