/* Main - main.c
 *
 * Purpose: initialize the runtime subsystems, wire the serial protocol, then
 * start the main loop.
 */
#include "esp_log.h"
#include "runtime.h"

#if CONFIG_TINYUSB_CDC_ENABLED
#include "proto.h"
#endif

static const char *TAG = "MAIN";

void app_main(void) {
    setup_init();

#if CONFIG_TINYUSB_CDC_ENABLED
    proto_init();
#endif

    setup_runtime_start();

    ESP_LOGI(TAG, "Started");
}
