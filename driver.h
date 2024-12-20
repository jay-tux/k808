//
// Created by jay on 12/19/24.
//

#ifndef UTIL_H
#define UTIL_H

#define USB_VID "30fa"
#define USB_PID "2350"

#define REMAP_VENDOR 0x3008
#define REMAP_PRODUCT 0x800E

#include <stdint.h>

struct layer;
struct context;
struct source;

enum k808_key {
  K808_0 = 0, K808_1, K808_2, K808_3, K808_4, K808_5, K808_6, K808_7, K808_8, K808_9,
  K808_DOT, K808_ENTER,

  K808_KEY_COUNT
};

enum k808_event_type {
  K808_PRESS = 0,
  K808_RELEASE
};

#define K808_X_KEYS X(K808_0) X(K808_1) X(K808_2) X(K808_3) X(K808_4) X(K808_5) X(K808_6) X(K808_7) X(K808_8) X(K808_9) X(K808_DOT) X(K808_ENTER)
#define K808_X_EVENTS X(K808_PRESS) X(K808_RELEASE)

typedef void (*key_handler)(const struct source *src, enum k808_key key, enum k808_event_type is_release, void *user_data);

struct context *init_ctx(void);
struct layer *ctx_add_layer(struct context *ctx, const char *layer_name);
void layer_register_override(struct layer *layer, enum k808_key key, key_handler handler, void *user_data);
void ctx_activate(struct context *ctx);
void ctx_wait(const struct context *ctx);
void ctx_free(struct context *ctx);

void handle_send_keys(const struct source *src, const uint16_t *keys, int count, int is_reverse, int is_key_up);

#endif //UTIL_H
