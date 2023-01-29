#include <sys/cdefs.h>
// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <inttypes.h>
#include "nvs_flash.h"
#include <esp_sleep.h>
#include <esp_ota_ops.h>
#include <esp_netif.h>
#include <esp_event.h>
#include "volf_ota_update.h"
#include "volf_error.h"
#include <stdio.h>
#include "aws_iot_config.h"
#include "aws_iot_mqtt_client_interface.h"
#include <string.h>
#include <cJSON.h>
#include <aws_iot_shadow_interface.h>
#include <driver/adc.h>
#include "volf_log.h"

#include "iot_wifi_sensor.h"
#include "volf_wifi_connect.h"

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define SLEEP_DURATION_KEY "slp_dur"
#define NVS_NAME_SENSOR_CONFIG "sensor.config"
#define SHADOW_CONNECT_RETRIES 5
#define MAX_SENSOR_PAYLOAD_SIZE 512
#define MAX_THING_NAME_SIZE 128

static struct sensor_config *desired_config;

extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");

/**
 * @brief Default MQTT port is pulled from the aws_iot_config.h
 */
uint32_t port = AWS_IOT_MQTT_PORT;

static struct sensor_config *init_sensor_config() {
    struct sensor_config *config = malloc(sizeof(struct sensor_config));
    config->current_sensor = DEFAULT_CURRENT_SENSOR;
    config->moisture_sensor = DEFAULT_MOISTURE_SENSOR;
    config->temperature_sensor = DEFAULT_TEMPERATURE_SENSOR;
    config->sht40_sensor = DEFAULT_SHT40_SENSOR;
    config->has_battery = DEFAULT_HAS_BATTERY;
    config->deep_sleep = DEFAULT_DEEP_SLEEP;
    config->adc_channels = DEFAULT_ADC_CHANNELS;
    config->moisture_low_voltage = DEFAULT_MOISTURE_LOW_VOLTAGE;
    config->moisture_high_voltage = DEFAULT_MOISTURE_HIGH_VOLTAGE;
    config->battery_low_voltage = DEFAULT_BATTERY_LOW_VOLTAGE;
    config->battery_high_voltage = DEFAULT_BATTERY_HIGH_VOLTAGE;
    config->sleep_duration = DEFAULT_SLEEP_DURATION;
    config->version = VERSION;
    return config;
}

static uint64_t read_sleep_duration() {
    nvs_handle_t nvs_handle;
    uint64_t sleep_duration = DEFAULT_SLEEP_DURATION;

    esp_err_t err = nvs_open(NVS_NAME_SENSOR_CONFIG, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_u64(nvs_handle, SLEEP_DURATION_KEY, &sleep_duration);
        LOGI("Found err when retrieving sleep duration key. %d", err);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            nvs_set_u64(nvs_handle, SLEEP_DURATION_KEY, sleep_duration);
        }
    } else {
        LOGE("Error (%s) opening NVS handle for reading sleep duration!\n", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);

    return sleep_duration;
}

static void store_sleep_duration(uint64_t sleep_duration) {
    nvs_handle_t nvs_handle;

    esp_err_t err = nvs_open(NVS_NAME_SENSOR_CONFIG, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOGE("Error (%s) opening NVS handle for storing sleep duration!\n", esp_err_to_name(err));
        return;
    }

    nvs_set_u64(nvs_handle, SLEEP_DURATION_KEY, sleep_duration);

    nvs_close(nvs_handle);
}

static void go_to_sleep() {
    uint64_t timeToSleep;
    uint64_t timeToSleepInSeconds = read_sleep_duration();

    LOGI("Going to sleep for %" PRId64 " seconds...", timeToSleepInSeconds);
    hibernate_moisture_sensor();
    hibernate_temperature_sensor();
    timeToSleep = timeToSleepInSeconds * uS_TO_S_FACTOR;
    esp_sleep_enable_timer_wakeup(timeToSleep);
    esp_deep_sleep_start();
}

char *create_sensor_payload(struct sensor_config config) {
    char *payload_str = NULL;
    uint32_t battery_voltage;
    uint32_t battery_pct;
    uint32_t moisture_voltage;
    uint32_t moisture_pct;
    float temperature;
    float humidity;
    uint32_t ac_current;

    cJSON *payload = cJSON_CreateObject();
    cJSON *state = cJSON_AddObjectToObject(payload, "state");
    if (state == NULL) {
        goto end;
    }

    cJSON *reported = cJSON_AddObjectToObject(state, "reported");
    if (reported == NULL) {
        goto end;
    }

    if (cJSON_AddNumberToObject(reported, "version", VERSION) == NULL) {
        goto end;
    }
    if (cJSON_AddNumberToObject(reported, "sleepDuration", config.sleep_duration) == NULL) {
        goto end;
    }
    if (cJSON_AddBoolToObject(reported, "deepSleep", config.deep_sleep) == NULL) {
        goto end;
    }

    if (config.has_battery) {
        battery_voltage = read_battery_voltage();
        battery_pct = convert_battery_voltage_to_pct(battery_voltage, config.battery_low_voltage,
                                                     config.battery_high_voltage);

        if (cJSON_AddNumberToObject(reported, "batteryVoltage", battery_voltage) == NULL) {
            goto end;
        }
        if (cJSON_AddNumberToObject(reported, "batteryPercent", battery_pct) == NULL) {
            goto end;
        }
        if (cJSON_AddNumberToObject(reported, "batteryLowVoltage", config.battery_low_voltage) == NULL) {
            goto end;
        }
        if (cJSON_AddNumberToObject(reported, "batteryHighVoltage", config.battery_high_voltage) == NULL) {
            goto end;
        }
    }

    if (config.moisture_sensor) {
        moisture_voltage = read_soil_moisture_voltage();

        moisture_pct = convert_moisture_voltage_to_pct(moisture_voltage, config.moisture_low_voltage,
                                                       config.moisture_high_voltage);

        if (cJSON_AddNumberToObject(reported, "moistureVoltage", moisture_voltage) == NULL) {
            goto end;
        }
        if (cJSON_AddNumberToObject(reported, "moisturePercent", moisture_pct) == NULL) {
            goto end;
        }
        if (cJSON_AddNumberToObject(reported, "moistureLowVoltage", config.moisture_low_voltage) == NULL) {
            goto end;
        }
        if (cJSON_AddNumberToObject(reported, "moistureHighVoltage", config.moisture_high_voltage) == NULL) {
            goto end;
        }
    }

    if (config.temperature_sensor) {
        temperature = read_temperature();

        if (cJSON_AddNumberToObject(reported, "temperature", temperature) == NULL) {
            goto end;
        }
    }

    if (config.sht40_sensor) {
        sht40_read_humidity_and_temperature(&humidity, &temperature);

        if (cJSON_AddNumberToObject(reported, "temperature", temperature) == NULL) {
            goto end;
        }
        if (cJSON_AddNumberToObject(reported, "humidity", humidity) == NULL) {
            goto end;
        }
    }

    if (config.current_sensor) {
        if ((config.adc_channels & ADC_CHANNEL_MASK_0) != 0) {
            ac_current = read_ac_current(ADC1_CHANNEL_0);
            LOGI("Current = %d", ac_current);
            if (cJSON_AddNumberToObject(reported, "acCurrent1", ac_current) == NULL) {
                goto end;
            }
        }
        if ((config.adc_channels & ADC_CHANNEL_MASK_3) != 0) {
            ac_current = read_ac_current(ADC1_CHANNEL_3);
            if (cJSON_AddNumberToObject(reported, "acCurrent2", ac_current) == NULL) {
                goto end;
            }
        }
        if ((config.adc_channels & ADC_CHANNEL_MASK_6) != 0) {
            ac_current = read_ac_current(ADC1_CHANNEL_6);
            if (cJSON_AddNumberToObject(reported, "acCurrent3", ac_current) == NULL) {
                goto end;
            }
        }
        if ((config.adc_channels & ADC_CHANNEL_MASK_7) != 0) {
            ac_current = read_ac_current(ADC1_CHANNEL_7);
            if (cJSON_AddNumberToObject(reported, "acCurrent4", ac_current) == NULL) {
                goto end;
            }
        }
    }
    payload_str = cJSON_Print(payload);

    LOGI("Final payload contents: %s", payload_str);
    end:
    cJSON_Delete(payload);
    return payload_str;
}

static void json_to_config(cJSON *json, struct sensor_config *config) {
    cJSON *json_tmp;

    json_tmp = cJSON_GetObjectItem(json, "moistureSensor");
    if (json_tmp != NULL) {
        LOGI("moisture sensor json type is %d", json_tmp->type);
        config->moisture_sensor = cJSON_IsTrue(json_tmp);
    }
    json_tmp = cJSON_GetObjectItem(json, "currentSensor");
    if (json_tmp != NULL) {
        LOGI("current sensor json type is %d", json_tmp->type);
        config->current_sensor = cJSON_IsTrue(json_tmp);
    }
    json_tmp = cJSON_GetObjectItem(json, "temperatureSensor");
    if (json_tmp != NULL) {
        LOGI("Temperature sensor json type is %d", json_tmp->type);
        config->temperature_sensor = cJSON_IsTrue(json_tmp);
    }
    json_tmp = cJSON_GetObjectItem(json, "sht40Sensor");
    if (json_tmp != NULL) {
        LOGI("SHT40 humidity and temperature sensor json type is %d", json_tmp->type);
        config->sht40_sensor = cJSON_IsTrue(json_tmp);
    }
    json_tmp = cJSON_GetObjectItem(json, "hasBattery");
    if (json_tmp != NULL) {
        LOGI("Has battery json type is %d", json_tmp->type);
        config->has_battery = cJSON_IsTrue(json_tmp);
    }
    json_tmp = cJSON_GetObjectItem(json, "deepSleep");
    if (json_tmp != NULL) {
        config->deep_sleep = cJSON_IsTrue(json_tmp);
    }
    json_tmp = cJSON_GetObjectItem(json, "adcChannels");
    if (json_tmp != NULL) {
        config->adc_channels = json_tmp->valueint;
    }
    json_tmp = cJSON_GetObjectItem(json, "batteryHighVoltage");
    if (json_tmp != NULL) {
        config->battery_high_voltage = json_tmp->valueint;
    }
    json_tmp = cJSON_GetObjectItem(json, "batteryLowVoltage");
    if (json_tmp != NULL) {
        config->battery_low_voltage = json_tmp->valueint;
    }
    json_tmp = cJSON_GetObjectItem(json, "moistureHighVoltage");
    if (json_tmp != NULL) {
        config->moisture_high_voltage = json_tmp->valueint;
    }
    json_tmp = cJSON_GetObjectItem(json, "moistureLowVoltage");
    if (json_tmp != NULL) {
        config->moisture_low_voltage = json_tmp->valueint;
    }
    json_tmp = cJSON_GetObjectItem(json, "sleepDuration");
    if (json_tmp != NULL) {
        config->sleep_duration = json_tmp->valueint;
    }
    json_tmp = cJSON_GetObjectItem(json, "version");
    if (json_tmp != NULL) {
        config->version = json_tmp->valueint;
    }
}

void get_sensor_shadow_callback(const char *thing_name, ShadowActions_t action, Shadow_Ack_Status_t status,
                                const char *payload, void *context_data) {
    desired_config = init_sensor_config();
    LOGI("Received json payload for existing shadow:\n %s", payload);
    cJSON *root = cJSON_Parse(payload);
    cJSON *state = cJSON_GetObjectItem(root, "state");
    json_to_config(cJSON_GetObjectItem(state, "desired"), desired_config);
    cJSON_Delete(root);
}

void connect_to_aws(AWS_IoT_Client *client, char *thing_name) {
    int shadow_connect_try = 0;
    IoT_Error_t rc;

    ShadowInitParameters_t sp = ShadowInitParametersDefault;
    sp.pHost = AWS_IOT_MQTT_HOST;
    sp.port = AWS_IOT_MQTT_PORT;
    sp.pClientCRT = (const char *) certificate_pem_crt_start;
    sp.pClientKey = (const char *) private_pem_key_start;
    sp.pRootCA = (const char *) aws_root_ca_pem_start;
    sp.enableAutoReconnect = false;
    sp.disconnectHandler = NULL;

    LOGI("Shadow Init");
    volf_handle_error(RETRY, "aws_iot_shadow_init", aws_iot_shadow_init(client, &sp));

    ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
    scp.pMyThingName = thing_name;
    scp.pMqttClientId = thing_name;
    scp.mqttClientIdLen = (uint16_t) strlen(thing_name);

    LOGI("Shadow Connect");
    do {
        rc = aws_iot_shadow_connect(client, &scp);
        shadow_connect_try++;
    } while (shadow_connect_try <= SHADOW_CONNECT_RETRIES && rc != SUCCESS);
    volf_handle_error(RETRY, "aws_iot_shadow_connect", rc);

    /**
     * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    volf_handle_error(CONTINUE, "aws_iot_shadow_set_autoreconnect_status",
                      aws_iot_shadow_set_autoreconnect_status(client, true));
}


static char *convert_error_logs_to_json(struct volf_errors *errors) {
    struct volf_error_log current_log;
    struct volf_publish_attempt current_attempt;
    char *json_logs = NULL;

    LOGI("Building json string for %d error logs.", errors->num_error_logs);
    cJSON *payload = cJSON_CreateObject();
    cJSON *state = cJSON_AddObjectToObject(payload, "state");
    if (state == NULL) goto end_error_log_conversion;

    cJSON *reported = cJSON_AddObjectToObject(state, "reported");
    if (reported == NULL) goto end_error_log_conversion;

    cJSON *error_logs = cJSON_AddArrayToObject(reported, "els");
    if (error_logs == NULL) goto end_error_log_conversion;

    for (int i = 0; i < errors->num_error_logs; i++) {
        current_log = errors->error_logs[i];
        cJSON *error_log = cJSON_CreateObject();
        if (error_log == NULL) goto end_error_log_conversion;
        cJSON_AddItemToArray(error_logs, error_log);

        cJSON *publish_attempts = cJSON_AddArrayToObject(error_log, "pas");
        if (publish_attempts == NULL) goto end_error_log_conversion;

        for (int j = 0; j < current_log.num_publish_attempts; j++) {
            current_attempt = current_log.publish_attempts[j];
            cJSON *publish_attempt = cJSON_CreateObject();
            if (publish_attempt == NULL) goto end_error_log_conversion;
            cJSON_AddItemToArray(publish_attempts, publish_attempt);

            if (cJSON_AddNumberToObject(publish_attempt, "r", current_attempt.runtime) == NULL)
                goto end_error_log_conversion;

            if (cJSON_AddStringToObject(publish_attempt, "rc", current_attempt.retry_context) == NULL)
                goto end_error_log_conversion;

            if (cJSON_AddStringToObject(publish_attempt, "ac", current_attempt.abort_context) == NULL)
                goto end_error_log_conversion;

            cJSON *continue_contexts = cJSON_AddArrayToObject(publish_attempt, "cc");
            if (continue_contexts == NULL) goto end_error_log_conversion;

            for (int k = 0; k < current_attempt.num_continue_contexts; k++) {
                cJSON *continue_context = cJSON_CreateString(current_attempt.continue_contexts[k]);
                if (continue_context == NULL) goto end_error_log_conversion;
                cJSON_AddItemToArray(continue_contexts, continue_context);
            }
        }
    }

    json_logs = cJSON_PrintUnformatted(payload);
    if (json_logs == NULL) {
        LOGE("Failed to print error log json payload.");
    }
    end_error_log_conversion:
    cJSON_Delete(payload);
    return json_logs;
}

static void publish_error_logs(AWS_IoT_Client *client, const char *thing_name) {
    char *log_payload;
    struct volf_errors *errors;
    IoT_Error_t err;

    errors = volf_get_errors();
    log_payload = convert_error_logs_to_json(errors);
    if (log_payload == NULL) {
        LOGE("An error occurred while converting the logs to json!");
        return;
    }
    LOGI("Publishing log payload: \n%s", log_payload);
    err = aws_iot_shadow_update(client, thing_name, log_payload, NULL, NULL, 10, false);
    LOGI("Received rc from shadow update, %d", err);
    volf_handle_error(CONTINUE, "aws_iot_shadow_yield_3", aws_iot_shadow_yield(client, 5000));
    if (err == SUCCESS) {
        LOGI("Successfully published logs. Clearing local copy.");
        volf_clear_errors();
    }
}

_Noreturn void read_and_report_task(void *param) {
    int shadow_get_try = 0;
    IoT_Error_t rc;
    char *sensor_payload;
    char thing_name[MAX_THING_NAME_SIZE];
    AWS_IoT_Client client;
    char *node_address = volf_addr_str(volf_get_addr());
    int yields_for_shadow = 30;

    snprintf(thing_name, MAX_THING_NAME_SIZE, "Sensor_%s", node_address);

    while (true) {
        if (client.clientStatus.clientState < CLIENT_STATE_CONNECTED_IDLE || client.clientStatus.clientState > CLIENT_STATE_CONNECTED_WAIT_FOR_CB_RETURN) {
            connect_to_aws(&client, thing_name);

            LOGI("Getting shadow...");
            do {
                rc = aws_iot_shadow_get(&client, thing_name, get_sensor_shadow_callback, NULL, 20, false);
                shadow_get_try++;
            } while (shadow_get_try <= SHADOW_CONNECT_RETRIES && rc != SUCCESS);
            volf_handle_error(RETRY, "aws_iot_shadow_get", rc);

            LOGI("Yielding for shadow...");

            while (desired_config == NULL && yields_for_shadow > 0) {
                volf_handle_error(CONTINUE, "aws_iot_shadow_yield_1", aws_iot_shadow_yield(&client, 1000));
                yields_for_shadow--;
            }

            if (desired_config == NULL) {
                volf_handle_error(RETRY, "aws_iot_shadow_get", 9999);
            }

            if (desired_config->sleep_duration != 0) {
                store_sleep_duration(desired_config->sleep_duration);
            }
        } else {
            LOGI("Already connected to AWS. Reading sensor data...");
        }

        sensor_payload = create_sensor_payload(*desired_config);

        volf_handle_error(RETRY, "aws_iot_shadow_update",
                          aws_iot_shadow_update(&client, thing_name, sensor_payload, NULL, NULL, 10, false));

        volf_handle_error(CONTINUE, "aws_iot_shadow_yield_2", aws_iot_shadow_yield(&client, 1000));

        if (volf_errors_available()) {
            publish_error_logs(&client, thing_name);
        }

        if (desired_config->version > VERSION) {
            install_ota_update(node_address, desired_config->version);
        }

        if (sensor_payload != NULL) {
            free(sensor_payload);
        }

        if (desired_config->deep_sleep) {
            LOGI("Successfully published sensor reading. Going to sleep...");
            go_to_sleep();
        } else {
            LOGI("Successfully published sensor reading. Resting for %d seconds.", desired_config->sleep_duration);
            vTaskDelay((desired_config->sleep_duration * 1000) / portTICK_RATE_MS);
        }
    }
}

static void verify_ota_update() {
    esp_err_t rc;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            rc = esp_ota_mark_app_valid_cancel_rollback();
            if (rc == ESP_OK) {
                LOGI("App is valid, rollback cancelled successfully");
            } else {
                volf_handle_error(CONTINUE, "esp_ota_mark_app_valid_cancel_rollback", rc);
            }
        }
    }
}

static void init_wifi() {
    LOGI("Initializing WIFI...");
    volf_handle_error(RETRY, "esp_netif_init", esp_netif_init());
    volf_handle_error(RETRY, "esp_event_loop_create_default", esp_event_loop_create_default());

    volf_handle_error(RETRY, "volf_wifi_connect", volf_wifi_connect());
    LOGI("WIFI Initialization complete.");
}

void app_main(void) {
    LOGI("Starting main, firmware version is %d\n", VERSION);

    /* Initialize NVS — it is used to store PHY calibration data and whether an update is available*/
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        LOGE("Unable to initialize flash! err=%s", esp_err_to_name(err));
    }

    volf_error_init();
    volf_register_error_handler(RETRY, esp_restart);
    volf_register_error_handler(ABORT, go_to_sleep);

    init_wifi();

    verify_ota_update();

    xTaskCreatePinnedToCore(&read_and_report_task, "read_and_report_task", 18432, NULL, 5, NULL, 1);
}
