#pragma once

#include <stdlib.h>
#include <stddef.h>

typedef struct {
  int capacity;
  int count;
} VecHeader;

static VecHeader* _vec_header(void* v) {
  return (VecHeader*)v - 1;
}

static void* _vec_put(void* v, size_t stride) {
  VecHeader* h = NULL;

  if (!v) {
    h = malloc(sizeof(VecHeader) + 8 * stride);
    h->capacity = 8;
    h->count = 0;
  }
  else {
    h = _vec_header(v);
  }

  if (h->count == h->capacity) {
    h->capacity *= 2;
    h = realloc(h, sizeof(VecHeader) + h->capacity * stride);
  }

  h->count++;

  return h + 1;
}

static int vec_length(void* v) {
  if (!v) {
    return 0;
  }

  return _vec_header(v)->count;
}

static int _vec_pop(void* v) {
  assert(vec_length(v));
  return --_vec_header(v)->count;
}

static void vec_free(void* v) {
  if (v) {
    free(_vec_header(v));
  }
}

static void vec_clear(void* v) {
  if (v) {
    _vec_header(v)->count = 0;
  }
}

#define Vec(T) T*

#define vec_put(v, x) ( (*(void**)&(v)) = _vec_put(v, sizeof((v)[0])), (v)[vec_length(v)-1] = (x) )
#define vec_pop(v) ((v)[_vec_pop(v)])