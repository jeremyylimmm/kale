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

  AST* ast = parse(arena, source, &tokens);
  if (!ast) {
    return 1;
  }

  ast_dump(ast);

  //SemContext* sem = sem_init(arena);
  //SemFile* sem_file = check_ast(sem, ast_buffer);
  //sem_dump(sem_file);

  return 0;
}