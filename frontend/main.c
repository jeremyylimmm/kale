#include <stdio.h>

#include "frontend.h"

static ScratchLibrary* global_scratch_lib;

Scratch global_scratch(int num_conflicts, Arena** conflicts) {
  return scratch_get(global_scratch_lib, num_conflicts, conflicts);
}

int main() {
  global_scratch_lib = new_scratch_library();

  Arena* arena = new_arena();

  SourceContents source = load_source(arena, "examples/test.kale");
  TokenizedBuffer tokens = tokenize(arena, source);

  for_range(int, i, tokens.length) {
    Token token = tokens.tokens[i];
    printf("<%d (line %d): '%.*s'>\n", token.kind, token.line, token.length, token.start);
  }

  return 0;
}