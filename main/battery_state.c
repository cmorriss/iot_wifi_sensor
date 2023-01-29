// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <driver/adc.h>
#include <memory.h>
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "iot_wifi_sensor.h"

#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_0
#define BATTERY_ADC_ATTENUATION ADC_ATTEN_DB_11
#define BATTERY_ADC_UNIT ADC_UNIT_1

static esp_adc_cal_characteristics_t* adc_chars;

void init_adc() {
    LOGI("Initializing ADC for reading remaining battery level.");
    // Characterize ADC at particular attenuation
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(BATTERY_ADC_UNIT, BATTERY_ADC_ATTENUATION, ADC_WIDTH_BIT_12, 0, adc_chars);

    // Configure ADC channel
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
}

void shutdown_adc() {
    free(adc_chars);
}

uint32_t read_battery_voltage() {
    init_adc();

    uint32_t reading = adc1_get_raw(BATTERY_ADC_CHANNEL);

    uint32_t voltage = esp_adc_cal_raw_to_voltage(reading, adc_chars);

    shutdown_adc();

    LOGI("Battery voltage raw Reading: %d\n", reading);
    LOGI("Calculated battery voltage: %d\n", voltage);
    return voltage;
}

uint32_t convert_battery_voltage_to_pct(uint32_t voltage, uint32_t low_voltage, uint32_t high_voltage) {
    LOGI("Calculating battery pct with lv: %d, hv: %d", low_voltage, high_voltage);
    if (voltage < low_voltage) return 0;
    if (voltage > high_voltage) return 100;
    return ((voltage - low_voltage) * 100) / (high_voltage - low_voltage);
}