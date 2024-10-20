#include "frontend.h"

SemContext* sem_init(Arena* arena) {
  SemContext* ctx = arena_type(arena, SemContext);
  ctx->arena = arena;
  ctx->allocator = new_allocator(arena);
  return ctx;
}