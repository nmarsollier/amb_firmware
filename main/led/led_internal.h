#pragma once

#include "led.h"
#include "config.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ── WS2812 RMT configuration ─────────────────────────────── */

#define LED_RMT_RES_HZ      10000000  /* 10 MHz, 0.1 µs/tick */

/* WS2812 bit timing (in 0.1 µs ticks). */
#define LED_WS2812_T0H      4         /* 0.4 µs high */
#define LED_WS2812_T0L      9         /* 0.9 µs low  */
#define LED_WS2812_T1H      8         /* 0.8 µs high */
#define LED_WS2812_T1L      4         /* 0.4 µs low  */

/* ── Status colors (RGB) — always green ───────────────────── */

#define LED_NORMAL_R        0
#define LED_NORMAL_G        64
#define LED_NORMAL_B        0

#define LED_ERROR_R         0
#define LED_ERROR_G         64
#define LED_ERROR_B         0

/* ── Shared state ──────────────────────────────────────────── */

extern LedState led_current_state;
extern rmt_channel_handle_t led_tx_chan;
extern rmt_encoder_handle_t led_encoder;

/* Serialises led_set_color() so the persistent RMT buffer is never overwritten
 * while a transmission is still encoding from it. */
extern SemaphoreHandle_t led_mutex;

/* ── Internal helpers ─────────────────────────────────────── */

/* Set the WS2812 color (RGB). */
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

/* Start / stop the beacon animation (red blink, error). */
void led_beacon_start(void);
void led_beacon_stop(void);

/* Internal: apply a state transition (green / red beacon). Only called from
 * led_update() — not part of the public API. */
void led_set_state(LedState state);
