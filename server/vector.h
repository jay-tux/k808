//
// Created by jay on 12/22/24.
//

#ifndef VECTOR_H
#define VECTOR_H

struct vector;

typedef void (*free_data)(void *);

struct vector *init_vector(int elem_size);
void push_back(struct vector *vec, const void *data);
void *vector_at(const struct vector *vec, int index);
void *vector_last(const struct vector *vec);
int vector_size(const struct vector *vec);
void free_vector(struct vector *vec, free_data elem_free);

#endif //VECTOR_H
