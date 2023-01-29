// © Christopher Morrissey <cmorriss@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include <stdbool.h>

#ifndef VOLF_ERROR_H_
#define VOLF_ERROR_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_ERROR_CONTEXT_SIZE 64
#define MAX_CONTINUE_CONTEXTS 5
#define MAX_PUBLISH_ATTEMPTS 3
#define MAX_ERROR_LOGS 5

typedef enum {
    CONTINUE = 0,
    RETRY = 1,
    ABORT = 2
} volf_error_t;

struct volf_publish_attempt {
    uint32_t runtime;
    char retry_context[MAX_ERROR_CONTEXT_SIZE];
    char abort_context[MAX_ERROR_CONTEXT_SIZE];
    char continue_contexts[MAX_CONTINUE_CONTEXTS][MAX_ERROR_CONTEXT_SIZE];
    uint8_t num_continue_contexts;
};

struct volf_error_log {
    struct volf_publish_attempt publish_attempts[MAX_PUBLISH_ATTEMPTS];
    uint8_t num_publish_attempts;
};

struct volf_errors {
    struct volf_error_log error_logs[MAX_ERROR_LOGS];
    uint8_t num_error_logs;
};

typedef void volf_error_handler_t();

void volf_register_error_handler(volf_error_t error, volf_error_handler_t handler);
bool volf_errors_available();
struct volf_errors *volf_get_errors();
void volf_clear_errors();
void volf_error_init();
void volf_handle_error(volf_error_t error, char *context, int associated_rc);

#ifdef __cplusplus
}
#endif

#endif /* VOLF_ERROR_H_ */