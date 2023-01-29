// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <freertos/FreeRTOS.h>
#include <driver/rtc_io.h>
#include <freertos/task.h>
#include "volf_sensors.h"
#include "ds18b20.h"

#define SENSOR_POWER_GPIO GPIO_NUM_25
#define SENSOR_DATA_GPIO GPIO_NUM_14

static void init_temperature_sensor() {
    LOGI("Initializing temperature sensor power.");
    gpio_pad_select_gpio(SENSOR_POWER_GPIO);
    gpio_set_direction(SENSOR_POWER_GPIO, GPIO_MODE_OUTPUT);

    LOGI("Powering up temperature sensor");
    gpio_set_level(SENSOR_POWER_GPIO, 1);
    ds18b20_init(SENSOR_DATA_GPIO);
}

static void power_off_temperature_sensor() {
    LOGI("Powering down temperature sensor");
    gpio_set_level(SENSOR_POWER_GPIO, 0);
}

void hibernate_temperature_sensor() {
    power_off_temperature_sensor();
}

float read_temperature() {
    float temp_c;
    float temp_f;

    init_temperature_sensor();
    temp_c = ds18b20_get_temp();
    if (temp_c == 0) {
        vTaskDelay(200 / portTICK_RATE_MS);
        temp_c = ds18b20_get_temp();
    }
    power_off_temperature_sensor();
    temp_f = (temp_c * 1.8f) + 32.0f;
    LOGI("Read temp of %.2f C and %.2f F", temp_c, temp_f);

    return temp_f;
}