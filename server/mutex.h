//
// Created by jay on 12/20/24.
//

#ifndef MUTEX_H
#define MUTEX_H

struct mutex;

struct mutex *new_mutex(void);
void mutex_acquire_sync(struct mutex *mutex, int thread_id);
int mutex_acquire_async(struct mutex *mutex, int thread_id);
void mutex_release(struct mutex *mutex, int thread_id);
void free_mutex(struct mutex *mutex);

#endif //MUTEX_H
