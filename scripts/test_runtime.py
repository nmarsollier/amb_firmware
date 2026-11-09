#!/usr/bin/env python3
"""Host regression tests of the production C runtime with fake I2C/USB/RTOS.
Run: python3 scripts/test_runtime.py (requires a C11 compiler).
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMMON = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_TIMEOUT 5
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef void *SemaphoreHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((ms) / 10)
static unsigned tick, delays, takes, gives;
static void (*delay_hook)(void);
static TickType_t xTaskGetTickCount(void) { return tick; }
static void vTaskDelay(TickType_t n) {
    assert(n > 0); tick += n; delays++; if (delay_hook) delay_hook();
}
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static int xSemaphoreTake(SemaphoreHandle_t m, unsigned timeout) {
    (void)timeout; assert(m); takes++; return pdTRUE;
}
static int xSemaphoreGive(SemaphoreHandle_t m) { assert(m); gives++; return pdTRUE; }
static int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
        unsigned stack, void *arg, int priority, void *handle, int core) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    (void)handle; (void)core; return pdPASS;
}
static int64_t now;
static int64_t esp_timer_get_time(void) { return now; }
#define GPIO_NUM_8 8
#define GPIO_NUM_9 9
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_48 48
#define CONFIG_TINYUSB_CDC_TX_BUFSIZE 4096
#define TINYUSB_CDC_ACM_0 0
'''
VIBRATION = r'''
#include "vibration/vibration.c"
static esp_err_t boot_result, status_result, read_result, recovery_result;
static uint8_t fifo_count;
static unsigned recoveries;
esp_err_t accelerometer_init(void) { return boot_result; }
esp_err_t accelerometer_recover(void) { recoveries++; return recovery_result; }
esp_err_t accelerometer_fifo_entries(uint8_t *n) { *n = fifo_count; return status_result; }
esp_err_t accelerometer_read_fifo(AccelSample *buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = (AccelSample){0, 0, 1};
    return read_result;
}
static void poll_after(int64_t elapsed, uint8_t count) {
    now += elapsed; fifo_count = count; vibration_poll();
}
int main(void) {
    VibrationReading r;
    vibration_init();
    assert(!vibration_get_healthy());
    poll_after(10000, 2);
    assert(vibration_get_healthy());
    /* Fast empty polls after draining the FIFO are not sensor failures. */
    for (int i = 0; i < 10; i++) poll_after(1000, 0);
    assert(vibration_get_healthy() && recoveries == 0);
    /* Missing samples are a fault only after the elapsed-time deadline. */
    poll_after(250000, 0);
    assert(!vibration_get_healthy());
    vibration_take_reading(&r); assert(!r.valid);
    /* A recovered sample seeds the filters instead of creating a fake peak. */
    poll_after(10000, 2);
    vibration_take_reading(&r); assert(r.valid && r.rms_total == 0);
    /* Actual I2C failures count; empty-but-successful status resets the streak. */
    status_result = ESP_ERR_TIMEOUT;
    poll_after(10000, 0); assert(vibration_get_healthy());
    status_result = ESP_OK;
    poll_after(10000, 0); assert(vibration_get_healthy());
    status_result = ESP_ERR_TIMEOUT;
    poll_after(10000, 0); poll_after(10000, 0);
    assert(!vibration_get_healthy());
    recovery_result = ESP_ERR_TIMEOUT;
    poll_after(1000000, 0); assert(recoveries == 1);
    for (int i = 0; i < 20; i++) poll_after(10000, 0);
    assert(recoveries == 1); /* no retry storm */
    recovery_result = ESP_OK;
    poll_after(1000000, 0); assert(recoveries == 2);
    assert(!vibration_get_healthy()); /* configuration alone is not proof of life */
    status_result = ESP_OK;
    poll_after(10000, 2); assert(vibration_get_healthy());
    /* Boot-time absence also recovers without restarting the MCU. */
    boot_result = ESP_ERR_NOT_FOUND; vibration_init();
    assert(!vibration_get_enabled());
    poll_after(1000000, 0); assert(vibration_get_enabled());
    poll_after(10000, 2); assert(vibration_get_healthy());
    /* Partial FIFO reads cannot leak old statistics into the next interval. */
    read_result = ESP_ERR_TIMEOUT;
    poll_after(10000, 2); vibration_take_reading(&r); assert(!r.valid);
    read_result = ESP_OK;
    poll_after(10000, 2); vibration_take_reading(&r);
    assert(r.valid && r.rms_total == 0);
    puts("vibration: empty FIFO, stale data, I2C faults, retry limits, recovery passed");
}
'''
USB = r'''
static bool connected = true;
static unsigned available, queue_calls, clears, short_write;
static size_t last_len;
static bool tud_cdc_n_connected(int itf) { (void)itf; return connected; }
static unsigned tud_cdc_n_write_available(int itf) { (void)itf; return available; }
static bool tud_cdc_n_write_clear(int itf) { (void)itf; clears++; available = 4096; return true; }
static size_t tinyusb_cdcacm_write_queue(int itf, const uint8_t *data, size_t len) {
    (void)itf; (void)data; assert(available >= len);
    queue_calls++; last_len = len; available -= len;
    return short_write ? len - 1 : len;
}
static esp_err_t tinyusb_cdcacm_write_flush(int itf, unsigned timeout) {
    (void)itf; (void)timeout; return ESP_OK;
}
#include "usb_cdc/usb_cdc_tx.c"
SemaphoreHandle_t usb_cdc_rx_sem = (void *)1;
SemaphoreHandle_t usb_cdc_tx_mutex = (void *)2;
static void reconnect(void) { usb_cdc_tx_reset(); delay_hook = NULL; }
static void drain(void) { available = 4096; delay_hook = NULL; }
int main(void) {
    uint8_t frame[2052] = {0};
    /* A full FIFO times out with no partial frame and without poisoning TX. */
    assert(!usb_cdc_send(frame, sizeof(frame)));
    assert(tick == 100 && delays == 100 && queue_calls == 0);
    available = 4096;
    assert(usb_cdc_send(frame, sizeof(frame)) && last_len == 2052);
    /* Insufficient space waits for the whole frame, including its prefix. */
    available = 2051; delay_hook = drain;
    assert(usb_cdc_send(frame, sizeof(frame)));
    /* Reconnect callback never takes the TX lock, even during a blocked send. */
    unsigned before = takes;
    usb_cdc_tx_reset(); assert(takes == before);
    assert(!usb_cdc_send(frame, sizeof(frame)));
    usb_cdc_tx_service(); assert(clears == 1);
    assert(usb_cdc_send(frame, sizeof(frame)));
    available = 0; delay_hook = reconnect;
    assert(!usb_cdc_send(frame, sizeof(frame)));
    usb_cdc_tx_service(); assert(clears == 2);
    assert(usb_cdc_send(frame, sizeof(frame)));
    /* Disconnect/oversize writes are rejected without queueing bytes. */
    before = queue_calls; connected = false;
    assert(!usb_cdc_send(frame, sizeof(frame)) && queue_calls == before);
    connected = true;
    assert(!usb_cdc_send(frame, 4097) && queue_calls == before);
    /* Defensive handling if the underlying driver violates the reservation. */
    available = 4096; short_write = 1;
    assert(!usb_cdc_send(frame, sizeof(frame)));
    short_write = 0; available = 4096;
    assert(!usb_cdc_send(frame, sizeof(frame)));
    usb_cdc_tx_reset(); usb_cdc_tx_service();
    assert(usb_cdc_send(frame, sizeof(frame)));
    puts("USB: bounded timeout, whole frames, retry without reconnect, deferred reset passed");
}
'''

DRIVER = r'''
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef int i2c_port_t;
typedef int gpio_num_t;
#define I2C_NUM_1 1
#define I2C_ADDR_BIT_LEN_7 7
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
static uint8_t registers[256] = {[0] = 0xe5};
static unsigned buses, devices, resets, writes;
static esp_err_t probe_result, reset_result, attach_result;
static int corrupt_register = -1;
static esp_err_t i2c_master_transmit_receive(void *dev, const uint8_t *reg,
        size_t tx_len, uint8_t *out, size_t rx_len, int timeout) {
    assert(dev); (void)tx_len; (void)timeout;
    for (size_t i = 0; i < rx_len; i++) out[i] = registers[*reg + i];
    if (*reg == corrupt_register) *out ^= 1;
    return ESP_OK;
}
static esp_err_t i2c_master_transmit(void *dev, const uint8_t *data, size_t len, int timeout) {
    assert(dev && len == 2); (void)timeout;
    registers[data[0]] = data[1]; writes++; return ESP_OK;
}
static esp_err_t i2c_master_bus_add_device(void *bus, const i2c_device_config_t *cfg, void **dev) {
    assert(bus); (void)cfg;
    if (attach_result != ESP_OK) return attach_result;
    *dev = (void *)2; devices++; return ESP_OK;
}
static esp_err_t i2c_master_probe(void *bus, unsigned addr, int timeout) {
    assert(bus); (void)addr; (void)timeout; return probe_result;
}
static esp_err_t i2c_master_bus_reset(void *bus) { assert(bus); resets++; return reset_result; }
#include "accelerometer/accelerometer_init.c"
#include "accelerometer/accelerometer_fifo.c"
#include "accelerometer/accelerometer_read.c"
esp_err_t i2c_bus_create(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, void **bus) {
    (void)port; (void)sda; (void)scl; buses++; *bus = (void *)1; return ESP_OK;
}
int main(void) {
    /* Device allocation can be retried without allocating another bus. */
    attach_result = ESP_ERR_TIMEOUT;
    assert(accelerometer_init() == ESP_ERR_TIMEOUT && buses == 1);
    attach_result = ESP_OK; probe_result = ESP_ERR_NOT_FOUND;
    assert(accelerometer_recover() == ESP_ERR_NOT_FOUND);
    assert(buses == 1 && devices == 1);
    AccelSample sample;
    uint8_t count;
    assert(accelerometer_read_sample(&sample) == ESP_ERR_INVALID_STATE);
    assert(accelerometer_fifo_entries(&count) == ESP_ERR_INVALID_STATE);
    assert(accelerometer_read_fifo(&sample, 1) == ESP_ERR_INVALID_STATE);
    /* Recovery restores lost configuration on the existing device/bus. */
    probe_result = ESP_OK;
    assert(accelerometer_recover() == ESP_OK);
    assert(accelerometer_is_present() && registers[0x2d] == 8);
    assert(registers[0x38] == 0x80 && registers[0x2c] == 0x0a);
    registers[0x2d] = 0; registers[0x38] = 0;
    assert(accelerometer_recover() == ESP_OK);
    assert(registers[0x2d] == 8 && registers[0x38] == 0x80);
    assert(buses == 1 && devices == 1 && resets == 2);
    /* Failed reset or mismatched configuration must not declare recovery. */
    unsigned before = writes;
    reset_result = ESP_ERR_TIMEOUT;
    assert(accelerometer_recover() == ESP_ERR_TIMEOUT && writes == before);
    reset_result = ESP_OK; corrupt_register = 0x2c;
    assert(accelerometer_recover() == ESP_ERR_INVALID_STATE);
    assert(!accelerometer_is_present());
    corrupt_register = -1;
    assert(accelerometer_recover() == ESP_OK);
    assert(buses == 1 && devices == 1);
    puts("ADXL345: allocation retry, bus reset, register restoration and verification passed");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='amb-runtime-') as directory:
        tmp = Path(directory)
        for header in ('esp_err.h', 'esp_log.h', 'esp_timer.h', 'sdkconfig.h',
                       'freertos/FreeRTOS.h', 'freertos/semphr.h', 'freertos/task.h',
                       'driver/gpio.h', 'driver/i2c_master.h', 'tinyusb_cdc_acm.h', 'class/cdc/cdc_device.h'):
            target = tmp / header
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('#pragma once\n')
        (tmp / 'fake.h').write_text(COMMON)
        for name, source in [('vibration', VIBRATION), ('usb', USB), ('driver', DRIVER)]:
            test = tmp / f'{name}.c'
            test.write_text(source)
            binary = tmp / f'test_{name}'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                            '-fsanitize=' + os.environ.get('SANITIZERS', 'undefined'),
                            '-include', str(tmp / 'fake.h'),
                            '-I', str(tmp), '-I', str(ROOT / 'main'),
                            '-I', str(ROOT / 'main/accelerometer'),
                            '-I', str(ROOT / 'main/config'), '-I', str(ROOT / 'main/i2c_bus'),
                            str(test), '-lm',
                            '-o', str(binary)], check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=30)

if __name__ == '__main__':
    main()
