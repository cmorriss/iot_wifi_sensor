// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <stdbool.h>

#ifndef VOLF_MISC_H
#define VOLF_MISC_H

uint8_t *volf_get_addr();
char* volf_addr_str(const void *addr);
void volf_clear_flash(const char *reason);

#endif //VOLF_MISC_H
