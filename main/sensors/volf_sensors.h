// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <driver/adc.h>

#include "volf_log.h"
#include "volf_misc.h"

/**
 * The ADC channels are specified by a bit mask, with the channel number being a 1 in the bit location.
 * Valid ADC channels include channels 0 (GPIO 36), 3 (GPIO 39), 6 (GPIO 35), and 7 (GPIO 34)
 */
#define ADC_CHANNEL_MASK_0 1
#define ADC_CHANNEL_MASK_1 2
#define ADC_CHANNEL_MASK_2 4
#define ADC_CHANNEL_MASK_3 8
#define ADC_CHANNEL_MASK_4 16
#define ADC_CHANNEL_MASK_5 32
#define ADC_CHANNEL_MASK_6 64
#define ADC_CHANNEL_MASK_7 128

#define DEFAULT_CURRENT_SENSOR false
#define DEFAULT_MOISTURE_SENSOR false
#define DEFAULT_TEMPERATURE_SENSOR false
#define DEFAULT_SHT40_SENSOR false
#define DEFAULT_HAS_BATTERY false
#define DEFAULT_DEEP_SLEEP true
#define DEFAULT_ADC_CHANNELS ADC_CHANNEL_MASK_0
#define DEFAULT_MOISTURE_LOW_VOLTAGE 1344
#define DEFAULT_MOISTURE_HIGH_VOLTAGE 2707
#define DEFAULT_BATTERY_LOW_VOLTAGE 1450
#define DEFAULT_BATTERY_HIGH_VOLTAGE 2110
#define DEFAULT_SLEEP_DURATION 3600

#define VALID_ADC_CHANNELS ADC_CHANNEL_MASK_0 & ADC_CHANNEL_MASK_3 & ADC_CHANNEL_MASK_6 & ADC_CHANNEL_MASK_7

struct sensor_config {
    bool current_sensor;
    bool moisture_sensor;
    bool temperature_sensor;
    bool sht40_sensor;
    bool has_battery;
    bool deep_sleep;
    uint8_t adc_channels;
    uint32_t moisture_low_voltage;
    uint32_t moisture_high_voltage;
    uint32_t battery_low_voltage;
    uint32_t battery_high_voltage;
    uint32_t sleep_duration;
    uint32_t version;
};

void hibernate_moisture_sensor();
void hibernate_temperature_sensor();

/** Moisture Sensor */
uint32_t read_soil_moisture_voltage();

uint32_t convert_moisture_voltage_to_pct(uint32_t voltage, uint32_t low_voltage, uint32_t high_voltage);

/** Temperature Sensor */
float read_temperature();

/** A/C Current Sensor */
uint32_t read_ac_current(adc1_channel_t channel);

/** SHT40 Humidity and Temperature Sensor */
void sht40_read_humidity_and_temperature(float *humidity, float* temperature);