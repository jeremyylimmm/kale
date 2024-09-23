#pragma once

#include <stdint.h>

typedef struct Arena Arena;

Arena* new_arena();
void free_arena(Arena* arena);

void* arena_push(Arena* arena, size_t amount);
void* arena_push_zeroed(Arena* arena, size_t amount);

#define arena_array(arena, type, count) ((type*)arena_push_zeroed(arena, sizeof(type) * (count)))
#define arena_type(arena, type) arena_array(arena, type, 1)

int lzcnt64(uint64_t number);
int tzcnt64(uint64_t number);