// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <esp_log.h>
#include <volf_misc.h>

#ifndef IOT_WIFI_SENSOR_VOLF_LOG_H
#define IOT_WIFI_SENSOR_VOLF_LOG_H

#define MAX_LOG_LEVEL ESP_LOG_DEBUG
#define LOG_NAME "mn"

#define FORMAT_LOG_MSG(format, letter) "(" #letter ") " LOG_NAME ": " format

void volf_log_write(esp_log_level_t level,
                    const char *format,
                    const char *tag,
                    ...);

#define LOGL__(level, format, ...) do { \
    if (MAX_LOG_LEVEL >= level) {       \
        volf_log_write(level, LOG_NAME, format, ##__VA_ARGS__); \
    }\
} while(0)

#define LOGD__(format, ...) LOGL__(ESP_LOG_DEBUG, format, ##__VA_ARGS__)
#define LOGI__(format, ...) LOGL__(ESP_LOG_INFO, format, ##__VA_ARGS__)
#define LOGW__(format, ...) LOGL__(ESP_LOG_WARN, format, ##__VA_ARGS__)
#define LOGE__(format, ...) LOGL__(ESP_LOG_ERROR, format, ##__VA_ARGS__)

#define LOGD_(format, ...) LOGD__((FORMAT_LOG_MSG(format, D)), ##__VA_ARGS__)
#define LOGI_(format, ...) LOGI__((FORMAT_LOG_MSG(format, I)), ##__VA_ARGS__)
#define LOGW_(format, ...) LOGW__((FORMAT_LOG_MSG(format, W)), ##__VA_ARGS__)
#define LOGE_(format, ...) LOGE__((FORMAT_LOG_MSG(format, E)), ##__VA_ARGS__)

#define LOGD(format, ... ) LOGD_(format "\n", ##__VA_ARGS__)
#define LOGI(format, ... ) LOGE_(format "\n", ##__VA_ARGS__)
#define LOGW(format, ... ) LOGW_(format "\n", ##__VA_ARGS__)
#define LOGE(format, ... ) LOGE_(format "\n", ##__VA_ARGS__)

#endif //IOT_WIFI_SENSOR_VOLF_LOG_H
