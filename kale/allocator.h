#pragma once

#include "base.h"

typedef struct Allocator Allocator;
Allocator* new_allocator(Arena* arena);

void* allocator_alloc(Allocator* a, uint64_t amount);
void allocator_free(Allocator* a, void* pointer);