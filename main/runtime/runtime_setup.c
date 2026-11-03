/* Runtime - runtime_setup.c
 *
 * Purpose: initialize the subsystems needed before the runtime loop starts.
 */
#include "runtime.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "environment.h"
#include "led.h"
#include "tilt.h"
#include "usb_cdc.h"

static const char *TAG = "RUNTIME_SETUP";

/*
 * Business use case: prepare the temperature/vibration module for operation.
 *
 * Objective: bring the core services, the environment and vibration sensors and
 * the USB transport online so the module starts in a usable state.
 */
void setup_init(void) {
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    led_init();

    /* Tilt (ADXL345) first, before the SHT31: the SHT31's clock stretching can
     * leave SCL held and corrupt the ADXL345's configuration. */
    tilt_init();
    environment_init();

#if 0  /* TEMPORARY diagnostic: USB disabled to keep the console visible */
    /* USB CDC-ACM transport — the protocol is wired to it by proto_init(). */
    esp_err_t cdc_result = usb_cdc_init();
    if (cdc_result != ESP_OK) {
        ESP_LOGW(TAG, "USB CDC init failed: %s", esp_err_to_name(cdc_result));
    }
#endif

    ESP_LOGI(TAG, "Board ready");
}
