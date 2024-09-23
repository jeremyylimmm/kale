#include <stdio.h>

#include "frontend.h"
#include "allocator.h"

int main() {
  //char* source_path = "examples/test.kale";
  //SourceContents source = load_source(arena, source_path);

  //printf("%s\n", source.contents);

  Arena* arena = new_arena();
  Allocator* a = new_allocator(arena);

  allocator_free(a, allocator_alloc(a, 1000));

  int n = 10;
  int** v = allocator_alloc(a, n * 2 * sizeof(int*));

  for (int i = 0; i < n; ++i) {
    v[i] = allocator_alloc(a, sizeof(int));
    *v[i] = i;
  }

  for (int i = 0; i < n; ++i)
    printf("%d\n", *v[i]);

  for (int i = 0; i < n; ++i)
    allocator_free(a, v[i]);

  for (int i = 0; i < n * 2; ++i) {
    v[i] = allocator_alloc(a, sizeof(int));
    *v[i] = i;
  }

  for (int i = 0; i < n * 2; ++i)
    printf("%d\n", *v[i]);

  free_arena(arena);

  return 0;
}