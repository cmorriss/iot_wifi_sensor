// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <esp_log.h>
#include <stdio.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include "volf_log.h"
#include "volf_error.h"

static char addr_buf[6 * 3];
static uint8_t base_mac[6];

uint8_t *
volf_get_addr() {
    volf_handle_error(RETRY, "esp_base_mac_addr_get", esp_base_mac_addr_get(base_mac));

    return base_mac;
}

char *volf_addr_str(const void *addr) {
    const uint8_t *u8p;

    u8p = addr;
    sprintf(addr_buf, "%02x_%02x_%02x_%02x_%02x_%02x",
            u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);

    return addr_buf;
}

void volf_clear_flash(const char *reason) {
    nvs_flash_erase();
    nvs_flash_init();

}
