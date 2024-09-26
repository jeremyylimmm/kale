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

  SourceContents source = load_source(arena, "examples/test.kale");
  TokenizedBuffer tokens = tokenize(arena, source);

  AST* ast = parse(arena, tokens);
  if (!ast) {
    return 1;
  }

  dump_ast(ast);

  return 0;
}