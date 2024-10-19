#include <stdio.h>

#include "frontend.h"
#include "allocator.h"

static ScratchLibrary* global_scratch_lib;

Scratch global_scratch(int num_conflicts, Arena** conflicts) {
  return scratch_get(global_scratch_lib, num_conflicts, conflicts);
}

int main() {
  global_scratch_lib = new_scratch_library();

  Arena* arena = new_arena();

  char* source_path = "examples/test.kale";
  SourceContents source = load_source(arena, source_path);
  TokenizedBuffer tokens = tokenize(arena, source);
  (void)tokens;

  return 0;
}