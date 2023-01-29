// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include "volf_ota_update.h"
#include "volf_error.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "volf_wifi_connect.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "volf_log.h"
#include "esp_wifi.h"
#include "volf_misc.h"

#define HASH_LEN 32

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static char ota_update_url[256];

#define DEFAULT_OTA_BUF_SIZE 1024

static char *get_ota_update_url(char *node_addr, uint32_t desired_version) {
    LOGI("Generating update URL...");

    sprintf(ota_update_url, "https://otaupdates.home:13800/%s/iot_wifi_sensor_v%d.bin", node_addr, desired_version);

    LOGI("Generated OTA update url: %s", ota_update_url);
    return ota_update_url;
}

static void print_sha256(const uint8_t *image_hash, const char *label) {
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    LOGI("%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void) {
    uint8_t sha_256[HASH_LEN] = {0};
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address = ESP_BOOTLOADER_OFFSET;
    partition.size = ESP_PARTITION_TABLE_OFFSET;
    partition.type = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

void install_ota_update(char *node_address, uint32_t desired_version) {
    char *update_url;

    LOGI("Starting OTA update");

    get_sha256_of_partitions();
    /* Ensure to disable any WiFi power save mode, this allows best throughput
     * and hence timings for overall OTA operation.
     */
    esp_wifi_set_ps(WIFI_PS_NONE);

    update_url = get_ota_update_url(node_address, desired_version);
    esp_http_client_config_t config = {
            .url = update_url,
            .cert_pem = (char *) server_cert_pem_start,
            .timeout_ms = 20000,
            .skip_cert_common_name_check = true
    };

    esp_err_t ret = esp_https_ota(&config);
    if (ret != ESP_OK) {
        LOGE("Firmware upgrade failed. Restarting to try again...");
        volf_handle_error(RETRY, "esp_https_ota", ret);
    } else {
        LOGI("Firmware upgrade succeeded. Setting boot state to verify ota update.");
        LOGI("Restarting to load new firmware.");
        esp_restart();
    }
}
