//
// Created by jay on 12/20/24.
//

#include "mutex.h"

#include <sched.h>
#include <stdlib.h>

struct mutex {
  int current_thread;
};

struct mutex *new_mutex(void) {
  struct mutex *res = malloc(sizeof(struct mutex));
  res->current_thread = -1;
  return res;
}

void mutex_acquire_sync(struct mutex *mutex, const int thread_id) {
  if (mutex->current_thread == thread_id) return;
  while (!__sync_bool_compare_and_swap(&mutex->current_thread, -1, thread_id)) {
    sched_yield();
  }
}

int mutex_acquire_async(struct mutex *mutex, const int thread_id) {
  if (mutex->current_thread == thread_id) return 1;
  return __sync_bool_compare_and_swap(&mutex->current_thread, -1, thread_id);
}

void mutex_release(struct mutex *mutex, const int thread_id) {
  if (mutex->current_thread == thread_id) {
    mutex->current_thread = -1;
  }
}

void free_mutex(struct mutex *mutex) {
  free(mutex);
}