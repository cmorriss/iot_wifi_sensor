// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#ifndef H_BLEPRPH_
#define H_BLEPRPH_

#include "volf_log.h"
#include "volf_misc.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "sensors/volf_sensors.h"

#define VERSION 12

/** Battery remaining */
uint32_t read_battery_voltage();
uint32_t convert_battery_voltage_to_pct(uint32_t voltage, uint32_t low_voltage, uint32_t high_voltage);

#ifdef __cplusplus
}
#endif

#endif
