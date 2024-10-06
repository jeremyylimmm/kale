#include "dynamic_array.h"

typedef struct {
  int capacity;
  int length;
  Allocator* allocator;
} Header;

#define INITIAL_CAPACITY 8

static Header* header(void* da) {
  assert(da);
  return (Header*)da - 1;
}

void* new_dynamic_array(Allocator* allocator) {
  Header* header = allocator_alloc(allocator, sizeof(Header));
  header->capacity = 0;
  header->length = 0;
  header->allocator = allocator;

  return header + 1;
}

static size_t allocation_size(int capacity, size_t stride) {
  return sizeof(Header) + capacity * stride;
}

void* _dynamic_array_put(void* da, size_t stride) {
  Header* h = header(da);

  if (h->length == h->capacity) {
    int new_capacity = h->capacity ? h->capacity * 2 : INITIAL_CAPACITY;

    size_t old_size = allocation_size(h->capacity, stride);
    size_t new_size = allocation_size(new_capacity, stride);

    Header* h2 = allocator_alloc(h->allocator, new_size);
    memcpy(h2, h, old_size);

    allocator_free(h->allocator, h);
    h = h2;

    h->capacity = new_capacity;
  }

  h->length++;
  return h + 1;
}

int dynamic_array_length(void* da) {
  return header(da)->length;
}

int _dynamic_array_pop(void* da) {
  assert(dynamic_array_length(da));
  return --(header(da)->length);
}

void dynamic_array_clear(void* da) {
  header(da)->length = 0;
}

void* _dynamic_array_bake(Arena* arena, void* da, size_t stride) {
  size_t length = header(da)->length;
  size_t size = length * stride;

  void* buffer = arena_push(arena, size);
  memcpy(buffer, da, size);

  return buffer;
};