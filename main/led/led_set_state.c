/* LED — led_set_state.c
 *
 * Purpose: apply LED state transitions (green / red beacon).
 *
 * This is an internal helper called exclusively from led_update(). Other
 * modules must never call this directly — led_update() is the single public
 * entry point that decides the correct state.
 */
#include "led_internal.h"

#include "esp_err.h"
#include "esp_log.h"

/* ── Shared state ──────────────────────────────────────────── */

LedState led_current_state = LED_STATE_NORMAL;

static const char *TAG = "LED";

/* Persistent transmit buffer: rmt_transmit() may encode the data after the call
 * returns, so the buffer must outlive led_set_color() — a stack array would be
 * dead by then. */
static uint8_t s_rgb[3];

/* ── Internal API ──────────────────────────────────────────── */

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    s_rgb[0] = r;   /* LED byte order is RGB (this board's clone) */
    s_rgb[1] = g;
    s_rgb[2] = b;

    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    esp_err_t err = rmt_transmit(led_tx_chan, led_encoder, s_rgb, sizeof(s_rgb), &tx_conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
    }
}

void led_set_state(LedState state) {
    if (state == led_current_state) {
        return;
    }

    led_current_state = state;

    /* Stop the animation (if any) before applying the new state. */
    led_beacon_stop();

    switch (state) {
    case LED_STATE_NORMAL:
        led_set_color(LED_NORMAL_R, LED_NORMAL_G, LED_NORMAL_B);
        break;
    case LED_STATE_ERROR:
        led_beacon_start();
        break;
    }
}
