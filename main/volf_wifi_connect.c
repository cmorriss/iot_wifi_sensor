// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <string.h>
#include "volf_wifi_connect.h"
#include "volf_log.h"
#include "volf_error.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"

#define NR_OF_IP_ADDRESSES_TO_WAIT_FOR (s_active_interfaces)

#define CONFIG_WIFI_SSID "dadiator"
#define CONFIG_WIFI_PASSWORD "chr0nika"

static int s_active_interfaces = 0;
static xSemaphoreHandle s_semph_get_ip_addrs;
static esp_ip4_addr_t s_ip_addr;

static esp_netif_t* wifi_start(void);
static void wifi_stop(void);
static esp_netif_t *get_netif_from_desc(const char *desc);

/**
 * @brief Checks the netif description if it contains specified prefix.
 * All netifs created withing common connect component are prefixed with the module TAG,
 * so it returns true if the specified netif is owned by this module
 */
static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix)-1) == 0;
}

/* set up connection, Wi-Fi and/or Ethernet */
static void start(void)
{
    wifi_start();
    s_active_interfaces++;
    s_semph_get_ip_addrs = xSemaphoreCreateCounting(NR_OF_IP_ADDRESSES_TO_WAIT_FOR, 0);
}

/* tear down connection, release resources */
static void stop(void)
{
    wifi_stop();
    s_active_interfaces--;
}

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (!is_our_netif(LOG_NAME, event->esp_netif)) {
        LOGW("Got IPv4 from another interface \"%s\": ignored", esp_netif_get_desc(event->esp_netif));
        return;
    }
    LOGI("Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
    xSemaphoreGive(s_semph_get_ip_addrs);
}

esp_err_t volf_wifi_connect(void)
{
    if (s_semph_get_ip_addrs != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    start();
    volf_handle_error(CONTINUE, "esp_register_shutdown_handler", esp_register_shutdown_handler(&stop));
    LOGI("Waiting for IP(s)");
    for (int i=0; i<NR_OF_IP_ADDRESSES_TO_WAIT_FOR; ++i) {
        xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
    }
    // iterate over active interfaces, and print out IPs of "our" netifs
    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ip;
    for (int i=0; i<esp_netif_get_nr_of_ifs(); ++i) {
        netif = esp_netif_next(netif);
        if (is_our_netif(LOG_NAME, netif)) {
            LOGI("Connected to %s", esp_netif_get_desc(netif));
            volf_handle_error(RETRY, "esp_netif_get_ip_info", esp_netif_get_ip_info(netif, &ip));

            LOGI("- IPv4 address: " IPSTR, IP2STR(&ip.ip));
        }
    }
    return ESP_OK;
}

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    LOGI("Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    volf_handle_error(RETRY, "esp_wifi_connect", err);
}

static esp_netif_t* wifi_start(void)
{
    char *desc;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    volf_handle_error(RETRY, "esp_wifi_init", esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Prefix the interface description with the log name
    asprintf(&desc, "%s: %s", LOG_NAME, esp_netif_config.if_desc);
    esp_netif_config.if_desc = desc;
    esp_netif_config.route_prio = 128;
    esp_netif_t *netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    free(desc);
    esp_wifi_set_default_wifi_sta_handlers();

    volf_handle_error(CONTINUE, "esp_event_handler_register:on_wifi_disconnect", esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL));
    volf_handle_error(RETRY, "esp_event_handler_register:on_got_ip", esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));

    volf_handle_error(CONTINUE, "esp_wifi_set_storage", esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    LOGI("Connecting to %s...", wifi_config.sta.ssid);
    volf_handle_error(RETRY, "esp_wifi_set_mode", esp_wifi_set_mode(WIFI_MODE_STA));
    volf_handle_error(RETRY, "esp_wifi_set_config", esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    volf_handle_error(RETRY, "esp_wifi_start", esp_wifi_start());
    volf_handle_error(RETRY, "esp_wifi_connect", esp_wifi_connect());
    return netif;
}

static void wifi_stop(void)
{
    esp_netif_t *wifi_netif = get_netif_from_desc("sta");
    volf_handle_error(CONTINUE, "esp_event_handler_unregister:on_wifi_disconnect", esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    volf_handle_error(CONTINUE, "esp_event_handler_unregister:on_get_ip", esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip));
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    volf_handle_error(CONTINUE, "esp_wifi_stop", err);
    volf_handle_error(CONTINUE, "esp_wifi_deinit", esp_wifi_deinit());
    volf_handle_error(CONTINUE, "esp_wifi_clear_default_wifi_driver_and_handlers", esp_wifi_clear_default_wifi_driver_and_handlers(wifi_netif));
    esp_netif_destroy(wifi_netif);
}

static esp_netif_t *get_netif_from_desc(const char *desc)
{
    esp_netif_t *netif = NULL;
    char *expected_desc;
    asprintf(&expected_desc, "%s: %s", LOG_NAME, desc);
    while ((netif = esp_netif_next(netif)) != NULL) {
        if (strcmp(esp_netif_get_desc(netif), expected_desc) == 0) {
            free(expected_desc);
            return netif;
        }
    }
    free(expected_desc);
    return netif;
}
