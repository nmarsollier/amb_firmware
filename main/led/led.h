#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * LED states for the status indicator on GPIO 12.
 *
 * NORMAL — dim brightness (~12 %), both sensors reading normally.
 * ERROR  — beacon (blink-blink, slow fade-out), a sensor is unreadable.
 *
 * All state decisions are made inside led_update(), which is the single public
 * entry point for LED state changes. No other module should call
 * led_set_state() directly.
 */
typedef enum {
    LED_STATE_NORMAL,
    LED_STATE_ERROR
} LedState;

/* Initialise LEDC PWM on GPIO 12 and start in NORMAL (dim). */
void led_init(void);

/*
 * Periodic update, called every ~200 ms from the runtime loop.
 *
 * Inspects the sensors and transitions the LED to the correct state. This is
 * the ONLY public function that changes LED state.
 */
void led_update(void);
