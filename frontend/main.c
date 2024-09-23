#include <stdio.h>

#include "frontend.h"
#include "allocator.h"
#include "dynamic_array.h"

int main() {
  Arena* arena = new_arena();
  Allocator* a = new_allocator(arena);

  DynamicArray(int) x = new_dynamic_array(a);

  for (int i = 0; i < 100; ++i) {
    dynamic_array_put(x, i);
  }

  while (dynamic_array_length(x)) {
    int i = dynamic_array_pop(x);
    printf("%d\n", i);
  }

  return 0;
}