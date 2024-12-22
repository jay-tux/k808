//
// Created by jay on 12/22/24.
//

#include "vector.h"

#include <stdlib.h>
#include <string.h>

struct vector {
  int elem_size;
  int capacity;
  int size;
  void *data;
};

struct vector *init_vector(const int elem_size) {
  struct vector *vec = malloc(sizeof(struct vector));
  if (vec == NULL) {
    return NULL;
  }

  vec->elem_size = elem_size;
  vec->capacity = 8;
  vec->size = 0;
  vec->data = malloc(8 * elem_size);
  if (vec->data == NULL) {
    free(vec);
    return NULL;
  }

  return vec;
}

void push_back(struct vector *vec, const void *data) {
  if (vec->size == vec->capacity) {
    vec->capacity *= 2;
    void *copy = realloc(vec->data, vec->capacity * vec->elem_size);
    if (copy != NULL) {
      vec->data = copy;
    }
  }

  memcpy(vec->data + vec->size * vec->elem_size, data, vec->elem_size);
  vec->size++;
}

void *vector_at(const struct vector *vec, const int index) {
  if (index < 0 || index >= vec->size) {
    return NULL;
  }

  return vec->data + index * vec->elem_size;
}

void *vector_last(const struct vector *vec) {
  return vector_at(vec, vec->size - 1);
}

int vector_size(const struct vector *vec) {
  return vec->size;
}

void free_vector(struct vector *vec, const free_data elem_free) {
  for (int i = 0; i < vec->size; i++) {
    elem_free(vector_at(vec, i));
  }
  free(vec->data);
  free(vec);
}