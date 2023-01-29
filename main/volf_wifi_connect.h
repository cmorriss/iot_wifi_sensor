// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_netif.h"

esp_err_t volf_wifi_connect(void);
esp_err_t volf_wifi_disconnect(void);

/**
 * @brief Returns esp-netif pointer created by volf_wifi_connect()
 */
esp_netif_t *get_esp_netif(void);

#ifdef __cplusplus
}
#endif
