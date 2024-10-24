#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "platform.h"

#define LENGTH(array) (sizeof(array)/sizeof((array)[0]))

#define for_range(type, it, end) for (type it = 0; it < (end); ++it)
#define for_range_rev(type, it, end) for (type it = (end)-1; it >= 0; --it)

#define for_list(type, it, start) for (type* it = (start); it; it = it->next)

inline void* offset_pointer(void* pointer, int64_t offset) {
  return (uint8_t*)pointer + offset;
} 

typedef struct {
  int length;
  char* str;
} String;

#define BIT(x) (1 << (x))

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

inline bool strings_ident(String a, String b) {
  if (a.length != b.length) {
    return false;
  }

  return memcmp(a.str, b.str, a.length * sizeof(a.str[0])) == 0;
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

inline uint64_t fnv1a_hash(void* data, size_t n) {
  uint64_t hash = 0xcbf29ce484222325;

  for_range(size_t, i, n) {
    uint8_t byte = ((uint8_t*)data)[i];
    hash ^= (uint64_t)(byte);
    hash *= 0x100000001b3;
  }

  return hash;
}