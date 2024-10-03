#pragma once

#include <stdint.h>

static uint64_t fnv1a_hash(void* data, size_t len) {
  uint64_t hash = 0xcbf29ce484222325;

  for (size_t i = 0; i < len; ++i) {
    uint8_t byte = ((uint8_t*)data)[i];
    hash ^= (uint64_t)byte;
    hash *= 0x100000001b3;
  }

  return hash;
}
