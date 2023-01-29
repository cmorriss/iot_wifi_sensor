// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <string.h>
#include "volf_error.h"
#include "volf_log.h"
#include "volf_misc.h"

#include "nvs.h"

/**
 * Acronym definitions:
 *
 * ELC: Error Log Count
 * PAC: Publish Attempt Count
 * CC: Continue Count
 * PAR: Publish Attempt Runtime
 *
 * EC: Error Continue
 * ER: Error Retry
 * EA: Error Abort
 */

#define ERROR_LOG_COUNT_KEY "ELC"
#define MAX_KEY_SIZE 12
#define NVS_NAME_ERRORS "volf.errors"

/* AT_<ELC> */
char *PUBLISH_ATTEMPT_COUNT_KEY_TEMPLATE = "PAC_%d";

/* PAR_<ELC>_<PAC> */
char *PUBLISH_ATTEMPT_RUNTIME_KEY_TEMPLATE = "PAR_%d_%d";

/* EC_<ELC>_<PAC>_<CC> */
char *ERROR_CONTINUE_KEY_TEMPLATE = "EC_%d_%d_%d";

/* ER_<ELC>_<PAC> */
char *ERROR_RETRY_KEY_TEMPLATE = "ER_%d_%d";

/* EA_<ELC>_<PAC> */
char *ERROR_ABORT_KEY_TEMPLATE = "EA_%d_%d";

static uint8_t error_log_count = 0;
static uint8_t publish_attempt_count = 0;
static uint8_t continue_count = 0;

static volf_error_handler_t *abort_handler = NULL;
static volf_error_handler_t *retry_handler = NULL;
static volf_error_handler_t *continue_handler = NULL;

static bool initialized = false;
static struct volf_errors loaded_errors;

static void volf_read_errors();
static void volf_build_error_context_key(volf_error_t error, char *error_context_key);

void volf_register_error_handler(volf_error_t error, volf_error_handler_t handler) {
    switch (error) {
        case RETRY:
            retry_handler = handler;
            break;
        case ABORT:
            abort_handler = handler;
            break;
        case CONTINUE:
            continue_handler = handler;
            break;
        default:
            LOGW("Attempt to register handler for unknown error type: %d", error);
    }
}

/**
 * Initialize the error log state. This reads the
 */
void volf_error_init() {
    nvs_handle_t my_handle;
    char publish_attempt_key[MAX_KEY_SIZE];

    esp_err_t err = nvs_open(NVS_NAME_ERRORS, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        LOGE("Error (%s) opening NVS handle for error log initialization!\n", esp_err_to_name(err));
        return;
    }

    err = nvs_get_u8(my_handle, ERROR_LOG_COUNT_KEY, &error_log_count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }

    if (err != 0) {
        LOGE("Error (%s) reading error log count!\n", esp_err_to_name(err));
        nvs_erase_all(my_handle);
    } else {
        LOGI("Found log error count: %d", error_log_count);

        if (error_log_count > 0) {
            volf_read_errors();

            snprintf(publish_attempt_key, MAX_KEY_SIZE, PUBLISH_ATTEMPT_COUNT_KEY_TEMPLATE, error_log_count);
            err = nvs_get_u8(my_handle, publish_attempt_key, &publish_attempt_count);
            if (err != 0 && err != ESP_ERR_NVS_NOT_FOUND) {
                LOGE("Error (%s) reading error log count!\n", esp_err_to_name(err));
            }
            LOGI("Found publish attempt count %d", publish_attempt_count);
        } else {
            error_log_count = 1;
        }
        publish_attempt_count++;
    }
    nvs_close(my_handle);

    initialized = true;
}

bool volf_errors_available() {
    if (!initialized) {
        volf_error_init();
    }
    return loaded_errors.num_error_logs != 0;
}

void volf_clear_and_close(const nvs_handle_t *nvs_handle) {
    nvs_handle_t nvs_errors_handle;

    LOGI("Clearing all errors as it looks like there may have been a corruption.");
    if (nvs_handle != NULL) {
        nvs_errors_handle = *nvs_handle;
    } else {
        esp_err_t err = nvs_open(NVS_NAME_ERRORS, NVS_READWRITE, &nvs_errors_handle);
        if (err != ESP_OK) {
            LOGW("Error (%s) opening NVS handle to delete all error logs!\n", esp_err_to_name(err));
            return;
        }
    }

    nvs_erase_all(nvs_errors_handle);
    nvs_close(nvs_errors_handle);

    error_log_count = 0;
    publish_attempt_count = 0;
    continue_count = 0;
}

void volf_clear_errors() {
    volf_clear_and_close(NULL);
}

static esp_err_t volf_store_runtime(nvs_handle_t nvs_handle) {
    uint32_t publish_attempt_runtime;
    char publish_attempt_runtime_key[MAX_KEY_SIZE];
    esp_err_t err;

    publish_attempt_runtime = xTaskGetTickCount() * portTICK_PERIOD_MS;
    snprintf(publish_attempt_runtime_key, MAX_KEY_SIZE, PUBLISH_ATTEMPT_RUNTIME_KEY_TEMPLATE, error_log_count,
             publish_attempt_count);

    err = nvs_set_u32(nvs_handle, publish_attempt_runtime_key, publish_attempt_runtime);
    if (err != 0 && err != ESP_ERR_NVS_NOT_FOUND) {
        LOGI("Error (%s) setting key %s to value %d\n", esp_err_to_name(err), publish_attempt_runtime_key,
             publish_attempt_runtime);
        return err;
    } else {
        return ESP_OK;
    }
}

static void volf_build_error_context_key(volf_error_t error, char *error_context_key) {
    switch (error) {
        case RETRY:
            snprintf(error_context_key, MAX_KEY_SIZE, ERROR_RETRY_KEY_TEMPLATE, error_log_count, publish_attempt_count);
            break;
        case ABORT:
            snprintf(error_context_key, MAX_KEY_SIZE, ERROR_ABORT_KEY_TEMPLATE, error_log_count, publish_attempt_count);
            break;
        case CONTINUE:
            continue_count++;
            snprintf(error_context_key, MAX_KEY_SIZE, ERROR_CONTINUE_KEY_TEMPLATE, error_log_count,
                     publish_attempt_count, continue_count);
            break;
        default:
            LOGW("Cannot store error context for unknown error number %d", error);
    }
}

static esp_err_t volf_store_error_log_counts(nvs_handle_t nvs_handle) {
    char publish_attempt_count_key[MAX_KEY_SIZE];

    esp_err_t err = nvs_set_u8(nvs_handle, ERROR_LOG_COUNT_KEY, error_log_count);
    if (err != 0) {
        LOGE("Error (%s) setting error log count!\n", esp_err_to_name(err));
        return err;
    }

    snprintf(publish_attempt_count_key, MAX_KEY_SIZE, PUBLISH_ATTEMPT_COUNT_KEY_TEMPLATE, error_log_count);
    err = nvs_set_u8(nvs_handle, publish_attempt_count_key, publish_attempt_count);
    if (err != 0) {
        LOGE("Error (%s) setting publish attempt count!\n", esp_err_to_name(err));
    }
    return err;
}

static void volf_store_error_context(volf_error_t error, char *context, int associated_rc) {
    nvs_handle_t nvs_handle;
    char error_context_key[MAX_KEY_SIZE];
    char *context_and_rc;
    uint32_t max_context_and_rc_len = strlen(context) + 10;

    esp_err_t err = nvs_open(NVS_NAME_ERRORS, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOGE("Error (%s) opening NVS handle for error log update!\n", esp_err_to_name(err));
        return;
    }

    context_and_rc = malloc(max_context_and_rc_len * sizeof(char));
    snprintf(context_and_rc, max_context_and_rc_len, "%s(%d)", context, associated_rc);

    err = volf_store_error_log_counts(nvs_handle);
    if (err != ESP_OK) {
        volf_clear_and_close(&nvs_handle);
        return;
    }

    volf_build_error_context_key(error, error_context_key);
    LOGI("Storing error context: \"%s\" for key \"%s\".", context_and_rc, error_context_key);
    err = nvs_set_str(nvs_handle, error_context_key, context_and_rc);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        LOGE("Error (%s) setting key %s to value %s\n", esp_err_to_name(err), error_context_key, context_and_rc);
        volf_clear_and_close(&nvs_handle);
        return;
    }
    free(context_and_rc);

    if (error != CONTINUE) {
        err = volf_store_runtime(nvs_handle);
        if (err != ESP_OK) {
            volf_clear_and_close(&nvs_handle);
            return;
        }
    }

    nvs_close(nvs_handle);
}

static void volf_read_publish_attempt(nvs_handle_t nvs_handle, struct volf_publish_attempt *attempt, uint8_t log_num,
                                      uint8_t attempt_num) {
    int8_t continue_num;
    char nvs_key[MAX_KEY_SIZE];
    size_t nvs_str_length = MAX_ERROR_CONTEXT_SIZE;
    esp_err_t rc;

    snprintf(nvs_key, MAX_KEY_SIZE, PUBLISH_ATTEMPT_RUNTIME_KEY_TEMPLATE, log_num, attempt_num);
    LOGI("Reading publish attempt key %s", nvs_key);
    nvs_get_u32(nvs_handle, nvs_key, &attempt->runtime);

    snprintf(nvs_key, MAX_KEY_SIZE, ERROR_RETRY_KEY_TEMPLATE, log_num, attempt_num);
    LOGI("Reading publish attempt key %s", nvs_key);
    rc = nvs_get_str(nvs_handle, nvs_key, attempt->retry_context, &nvs_str_length);
    if (rc != ESP_OK) {
        LOGI("Received error (%s) when reading value for key %s", esp_err_to_name(rc), nvs_key);
    }

    nvs_str_length = MAX_ERROR_CONTEXT_SIZE;
    snprintf(nvs_key, MAX_KEY_SIZE, ERROR_ABORT_KEY_TEMPLATE, log_num, attempt_num);
    LOGI("Reading publish attempt key %s", nvs_key);
    rc = nvs_get_str(nvs_handle, nvs_key, attempt->abort_context, &nvs_str_length);
    if (rc != ESP_OK) {
        LOGI("Received error (%s) when reading value for key %s", esp_err_to_name(rc), nvs_key);
    }

    continue_num = 1;
    while (continue_num != -1 && continue_num <= MAX_CONTINUE_CONTEXTS) {
        snprintf(nvs_key, MAX_KEY_SIZE, ERROR_CONTINUE_KEY_TEMPLATE, log_num, attempt_num, continue_num);

        LOGI("Reading continue error key %s", nvs_key);
        nvs_str_length = MAX_ERROR_CONTEXT_SIZE;
        rc = nvs_get_str(nvs_handle, nvs_key, attempt->continue_contexts[continue_num - 1], &nvs_str_length);
        LOGI("Received rc from reading continue error: %s", esp_err_to_name(rc));
        if (rc != ESP_OK) {
            attempt->num_continue_contexts = continue_num - 1;
            continue_num = -1;
        } else {
            continue_num++;
        }
    }
}

struct volf_errors *volf_get_errors() {
    if (volf_errors_available()) {
        return &loaded_errors;
    } else {
        return NULL;
    }
}

static void volf_read_errors() {
    nvs_handle_t nvs_handle;
    esp_err_t rc;
    uint8_t logged_publish_attempts;
    struct volf_publish_attempt *current_attempt;
    uint8_t error_log_index = 0;
    char nvs_key[MAX_KEY_SIZE];

    loaded_errors.num_error_logs = 0;

    rc = nvs_open(NVS_NAME_ERRORS, NVS_READONLY, &nvs_handle);
    if (rc != ESP_OK) {
        LOGE("Error (%s) opening NVS handle for error log read!\n", esp_err_to_name(rc));
    }

    LOGI("Reading %d error logs", error_log_count);
    for (int error_log_num = 1; error_log_num <= error_log_count; error_log_num++) {
        LOGI("Error log %d", error_log_num);
        snprintf(nvs_key, MAX_KEY_SIZE, PUBLISH_ATTEMPT_COUNT_KEY_TEMPLATE, error_log_num);
        rc = nvs_get_u8(nvs_handle, nvs_key, &logged_publish_attempts);
        if (rc != 0) {
            if (rc == ESP_ERR_NVS_NOT_FOUND) {
                LOGW("Could not find attempt count for error log %d", error_log_num);
            } else {
                LOGE("Error (%s) when reading attempt count for error log %d", esp_err_to_name(rc), error_log_num);
            }
            continue;
        }
        LOGI("Publish attempts %d", logged_publish_attempts);

        if (logged_publish_attempts > MAX_PUBLISH_ATTEMPTS) {
            LOGE("Found too many publish attempts. Clearing all logs...");
            volf_clear_and_close(&nvs_handle);
            return;
        }

        for (int publish_attempt_num = 1; publish_attempt_num <= logged_publish_attempts; publish_attempt_num++) {
            current_attempt = &loaded_errors.error_logs[error_log_index].publish_attempts[publish_attempt_num - 1];
            volf_read_publish_attempt(nvs_handle, current_attempt, error_log_num, publish_attempt_num);
            LOGI("Publish attempt %d", publish_attempt_num);
            LOGI("Retry context = %s, Abort context = %s, Num continue contexts = %d, runtime = %d",
                 current_attempt->retry_context, current_attempt->abort_context, current_attempt->num_continue_contexts,
                 current_attempt->runtime);
        }
        loaded_errors.error_logs[error_log_index].num_publish_attempts = logged_publish_attempts;
        loaded_errors.num_error_logs++;
        error_log_index++;
    }

    nvs_close(nvs_handle);
}

static void volf_increment_error_log_count() {
    nvs_handle_t nvs_handle;
    esp_err_t rc;

    if (error_log_count + 1 > MAX_ERROR_LOGS) {
        LOGW("Warning: Max error logs stored.");
        volf_clear_and_close(NULL);
        return;
    }

    rc = nvs_open(NVS_NAME_ERRORS, NVS_READWRITE, &nvs_handle);
    if (rc != ESP_OK) {
        LOGE("Error (%s) opening NVS handle for incrementing error log count!\n", esp_err_to_name(rc));
    } else {
        rc = nvs_set_u8(nvs_handle, ERROR_LOG_COUNT_KEY, error_log_count + 1);
        if (rc != ESP_OK) {
            LOGE("Error (%s) incrementing error log count! Current log count: %d\n", esp_err_to_name(rc),
                 error_log_count);
            volf_clear_and_close(&nvs_handle);
            return;
        }
    }

    nvs_close(nvs_handle);
}

void volf_handle_error(volf_error_t error, char *context, int associated_rc) {
    if (!initialized) {
        volf_error_init();
    }

    if (associated_rc == ESP_OK) {
        LOGI("Execution of %s successful", context);
        return;
    }

    LOGE("An error occurred, context: %s, rc: %d", context, associated_rc);

    volf_store_error_context(error, context, associated_rc);

    if (publish_attempt_count >= MAX_PUBLISH_ATTEMPTS && error != CONTINUE) {
        error = ABORT;
    }

    switch (error) {
        case RETRY:
            if (retry_handler != NULL) {
                retry_handler();
            }
            break;
        case ABORT:
            volf_increment_error_log_count();
            if (abort_handler != NULL) {
                abort_handler();
            }
            break;
        case CONTINUE:
            if (continue_handler != NULL) {
                continue_handler();
            }
            break;
        default:
            LOGW("Cannot notify handler for unknown error type: %d", error);
    }
}