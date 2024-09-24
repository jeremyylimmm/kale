#pragma once

#include "allocator.h"

#define DynamicArray(T) T*

void* new_dynamic_array(Allocator* allocator);
void* _dynamic_array_put(void* da, size_t stride);

int dynamic_array_length(void* da);
int _dynamic_array_pop(void* da);

void* _dynamic_array_bake(Arena* arena, void* da, size_t stride);

#define dynamic_array_put(da, item) ( *(void**)&(da) = _dynamic_array_put(da, sizeof(*(da))), (da)[dynamic_array_length(da)-1] = (item), (void)0 )
#define dynamic_array_pop(da) ( (da)[_dynamic_array_pop(da)] )
#define dynamic_array_bake(arena, da) _dynamic_array_bake(arena, da, sizeof(*(da)))