#include <stdio.h>
#include <stdlib.h>
#include <linux/input-event-codes.h>

#include "driver.h"

static uint16_t keys[2] = { KEY_LEFTMETA, 0 };
const static uint16_t remap[K808_KEY_COUNT] = {
  KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_KPDOT, KEY_KPENTER
};

void override_to_win_key(const struct source *src, const enum k808_key key, const enum k808_event_type is_up, void *) {
  const char *event = is_up == K808_PRESS ? "press" : "release";

  switch (key) {
#define X(k) case k: printf("K808: trigger %s %s (%d)\n", event, #k, k); break;
    K808_X_KEYS
    case K808_KEY_COUNT: printf("K808: invalid trigger (KEY_COUNT)\n"); break;
    default: printf("K808: unknown trigger %d\n", key); break;
#undef X
  }

  keys[1] = remap[key];
  handle_send_keys(src, keys, 2, is_up == K808_RELEASE, is_up == K808_RELEASE);
}

int main(void) {
  struct context *ctx = init_ctx();
  if (ctx == NULL) {
    printf("K808 context failed to initialize.\n");
    return EXIT_FAILURE;
  }

  struct layer *layer0 = ctx_add_layer(ctx, "__default__");
  if (layer0 == NULL) {
    printf("K808 layer failed to initialize.\n");
    ctx_free(ctx);
    return EXIT_FAILURE;
  }

#define X(key) layer_register_override(layer0, key, override_to_win_key, NULL);
  K808_X_KEYS
#undef X

  ctx_activate(ctx);
  ctx_wait(ctx);
  ctx_free(ctx);

  return EXIT_SUCCESS;
}