// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <freertos/FreeRTOS.h>
#include <driver/adc.h>
#include <driver/rtc_io.h>
#include <freertos/task.h>
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "volf_sensors.h"

#define SENSOR_ADC_CHANNEL ADC1_CHANNEL_7
#define SENSOR_ADC_ATTENUATION ADC_ATTEN_DB_11
#define SENSOR_ADB_WIDTH_BIT ADC_WIDTH_BIT_12
#define SENSOR_ADC_UNIT ADC_UNIT_1
#define SENSOR_POWER_GPIO GPIO_NUM_25
#define SENSOR_DATA_GPIO GPIO_NUM_35

static esp_adc_cal_characteristics_t *adc_chars = NULL;

static void power_off_moisture_sensor() {
    LOGI("Powering down sensor");
    gpio_set_level(SENSOR_POWER_GPIO, 0);
}

static void init_moisture_sensor() {
    LOGI("Initializing ADC for reading sensor data.");
    // Characterize ADC at particular attenuation
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));

    esp_adc_cal_characterize(SENSOR_ADC_UNIT, SENSOR_ADC_ATTENUATION, SENSOR_ADB_WIDTH_BIT, 0,
                             adc_chars);

    // Configure ADC channel
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SENSOR_ADC_CHANNEL, SENSOR_ADC_ATTENUATION);

    LOGI("Initializing sensor power.");
    gpio_pad_select_gpio(SENSOR_POWER_GPIO);
    gpio_set_direction(SENSOR_POWER_GPIO, GPIO_MODE_OUTPUT);

    LOGI("Powering up sensor");
    gpio_set_level(SENSOR_POWER_GPIO, 1);
    vTaskDelay(1000 / portTICK_RATE_MS);
}

static void shutdown_sensor() {
    power_off_moisture_sensor();
    if (adc_chars != NULL) {
        free(adc_chars);
        adc_chars = NULL;
    }
}


void hibernate_moisture_sensor() {
    shutdown_sensor();
    rtc_gpio_isolate(SENSOR_DATA_GPIO);
}

uint32_t read_soil_moisture_voltage() {
    uint32_t reading_total = 0;
    uint32_t final_reading = 0;
    uint32_t total_reads = 10;
    init_moisture_sensor();

    for (int i = 0; i < total_reads; i++) {
        reading_total += adc1_get_raw(SENSOR_ADC_CHANNEL);
        vTaskDelay(10 / portTICK_RATE_MS);
    }
    final_reading = reading_total / total_reads;

    uint32_t voltage = esp_adc_cal_raw_to_voltage(final_reading, adc_chars);

    LOGI("Soil Moisture raw Reading: %d\n", final_reading);
    LOGI("Soil Moisture voltage: %d\n", voltage);

    shutdown_sensor();
    return voltage;
}

uint32_t convert_moisture_voltage_to_pct(uint32_t voltage, uint32_t low_voltage, uint32_t high_voltage) {
    if (voltage < low_voltage) return 100;
    if (voltage > high_voltage) return 0;
    return 100 - (((voltage - low_voltage) * 100) / (high_voltage - low_voltage));
}