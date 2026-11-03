/* Runtime - runtime_loop.c
 *
 * Purpose: run the periodic board cycle for the buttons and status LEDs.
 */
#include "runtime.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "environment.h"
#include "led.h"
#include "tilt.h"

#define MAIN_LOOP_PERIOD_MS     200
#define MAIN_TASK_STACK_SIZE    4096
#define MAIN_TASK_PRIORITY      5

static const char *TAG = "MAIN_LOOP";

/*
 * Business use case: orchestrate the board's periodic operational cycle.
 *
 * Objective: keep the environment and vibration sensors refreshed at a stable
 * cadence.
 */
static void main_loop_task(void *arg) {
    (void) arg;
    const TickType_t period = pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS);

    while (true) {
        environment_update();
        tilt_update();
        led_update();

        /* TEMPORARY diagnostic: log the tilt reading, simulating a protocol
         * state/tilt request. Remove when USB returns. */
        TiltReading tr;
        tilt_get_reading(&tr);
        if (tr.valid) {
            ESP_LOGI(TAG, "tilt pitch=%.2f roll=%.2f | g=(%.3f %.3f %.3f)",
                     tr.pitch_deg, tr.roll_deg, tr.ax_g, tr.ay_g, tr.az_g);
        }

        vTaskDelay(period);
    }
}

/*
 * Business use case: start the main runtime in the background.
 *
 * Objective: delegate continuous operation to a dedicated FreeRTOS task.
 */
void setup_runtime_start(void) {
    xTaskCreatePinnedToCore(
        main_loop_task,
        "main_loop",
        MAIN_TASK_STACK_SIZE,
        NULL,
        MAIN_TASK_PRIORITY,
        NULL,
        1);  /* CPU 1 — keep sensor I/O (ADC/I2C) off CPU 0 so a stuck read
              * never blocks the RX task (CPU 0, prio 19). */
}
