//
// Created by jay on 12/22/24.
//

#include "k808_context.h"
#include "vector.h"
#include "mutex.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <signal.h>

// TODO: use mutexes

struct key_event_handler {
  k808_handler h;
  void *user_data;
};

struct k808_layer {
  char *name;
  struct key_event_handler handlers[K808_KEY_COUNT];
};

struct local_ctx {
  int raw_fd;
  struct libevdev *device;
};

struct thread_data {
  int thread_id;
  struct k808 *k808;
  char *raw_path;
};

struct k808 {
  pthread_t *threads;
  struct thread_data *args;
  int thread_count;

  struct vector *layers;
  int layer_idx;
  k808_layer_change on_switch;
  void *on_switch_data;

  FILE *logger;
  struct mutex *layers_lock;
  struct mutex *output_lock;

  int output_fd;
  volatile int exiting;
};

static void k808_log(FILE *fd, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(fd, fmt, args);
  va_end(args);
}

void free_layer(void *layer) {
  struct k808_layer *l = layer;
  free(l->name);
  free(l);
}

struct k808 *init_k808(void) {
  struct k808 *res = malloc(sizeof(struct k808));
  res->threads = NULL;
  res->args = NULL;
  res->thread_count = 0;

  res->layers = init_vector(sizeof(struct k808_layer));
  res->layer_idx = 0;
  res->on_switch = NULL;
  res->on_switch_data = NULL;

  res->logger = stderr; // TODO: replace by /var/log/k808.log
  if (res->logger == NULL) {
    perror("Failed to open /var/log/k808.log as log file");
    res->logger = stderr;
  }

  res->layers_lock = new_mutex();
  res->output_lock = new_mutex();

  res->output_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (res->output_fd < 0) {
    k808_log(res->logger, "[K808 ERROR]: Can't open /dev/uinput: %s\n", strerror(errno));
    fclose(res->logger);
    free_vector(res->layers, free_layer);
    return NULL;
  }

  struct uinput_setup setup = {
    .id = {
      .bustype = BUS_USB,
      .vendor = K808_REMAPPED_VENDOR,
      .product = K808_REMAPPED_PRODUCT
    },
    .name = "K808 [REMAP]"
  };
  ioctl(res->output_fd, UI_SET_EVBIT, EV_KEY);
  for (int i = 0; i < 256; i++) {
    ioctl(res->output_fd, UI_SET_KEYBIT, i);
  }

  if (ioctl(res->output_fd, UI_DEV_SETUP, &setup) < 0 || ioctl(res->output_fd, UI_DEV_CREATE) < 0) {
    k808_log(res->logger, "[K808 ERROR]: Can't create uinput device: %s\n", strerror(errno));
    fclose(res->logger);
    free_vector(res->layers, free_layer);
    close(res->output_fd);
    return 0;
  }

  k808_log(res->logger, "[K808 INFO]: Opened /dev/uinput as fd %d.\n", res->output_fd);
  k808_log(res->logger, "[K808 INFO]: Created uinput device at %x:%x as %s.\n", setup.id.vendor, setup.id.product, setup.name);

  res->exiting = 0;

  return res;
}

struct k808_layer *k808_add_layer(const struct k808 *k808, const char *layer_name) {
  struct k808_layer *layer = malloc(sizeof(struct k808_layer));
  layer->name = strdup(layer_name);
  for (int i = 0; i < K808_KEY_COUNT; i++) {
    layer->handlers[i].h = NULL;
    layer->handlers[i].user_data = NULL;
  }

  push_back(k808->layers, layer);
  return vector_last(k808->layers);
}

int k808_layer_count(const struct k808 *k808) {
  return vector_size(k808->layers);
}

struct k808_layer *k808_nth_layer(const struct k808 *k808, const int n) {
  return vector_at(k808->layers, n);
}

struct k808_layer *k808_current_layer(const struct k808 *k808) {
  return k808_nth_layer(k808, k808_current_layer_idx(k808));
}

int k808_current_layer_idx(const struct k808 *k808) {
  return k808->layer_idx;
}

void k808_register_handler(struct k808_layer *layer, const enum k808_key key, const k808_handler handler, void *user_data) {
  if (layer == NULL) {
    return;
  }

  layer->handlers[key].h = handler;
  layer->handlers[key].user_data = user_data;
}

void k808_register_layer_switch_handler(struct k808 *k808, const k808_layer_change handler, void *user_data) {
  k808->on_switch = handler;
  k808->on_switch_data = user_data;
}

static void handle_key(const struct k808 *k808, const int thread_id, const int ev_key, const int ev_value) {
  const struct k808_layer *curr = vector_at(k808->layers, k808->layer_idx);
  if (curr == NULL) return;

  const enum k808_event event = ev_value ? K808_KEY_PRESS : K808_KEY_RELEASE;
  enum k808_key key;
  switch (ev_key) {
    case 79: case 30: key = K808_1; break;
    case 80: case 48: key = K808_2; break;
    case 81: case 46: key = K808_3; break;
    case 75: case 32: key = K808_4; break;
    case 76: case 18: key = K808_5; break;
    case 77: case 33: key = K808_6; break;
    case 71: case 36: key = K808_7; break;
    case 72: case 38: key = K808_8; break;
    case 73: case 50: key = K808_9; break;
    case 82: case 37: key = K808_0; break;
    case 83: key = K808_DOT; break;
    case 96: key = K808_ENTER; break;

    default:
      k808_log(k808->logger, "[Thread %02d]: Unknown key code %d.\n", thread_id, ev_key);
      return;
  }

  if (curr->handlers[key].h == NULL) {
    k808_log(k808->logger, "[Thread %02d]: No handler for key %d.\n", thread_id, key);
    return;
  }

  curr->handlers[key].h(key, event, curr->handlers[key].user_data);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef // pthread_create requires non-const void *
// ReSharper disable once CppDFAConstantFunctionResult // duh, we have no useful return value
static void *thread_driver(void *_args) {
  // arguments
  const struct thread_data *args = _args;
  const int thread_id = args->thread_id;
  const struct k808 *k808 = args->k808;
  const char *raw_path = args->raw_path;
  struct local_ctx local = { 0 };

  // setup
  local.raw_fd = open(raw_path, O_RDONLY | O_NONBLOCK);
  if (local.raw_fd < 0) {
    k808_log(k808->logger, "[Thread %02d]: Can't open %s: %s\n", thread_id, raw_path, strerror(errno));
    return NULL;
  }
  k808_log(k808->logger, "[Thread %02d]: Opened input device at %s as fd %d.\n", thread_id, raw_path, local.raw_fd);

  local.device = NULL;
  if (libevdev_new_from_fd(local.raw_fd, &local.device) < 0) {
    k808_log(k808->logger, "[Thread %02d]: Can't create libevdev from %s (fd %d): %s\n", thread_id, raw_path, local.raw_fd, strerror(errno));
    libevdev_free(local.device);
    close(local.raw_fd);
    return NULL;
  }

  if (libevdev_grab(local.device, LIBEVDEV_GRAB) < 0) {
    k808_log(k808->logger, "[Thread %02d]: Warning - can't grab input device %s: %s\n", thread_id, libevdev_get_name(local.device), strerror(errno));
  }

  k808_log(k808->logger, "[Thread %02d]: Initialized libevdev for input device %s.\n", thread_id, libevdev_get_name(local.device));

  // event loop
  struct input_event ev;
  while (!k808->exiting) {
    const int rc = libevdev_next_event(local.device, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
      k808_log(k808->logger, "[Thread %02d]: (%ld) Received { type = %d (%s); code = %d (%s); value = %d }.\n",
        thread_id, ev.time.tv_usec,
        ev.type, libevdev_event_type_get_name(ev.code),
        ev.code, libevdev_event_code_get_name(ev.type, ev.code),
        ev.value
      );

      if (ev.type == EV_KEY) handle_key(k808, thread_id, ev.code, ev.value);
      else if (ev.type == EV_REL) {
        // TODO
      }
    }
    else if (rc != -EAGAIN) {
      k808_log(k808->logger, "[Thread %02d]: Failed to read event: %s\n", thread_id, strerror(-rc));
      break;
    }
  }

  // cleanup
  k808_log(k808->logger, "[Thread %02d]: Cleaning up.\n", thread_id);
  libevdev_free(local.device);
  close(local.raw_fd);
  return NULL;
}

static struct vector *possible_devices(FILE *logger) {
  static char cmd[256];
  static char line[1024];

  snprintf(cmd, sizeof(cmd),
    "grep -l '%s' /sys/class/input/event*/device/uevent | "
    "xargs grep -l '%s' | "
    "sed 's#/sys/class/input/event\\([0-9]*\\)/device/uevent#/dev/input/event\\1#'",
    K808_VENDOR_ID, K808_PRODUCT_ID
  );
  k808_log(logger, "[K808 INFO]: Running command %s.\n", cmd);
  FILE *fp = popen(cmd, "r");
  if (fp == NULL) {
    k808_log(logger, "[K808 ERROR]: Failed to find device for %s:%s\n", K808_VENDOR_ID, K808_PRODUCT_ID);
    return init_vector(sizeof(const char *));
  }

  struct vector *res = init_vector(sizeof(const char *));
  while (fgets(line, sizeof(line), fp) != NULL) {
    char *data = strndup(line, strcspn(line, "\n"));
    push_back(res, &data);
    k808_log(logger, "[K808 INFO]: Found device #%d at %s.\n", vector_size(res), *(char **)vector_last(res));
  }

  return res;
}

static void free_indirect(void *d) {
  free(*(void **)d);
}

enum k808_start_result k808_start_async(struct k808 *k808) {
  if (k808 == NULL) return K808_NO_CTX;
  if (k808->thread_count > 0) {
    k808_log(k808->logger, "K808 driver already running.");
    return K808_ALREADY_RUNNING;
  }
  if (vector_size(k808->layers) == 0) {
    k808_log(k808->logger, "No layers to activate.");
    return K808_NO_LAYERS;
  }

  struct vector *devices = possible_devices(k808->logger);
  if (vector_size(devices) == 0) {
    k808_log(k808->logger, "No devices matching %s:%s found.", K808_VENDOR_ID, K808_PRODUCT_ID);
    return K808_NO_DEVICES;
  }

  k808->thread_count = vector_size(devices);
  k808->threads = malloc(k808->thread_count * sizeof(pthread_t));
  k808->args = malloc(k808->thread_count * sizeof(struct thread_data));

  for (int i = 0; i < k808->thread_count; i++) {
    k808->args[i].k808 = k808;
    k808->args[i].thread_id = i;
    k808->args[i].raw_path = strdup(*(char **)vector_at(devices, i));
    pthread_create(k808->threads + i, NULL, &thread_driver, k808->args + i);
  }

  free_vector(devices, free_indirect);

  return K808_RUNNING;
}

void k808_stop_sync(struct k808 *k808) {
  if (k808 == NULL || k808->thread_count == 0) return;

  k808->exiting = 1;
  for (int i = 0; i < k808->thread_count; i++) {
    pthread_join(k808->threads[i], NULL);
  }
}

void k808_free(struct k808 *k808) {
  free(k808->threads);
  free(k808->args);
  free_vector(k808->layers, free_layer);
  if (k808->logger != stderr) fclose(k808->logger);
  free_mutex(k808->layers_lock);
  free_mutex(k808->output_lock);
  close(k808->output_fd);
  free(k808);
}

void send_keys(const struct k808 *k808, const struct key_event *keys, const int count) {
  k808_log(k808->logger, "Sending %d key events.\n", count);
  struct input_event ev = {0};
  ev.type = EV_KEY;

  for (int i = 0; i < count; i++) {
    ev.code = keys[i].key;
    ev.value = keys[i].is_key_press;
    k808_log(k808->logger, "Sending { type = %d; code = %d; value = %d }.\n", ev.type, ev.code, ev.value);
    write(k808->output_fd, &ev, sizeof(ev));
  }

  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  k808_log(k808->logger, "Sending { type = %d; code = %d; value = %d }.\n", ev.type, ev.code, ev.value);
  write(k808->output_fd, &ev, sizeof(ev));
}