#include "frontend.h"

SemFunction* sem_generate(Arena* arena, ParseTree parse_tree) {
  Scratch scratch = global_scratch(1, &arena);
  (void)scratch;

  PARSE_TREE_POSTORDER_ITER(parse_tree, node) {
    for_range(int, i, node->num_children) {
      
    }
  }

  return NULL;
}