/* LED — led_set_state.c
 *
 * Purpose: apply LED state transitions (green / red beacon).
 *
 * This is an internal helper called exclusively from led_update(). Other
 * modules must never call this directly — led_update() is the single public
 * entry point that decides the correct state.
 */
#include "led_internal.h"

/* ── Shared state ──────────────────────────────────────────── */

LedState led_current_state = LED_STATE_NORMAL;

/* ── Internal API ──────────────────────────────────────────── */

void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t rgb[3] = { r, g, b };   /* LED byte order is RGB (this board's clone) */
    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    rmt_transmit(led_tx_chan, led_encoder, rgb, sizeof(rgb), &tx_conf);
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
