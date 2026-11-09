/* USB CDC — usb_cdc_init.c — TinyUSB CDC-ACM device installation. */
#include "usb_cdc.h"
#include "usb_cdc_internal.h"

#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/task.h"

#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

static const char *TAG = "USB_CDC_INIT";

static usb_cdc_conn_fn s_conn_fn;
static void *s_conn_arg;

/* CDC_EVENT_RX callback — runs in the TinyUSB task context.  It only posts
 * the semaphore so the RX task wakes and drains the FIFO; no CDC API is called
 * from here (flush/read from a callback is explicitly discouraged). */
static void usb_cdc_rx_event(int itf, cdcacm_event_t *event) {
    (void) itf;
    (void) event;
    if (usb_cdc_rx_sem != NULL) {
        xSemaphoreGive(usb_cdc_rx_sem);
    }
}

/* CDC_EVENT_LINE_STATE_CHANGED — the host opened or closed the serial port. */
static void usb_cdc_line_state(int itf, cdcacm_event_t *event) {
    (void) itf;
    bool dtr = event->line_state_changed_data.dtr;
    if (dtr) {
        /* Ask the RX worker to clear the previous session's queued bytes.
         * Never wait for its TX mutex from the TinyUSB task. */
        usb_cdc_tx_reset();
    }
    if (s_conn_fn != NULL) {
        s_conn_fn(dtr, s_conn_arg);
    }
}

void usb_cdc_set_conn_callback(usb_cdc_conn_fn cb, void *arg) {
    s_conn_fn = cb;
    s_conn_arg = arg;
}

esp_err_t usb_cdc_init(void) {
    /* Descriptors are left NULL so esp_tinyusb supplies the Kconfig-driven
     * CDC device/configuration/string descriptors (CDC class + custom
     * VID/PID/strings from sdkconfig.defaults). */
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    /* Keep USB on core 0, above the RX worker (19). Sensor I/O runs on core 1. */
    cfg.task.xCoreID = 0;
    cfg.task.priority = 20;

    usb_cdc_rx_sem = xSemaphoreCreateBinary();
    usb_cdc_tx_mutex = xSemaphoreCreateMutex();
    if (usb_cdc_rx_sem == NULL || usb_cdc_tx_mutex == NULL) {
        ESP_LOGE(TAG, "failed to create sync primitives");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    /* ESP_ERR_INVALID_STATE means the driver is already installed — just init
     * CDC below. */

    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = usb_cdc_rx_event,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = usb_cdc_line_state,
        .callback_line_coding_changed = NULL,
    };
    err = tinyusb_cdcacm_init(&acm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init: %s", esp_err_to_name(err));
        return err;
    }

    err = usb_cdc_rx_task_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_cdc_rx_task_start: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "CDC-ACM serial device started");
    return ESP_OK;
}

esp_err_t usb_cdc_prepare_download(void) {
    esp_err_t err = tinyusb_driver_uninstall();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to stop TinyUSB: %s", esp_err_to_name(err));
        return err;
    }
    // Let the host observe a disconnect before assigning the shared PHY to ROM USB.
    vTaskDelay(pdMS_TO_TICKS(100));
    usb_phy_config_t config = {
        .controller = USB_PHY_CTRL_SERIAL_JTAG,
        .target = USB_PHY_TARGET_INT,
    };
    usb_phy_handle_t phy = NULL;
    err = usb_new_phy(&config, &phy);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to prepare download USB: %s", esp_err_to_name(err));
    }
    // Keep the PHY active across the CPU reset; ROM will take ownership.
    return err;
}
