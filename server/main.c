#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/input-event-codes.h>

#include "server.h"
#include "k808_context.h"
#include "string.h"

#ifndef K808_SERVER
#error "K808_SERVER is not defined. Expected a file path."
#endif

static struct key_event press[2] = {
  { .key = KEY_LEFTMETA, .is_key_press = 1 },
  { .key = 0, .is_key_press = 1 }
};

static struct key_event release[2] = {
  { .key = 0, .is_key_press = 0 },
{ .key = KEY_LEFTMETA, .is_key_press = 0 },
};

const static uint16_t remap[K808_KEY_COUNT] = {
  KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_KPDOT, KEY_KPENTER
};

#define K808_X_KEYS X(K808_0) X(K808_1) X(K808_2) X(K808_3) X(K808_4) X(K808_5) X(K808_6) X(K808_7) X(K808_8) X(K808_9) X(K808_DOT) X(K808_ENTER)

void on_key(const enum k808_key key, const enum k808_event event, void *user) {
  const struct k808 *k808 = user;
  const char *event_str = event == K808_KEY_PRESS ? "press" : "release";
  const char *key_str = NULL;
  switch (key) {
#define X(k) case k: key_str = #k; break;
    K808_X_KEYS
#undef X
    default: key_str = NULL;
  }

  fprintf(stderr, "K808: trigger %s %s (%d)\n", event_str, key_str, key);

  if (event == K808_KEY_PRESS) {
    press[1].key = remap[key];
    send_keys(k808, press, 2);
  }
  else {
    release[0].key = remap[key];
    send_keys(k808, release, 2);
  }
}

enum server_response on_server_message(struct server *srv, const size_t len, const char *msg, void *) {
  if (len < 8) {
    fprintf(stderr, "Invalid message length: %lu (expected 8 or more)\n", len);
    return SERVER_CLOSE_CONN;
  }

  if (msg[0] != 'K' || msg[1] != '8' || msg[2] != '0' || msg[3] != '8') {
    fprintf(stderr, "Invalid message header: %c%c%c%c\n", msg[0], msg[1], msg[2], msg[3]);
    return SERVER_CLOSE_CONN;
  }

  const uint32_t actual_len = *(uint32_t *)(msg + 4);
  if (len != actual_len + 8) {
    fprintf(stderr, "Invalid lengths: expected %d, but got %lu\n", actual_len + 8, len);
  }

  fprintf(stderr, "Message: '%.*s'", actual_len, msg + 8);

  if (actual_len >= 4 && strncmp(msg + 8, "quit", 4) == 0) {
    fprintf(stderr, "[K808] Received quit request...\n");
    server_stop(srv);
  }

  return SERVER_CLOSE_CONN;
}

static struct k808 *k808;
static struct server *srv;

void signal_handler(int) {
  printf(" --- Exit signal received! --- \n");
  server_stop(srv);
  server_free(srv);
  k808_stop_sync(k808);
  k808_free(k808);
}

int main(void) {
  k808 = init_k808();
  struct k808_layer *layer = k808_add_layer(k808, "default");
#define X(k) k808_register_handler(layer, k, on_key, k808);
  K808_X_KEYS
#undef X
  if (k808_start_async(k808) != K808_RUNNING) return EXIT_FAILURE;

  srv = init_server(K808_SERVER, on_server_message, NULL);

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  server_run(srv);
  server_free(srv);

  k808_stop_sync(k808);
  k808_free(k808);
}