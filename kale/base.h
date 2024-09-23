#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "platform.h"

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