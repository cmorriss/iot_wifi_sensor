// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#ifndef VOLF_OTA_UPDATE_H
#define VOLF_OTA_UPDATE_H

#include <stdint-gcc.h>

void install_ota_update(char *node_address, uint32_t desired_version);

#endif //VOLF_OTA_UPDATE_H
