// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>
#include "volf_log.h"

#define MAX_RUNTIME_SIZE 12

static void get_runtime(char *runtime_str) {
    uint32_t runtime = xTaskGetTickCount() * portTICK_PERIOD_MS;
    sprintf(runtime_str, "%d", runtime);
}

void volf_log_write(esp_log_level_t level,
                    const char *tag,
                    const char *format,
                    ...) {
    va_list args;
    char runtime_str[MAX_RUNTIME_SIZE];
    char *new_format;

    va_start(args, format);
    get_runtime(runtime_str);

    new_format = malloc((strlen(runtime_str) + strlen(format) + 4) * sizeof(char));
    sprintf(new_format, "(%s) %s", runtime_str, format);

    esp_log_writev(level, tag, new_format, args);

    va_end(args);
    free(new_format);
}
