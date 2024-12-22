//
// Created by jay on 12/22/24.
//

#ifndef K808_CONTEXT_H
#define K808_CONTEXT_H

#define K808_VENDOR_ID "30fa"
#define K808_PRODUCT_ID "2350"
#define K808_REMAPPED_VENDOR 0x3008
#define K808_REMAPPED_PRODUCT 0x800E

#include <stdint.h>

struct k808;
struct k808_layer;

enum k808_key {
  K808_0 = 0, K808_1, K808_2, K808_3, K808_4, K808_5, K808_6, K808_7, K808_8, K808_9,
  K808_DOT, K808_ENTER,

  K808_KEY_COUNT
};

enum k808_event {
  K808_KEY_PRESS = 0,
  K808_KEY_RELEASE
};

enum k808_start_result {
  K808_RUNNING, K808_NO_CTX, K808_ALREADY_RUNNING, K808_NO_LAYERS, K808_NO_DEVICES
};

struct key_event {
  uint16_t key;
  int is_key_press;
};

typedef void (*k808_handler)(enum k808_key key, enum k808_event event, void *user_data);
typedef void (*k808_layer_change)(const struct k808_layer *old, const struct k808_layer *new, void *user_data);

struct k808 *init_k808(void);
struct k808_layer *k808_add_layer(const struct k808 *k808, const char *layer_name);
int k808_layer_count(const struct k808 *k808);
struct k808_layer *k808_nth_layer(const struct k808 *k808, int n);
struct k808_layer *k808_current_layer(const struct k808 *k808);
int k808_current_layer_idx(const struct k808 *k808);
void k808_register_handler(struct k808_layer *layer, enum k808_key key, k808_handler handler, void *user_data);
void k808_register_layer_switch_handler(struct k808 *k808, k808_layer_change handler, void *user_data);
enum k808_start_result k808_start_async(struct k808 *k808);
void k808_stop_sync(struct k808 *k808);
void k808_free(struct k808 *k808);

void send_keys(const struct k808 *k808, const struct key_event *keys, int count);

#endif //K808_CONTEXT_H
