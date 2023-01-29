// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only
#include <sht4x.h>
#include <string.h>
#include <volf_log.h>

#define I2C_MASTER_SDA 21
#define I2C_MASTER_SCL 22

static sht4x_t dev;
static bool init_flag = false;

void init_if_needed() {
    if (!init_flag) {
        init_flag = true;
        ESP_ERROR_CHECK(i2cdev_init());
        memset(&dev, 0, sizeof(sht4x_t));

        ESP_ERROR_CHECK(sht4x_init_desc(&dev, 0, I2C_MASTER_SDA, I2C_MASTER_SCL));
        ESP_ERROR_CHECK(sht4x_init(&dev));
    }
}

void sht40_read_humidity_and_temperature(float *humidity, float* temperature) {
    init_if_needed();
    ESP_ERROR_CHECK(sht4x_measure(&dev, temperature, humidity));
}
