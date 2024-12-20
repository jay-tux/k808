//
// Created by jay on 12/19/24.
//

#include "driver.h"

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

struct handler {
  key_handler h;
  void *user_data;
};

struct layer {
  char *name;
  struct handler handlers[K808_KEY_COUNT];
};

typedef struct loop_node {
  struct layer l;
  struct loop_node *next;
} node;

typedef struct layer_loop {
  struct loop_node *current;
  struct loop_node *tail;
} loop;

typedef struct local_ctx {
  const char *raw_path;
  int raw_fd;
  struct libevdev *device;
  int mapped_fd;
} local;

typedef struct mutex {
  int taken;
} mtx;

struct context {
  local *per_thread;
  pthread_t *threads;
  int thread_count;
  loop layers;
  FILE *logger;
  mtx lock;
  struct thread_args *args;
  int mapped_fd;
  volatile int quitting;
};

struct thread_args {
  int thread_id;
  struct context *ctx;
};

struct string_vec {
  char **data;
  int count;
};

struct source {
  const char *layer_name;
  int thread_id;
  struct context *ctx;
};

static void k808_log(FILE *fd, const char *file, const int line, const char *fmt, ...) {
  // fprintf(fd, "%8s:%03d: ", file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(fd, fmt, args);
  va_end(args);
}

#define LOG_TO(fd, fmt, ...) k808_log(fd, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

static void init_layer(struct layer *l, const char *name) {
  l->name = strdup(name);
  for (enum k808_key key = K808_0; key < K808_KEY_COUNT; key++) {
    l->handlers[key].h = NULL;
    l->handlers[key].user_data = NULL;
  }
}

static loop alloc_layer_loop() {
  const loop l = {NULL, NULL};
  return l;
}

static struct layer *loop_add_layer(loop *l, const char *name) {
  if (l->current == NULL) {
    l->current = malloc(sizeof(node));
    init_layer(&l->current->l, name);
    l->current->next = l->current;
    l->tail = l->current;
    return &l->current->l;
  }

  node *new = malloc(sizeof(node));
  init_layer(&new->l, name);
  new->next = l->tail->next;
  l->tail->next = new;
  l->tail = new;
  return &new->l;
}

static void delete_layer_loop(loop *l) {
  if (l == NULL || l->current == NULL || l->tail == NULL) {
    return;
  }

  node *curr = l->current;
  const node *end = l->current;
  do {
    node *next = curr->next;
    free(curr->l.name);
    free(curr);
    curr = next;
  } while (curr != end);

  l->current = NULL;
  l->tail = NULL;
}

static mtx new_mutex() {
  const mtx res = { .taken = -1 };
  return res;
}

static void mutex_acquire(mtx *mutex, const int thread_id) {
  if (mutex->taken == thread_id) return;
  while (!__sync_bool_compare_and_swap(&mutex->taken, -1, thread_id)) {
    sched_yield();
  }
}

static void mutex_release(mtx *mutex, const int thread_id) {
  if (mutex->taken == thread_id) {
    mutex->taken = -1;
  }
}

static struct context *global_ctx = NULL;
static int in_handler = 0;

void handle_signal(const int sig) {
  printf(" --- SIGNAL %d ---\n", sig);
  if (global_ctx == NULL) {
    printf("  -> No context, exiting.\n");
    exit(0);
  }
  if (in_handler) {
    printf("  -> Duplicate signal, force quitting now.\n");
    exit(-1);
  }

  in_handler = 1;
  global_ctx->quitting = 1;
  for (int i = 0; i < global_ctx->thread_count; i++) {
    pthread_join(global_ctx->threads[i], NULL);
  }
  ctx_free(global_ctx);
  exit(0);
}

struct context *init_ctx(void) {
  if (global_ctx != NULL) return global_ctx;

  struct context *res = malloc(sizeof(struct context));
  res->per_thread = NULL;
  res->thread_count = 0;
  res->layers = alloc_layer_loop();
  // res->logger = fopen("/var/log/k808.log", "w");
  res->logger = stderr;
  if (res->logger == NULL) {
    perror("Failed to open /var/log/k808.log as log file");
    res->logger = stderr;
  }
  res->lock = new_mutex();
  res->args = NULL;

  res->mapped_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (res->mapped_fd < 0) {
    LOG_TO(res->logger, "[K808 ERROR]: Can't open /dev/uinput: %s\n", strerror(errno));
    fclose(res->logger);
    delete_layer_loop(&res->layers);
    return 0;
  }

  struct uinput_setup setup = {
    .id = {
      .bustype = BUS_USB,
      .vendor = REMAP_VENDOR,
      .product = REMAP_PRODUCT
    },
    .name = "K808 [REMAP]"
  };
  ioctl(res->mapped_fd, UI_SET_EVBIT, EV_KEY);
  for (int i = 0; i < 256; i++) {
    ioctl(res->mapped_fd, UI_SET_KEYBIT, i);
  }

  if (ioctl(res->mapped_fd, UI_DEV_SETUP, &setup) < 0 || ioctl(res->mapped_fd, UI_DEV_CREATE) < 0) {
    LOG_TO(res->logger, "[K808 ERROR]: Can't create uinput device: %s\n", strerror(errno));
    fclose(res->logger);
    delete_layer_loop(&res->layers);
    close(res->mapped_fd);
    return 0;
  }

  LOG_TO(res->logger, "[K808 INFO]: Created uinput device at %x:%x as %s.\n", setup.id.vendor, setup.id.product, setup.name);

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
  global_ctx = res;

  return res;
}

struct layer *ctx_add_layer(struct context *ctx, const char *layer_name) {
  return loop_add_layer(&ctx->layers, layer_name);
}

void layer_register_override(struct layer *layer, const enum k808_key key, const key_handler handler, void *user_data) {
  if (layer == NULL) {
    return;
  }

  layer->handlers[key].h = handler;
  layer->handlers[key].user_data = user_data;
}

static struct string_vec possible_devices(FILE *logger) {
  static char cmd[256];
  static char line[1024];

  snprintf(cmd, sizeof(cmd),
    "grep -l '%s' /sys/class/input/event*/device/uevent | "
    "xargs grep -l '%s' | "
    "sed 's#/sys/class/input/event\\([0-9]*\\)/device/uevent#/dev/input/event\\1#'",
    USB_VID, USB_PID
  );
  LOG_TO(logger, "[K808 INFO]: Running command %s.\n", cmd);
  FILE *fp = popen(cmd, "r");
  if (fp == NULL) {
    LOG_TO(logger, "[K808 ERROR]: Failed to find device for %s:%s\n", USB_VID, USB_PID);
    const struct string_vec result = { .data = NULL, .count = 0 };
    return result;
  }

  struct string_vec res = {
    .data = malloc(16 * sizeof(char *)),
    .count = 0
  };
  int capacity = 16;
  while (fgets(line, sizeof(line), fp) != NULL) {
    res.count++;
    if (res.count > capacity) {
      char **dup = realloc(res.data, 2 * capacity * sizeof(char *));
      if (dup == NULL) break;
      capacity *= 2;
      res.data = dup;
    }

    res.data[res.count - 1] = strndup(line, strcspn(line, "\n"));
    LOG_TO(logger, "[K808 INFO]: Found device #%d at %s. (capacity: %d)\n", res.count, res.data[res.count - 1], capacity);
  }

  char **dup = realloc(res.data, res.count * sizeof(char *));
  if (dup != NULL) {
    res.data = dup;
  }

  return res;
}

static void clear_string_vec(const struct string_vec *sv) {
  for (int i = 0; i < sv->count; i++) {
    free(sv->data[i]);
  }
  free(sv->data);
}

static int open_device(const int thread_id, local *l, FILE *logger) {
  l->raw_fd = open(l->raw_path, O_RDONLY | O_NONBLOCK);
  if (l->raw_fd < 0) {
    LOG_TO(logger, "[Thread %02d]: Can't open %s: %s\n", thread_id, l->raw_path, strerror(errno));
    return 0;
  }
  LOG_TO(logger, "[Thread %02d]: Opened input device at %s as fd %d.\n", thread_id, l->raw_path, l->raw_fd);

  l->device = NULL;
  if (libevdev_new_from_fd(l->raw_fd, &l->device) < 0) {
    LOG_TO(logger, "[Thread %02d]: Can't create libevdev from %s (fd %d): %s\n", thread_id, l->raw_path, l->raw_fd, strerror(errno));
    libevdev_free(l->device);
    close(l->raw_fd);
    return 0;
  }
  
  if (libevdev_grab(l->device, LIBEVDEV_GRAB) < 0) {
    LOG_TO(logger, "[Thread %02d]: Warning - can't grab input device %s: %s\n", thread_id, libevdev_get_name(l->device), strerror(errno));
  }

  LOG_TO(logger, "[Thread %02d]: Initialized libevdev for input device %s.\n", thread_id, libevdev_get_name(l->device));

  return 1;
}

static void init_local_ctx(struct local_ctx *l, const char *path) {
  l->raw_path = strdup(path);
  l->raw_fd = -1;
  l->device = NULL;
  l->mapped_fd = -1;
}

static void handle_key_event(const struct layer *current, const struct source *src, FILE *logger, const __u16 raw_key, const int press) {
  if (current == NULL) return;

  enum k808_event_type type = press ? K808_PRESS : K808_RELEASE;
  enum k808_key key;
  switch (raw_key) {
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
      LOG_TO(logger, "[K808 ERROR]: Unknown key code %d.\n", raw_key);
      return;
  }

  if (current->handlers[key].h != NULL) {
    current->handlers[key].h(src, key, type, current->handlers[key].user_data);
  }
}

static void *thread_driver(void *_args) { // can't be const void * because of pthread_create
  const struct thread_args *args = _args;

  struct context *c = args->ctx;
  const int thread_id = args->thread_id;
  local *l = &c->per_thread[thread_id];

  if (!open_device(thread_id, l, c->logger)) {
    LOG_TO(c->logger, "[Thread %02d]: Failed to open device %s.\n", thread_id, l->raw_path);
    return NULL;
  }

  LOG_TO(c->logger, "[Thread %02d]: Successfully opened device %s.\n", thread_id, l->raw_path);

  struct source *src = malloc(sizeof(struct source));
  src->thread_id = thread_id;
  src->ctx = c;

  struct input_event ev;
  while (!c->quitting) {
    // LOG_TO(c->logger, "[Thread %02d]: Waiting for event...\n", thread_id);
    const int rc = libevdev_next_event(l->device, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
      LOG_TO(c->logger, "[Thread %02d]: (%ld) Received event { type %d (%s); code %d (%s), value %d }.\n",
          thread_id, ev.time.tv_usec,
          ev.type, libevdev_event_type_get_name(ev.code),
          ev.code, libevdev_event_code_get_name(ev.type, ev.code),
          ev.value);
      if (ev.type == EV_KEY || ev.type == EV_REL) {
        if (ev.type == EV_KEY) {
          src->layer_name = c->layers.current->l.name;
          handle_key_event(&c->layers.current->l, src, c->logger, ev.code, ev.value);
        }
      }
    }
    else if (rc != -EAGAIN) {
      LOG_TO(c->logger, "[Thread %02d]: Failed to read event: %s\n", thread_id, strerror(-rc));
      break;
    }
  }

  free(src);
  LOG_TO(c->logger, "[Thread %02d]: Exiting thread.\n", thread_id);

  return NULL;
}

void ctx_activate(struct context *ctx) {
  if (ctx == NULL) return;
  if (ctx->thread_count > 0) {
    LOG_TO(ctx->logger, "Context already activated (%d running threads).\n", ctx->thread_count);
    return;
  }
  if (ctx->layers.current == NULL) {
    LOG_TO(ctx->logger, "No layers to activate.\n");
    return;
  }

  const struct string_vec devices = possible_devices(ctx->logger);
  if (devices.count == 0) {
    clear_string_vec(&devices);
    return;
  }

  ctx->thread_count = devices.count;
  ctx->per_thread = malloc(devices.count * sizeof(local));
  ctx->threads = malloc(devices.count * sizeof(pthread_t));
  ctx->args = malloc(devices.count * sizeof(struct thread_args));
  for (int i = 0; i < devices.count; i++) {
    init_local_ctx(ctx->per_thread + i, devices.data[i]);
    ctx->args[i].thread_id = i;
    ctx->args[i].ctx = ctx;
    pthread_create(ctx->threads + i, NULL, &thread_driver, ctx->args + i);
  }
}

void ctx_wait(const struct context *ctx) {
  if (ctx == NULL) return;

  for (int i = 0; i < ctx->thread_count; i++) {
    pthread_join(ctx->threads[i], NULL);
  }
}

void ctx_free(struct context *ctx) {
  if (ctx == NULL) return;

  for (int i = 0; i < ctx->thread_count; i++) {
    libevdev_grab(ctx->per_thread[i].device, LIBEVDEV_UNGRAB);
    libevdev_free(ctx->per_thread[i].device);
    close(ctx->per_thread[i].raw_fd);
    close(ctx->per_thread[i].mapped_fd);
  }

  delete_layer_loop(&ctx->layers);
  free(ctx->per_thread);
  free(ctx->threads);
  free(ctx->args);

  if (ctx->logger != stderr && ctx->logger != stdout && ctx->logger != NULL) {
    fclose(ctx->logger);
  }

  free(ctx);
}

static void send_event(const int fd, const uint16_t type, const uint16_t code, const int32_t value) {
  struct input_event ev = {0};
  ev.type = type;
  ev.code = code;
  ev.value = value;
  write(fd, &ev, sizeof(ev));
}

void handle_send_keys(const struct source *src, const uint16_t *keys, const int count, const int is_reverse, const int is_key_up) {
  mutex_acquire(&src->ctx->lock, src->thread_id);
  if (is_reverse) {
    for (int i = count; i > 0; i--) {
      send_event(src->ctx->mapped_fd, EV_KEY, keys[i - 1], !is_key_up);
    }
  }
  else {
    for (int i = 0; i < count; i++) {
      send_event(src->ctx->mapped_fd, EV_KEY, keys[i], !is_key_up);
    }
  }
  send_event(src->ctx->mapped_fd, EV_SYN, SYN_REPORT, 0);
  mutex_release(&src->ctx->lock, src->thread_id);
}