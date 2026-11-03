/* LED — led_update.c
 *
 * Purpose: periodic LED state decision — the single public entry point for all
 * LED state changes.
 *
 * Called from the runtime loop every ~200 ms. Inspects the two sensors to pick
 * the correct LED state:
 *
 *   1. A sensor is unreadable -> beacon (blink-blink-fade-pause)
 *   2. Otherwise              -> dim (normal, both sensors healthy)
 *
 * No other module calls led_set_state() directly. This keeps all LED logic
 * cohesive in one place.
 */
#include "led_internal.h"

#include "environment.h"
#include "vibration.h"

void led_update(void) {
    bool sensor_fault = !environment_ambient_valid() || !vibration_get_healthy();

    led_set_state(sensor_fault ? LED_STATE_ERROR : LED_STATE_NORMAL);
}
