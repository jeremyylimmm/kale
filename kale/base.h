#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "platform.h"

#define LENGTH(array) (sizeof(array)/sizeof((array)[0]))

#define for_range(type, it, end) for (type it = 0; it < (end); ++it)

inline void* offset_pointer(void* pointer, int64_t offset) {
  return (uint8_t*)pointer + offset;
} 

typedef struct {
  int length;
  char* str;
} String;

inline String copy_cstr(Arena* arena, char* str) {
  int length = (int)strlen(str);

  char* buffer = arena_push(arena, (length + 1) * sizeof(char));
  memcpy(buffer, str, length * sizeof(char));
  buffer[length] = '\0';

  return (String) {
    .length = length,
    .str = buffer
  };
}

inline size_t bitset_num_u64(size_t num_bits) {
  return (num_bits + 63) / 64;
}

inline bool bitset_query(uint64_t* set, size_t index) {
  return (set[index/64] >> (index%64)) & 1;
} 

inline void bitset_set(uint64_t* set, size_t index) {
  set[index/64] |= (uint64_t)1 << (index % 64);
}

inline void bitset_unset(uint64_t* set, size_t index) {
  set[index/64] &= ~((uint64_t)1 << (index % 64));
}