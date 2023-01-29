// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <freertos/FreeRTOS.h>
#include <driver/adc.h>
#include <driver/rtc_io.h>
#include <freertos/task.h>
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "volf_sensors.h"

#define CURRENT_SENSOR_ADC_ATTENUATION ADC_ATTEN_DB_11
#define CURRENT_SENSOR_ADC_UNIT ADC_UNIT_1
#define CURRENT_SENSOR_BIT_WIDTH ADC_WIDTH_BIT_12
#define AC_DETECTION_RANGE 20
#define V_REF 1100  // ADC reference voltage

static bool adc_chars_initialized = false;


static esp_adc_cal_characteristics_t *init_adc_for_current() {
    esp_adc_cal_characteristics_t *adc_chars;

    // Characterize ADC at particular attenuation
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(CURRENT_SENSOR_ADC_UNIT, CURRENT_SENSOR_ADC_ATTENUATION, CURRENT_SENSOR_BIT_WIDTH, V_REF,
                             adc_chars);

    // Configure ADC bit width
    //adc_set_data_width(CURRENT_SENSOR_ADC_UNIT, CURRENT_SENSOR_BIT_WIDTH);
    adc1_config_width(CURRENT_SENSOR_BIT_WIDTH);
    adc1_config_channel_atten(ADC1_CHANNEL_0, CURRENT_SENSOR_ADC_ATTENUATION);
    adc1_config_channel_atten(ADC1_CHANNEL_3, CURRENT_SENSOR_ADC_ATTENUATION);
    adc1_config_channel_atten(ADC1_CHANNEL_6, CURRENT_SENSOR_ADC_ATTENUATION);
    adc1_config_channel_atten(ADC1_CHANNEL_7, CURRENT_SENSOR_ADC_ATTENUATION);

    return adc_chars;
}

uint32_t read_ac_current(adc1_channel_t channel) {
    uint32_t virtual_voltage_value;
    uint32_t reading;
    uint32_t raw_voltage;
    uint32_t voltage = 0;



    for (int i = 0; i < 15; i++)
    {
        esp_adc_cal_characteristics_t *adc_chars = init_adc_for_current();
        reading = adc1_get_raw(channel);
//        LOGI("Received reading value of %d", reading);

        raw_voltage = esp_adc_cal_raw_to_voltage(reading, adc_chars);


        //esp_adc_cal_get_voltage((adc_channel_t) channel, adc_chars, &reading);
        voltage += raw_voltage;

        free(adc_chars);
        vTaskDelay(1 / portTICK_RATE_MS);
    }

    voltage = voltage / 15;

    voltage = voltage * 736;

//    LOGI("Peak voltage: %d", voltage);

    /*The circuit is amplified by 2 times, so it is divided by 2.*/
    virtual_voltage_value = voltage / 1024 / 2;

//    LOGI("Virtual voltage after adjustment: %d", virtual_voltage_value);

    return virtual_voltage_value * AC_DETECTION_RANGE;
}


