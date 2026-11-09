/* USB CDC — bounded, whole-frame transmission to the host. */
#include "usb_cdc.h"
#include "usb_cdc_internal.h"

#include <stdatomic.h>
#include "sdkconfig.h"
#include "tinyusb_cdc_acm.h"
#include "class/cdc/cdc_device.h"
#include "freertos/task.h"

#define USB_CDC_TX_TIMEOUT_MS 1000

/* Only the RX worker clears the FIFO, never the TinyUSB callback. */
static atomic_bool s_reset_pending;
/* Defensive latch for an unexpected short write, guarded by the TX mutex. */
static bool s_tx_failed;

void usb_cdc_tx_reset(void) {
    atomic_store(&s_reset_pending, true);
    if (usb_cdc_rx_sem != NULL) {
        xSemaphoreGive(usb_cdc_rx_sem);
    }
}

void usb_cdc_tx_service(void) {
    if (usb_cdc_tx_mutex == NULL || !atomic_load(&s_reset_pending)) {
        return;
    }
    xSemaphoreTake(usb_cdc_tx_mutex, portMAX_DELAY);
    if (atomic_exchange(&s_reset_pending, false)) {
        tud_cdc_n_write_clear(TINYUSB_CDC_ACM_0);
        s_tx_failed = false;
    }
    xSemaphoreGive(usb_cdc_tx_mutex);
}

bool usb_cdc_send(const uint8_t *data, size_t len) {
    if (data == NULL || len == 0 || len > CONFIG_TINYUSB_CDC_TX_BUFSIZE ||
        usb_cdc_tx_mutex == NULL) {
        return false;
    }
    if (xSemaphoreTake(usb_cdc_tx_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    bool sent = false;
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(USB_CDC_TX_TIMEOUT_MS);
    while (!s_tx_failed && !atomic_load(&s_reset_pending) &&
           tud_cdc_n_connected(TINYUSB_CDC_ACM_0)) {
        /* A single writer owns the mutex; TinyUSB only frees FIFO space.
         * Wait before queueing ANY bytes so timeout cannot truncate a frame. */
        if (tud_cdc_n_write_available(TINYUSB_CDC_ACM_0) >= len) {
            size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data, len);
            sent = (queued == len);
            s_tx_failed = (queued > 0 && !sent);
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
            break;
        }
        if ((TickType_t)(xTaskGetTickCount() - start) >= timeout) {
            break;
        }
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        vTaskDelay(1); /* at least one tick, including at CONFIG_FREERTOS_HZ=100 */
    }
    xSemaphoreGive(usb_cdc_tx_mutex);
    return sent;
}
