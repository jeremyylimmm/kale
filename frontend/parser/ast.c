#include <stdio.h>

#include "frontend.h"
#include "dynamic_array.h"

typedef struct {
  int depth;
  uint64_t* first_child;
  AST* node;
} IndentedItem;

static IndentedItem indented_item(Arena* arena, int depth, uint64_t* parent_first_child, bool is_first_child, AST* node) {
  uint64_t* first_child = arena_array(arena, uint64_t, bitset_num_u64(depth+1));
  memcpy(first_child, parent_first_child, bitset_num_u64(depth) * sizeof(uint64_t));

  if (is_first_child) {
    bitset_set(first_child, depth);
  }

  return (IndentedItem) {
    .depth = depth,
    .first_child = first_child,
    .node = node
  };
}

static void print_indentation(IndentedItem item) {
  for (int i = 1; i < item.depth+1; ++i) {
    if (bitset_query(item.first_child, i)) {
      printf("%c", i == item.depth ? 192 : ' ');
    }
    else {
      printf("%c", i == item.depth ? 195 : 179);
    }

    printf("%c", i == item.depth ? 196 : ' ');
  }
}

void ast_dump(AST* ast) {
  Scratch scratch = global_scratch(0, NULL);

  DynamicArray(IndentedItem) stack = new_dynamic_array(scratch.allocator);
  dynamic_array_put(stack, indented_item(
    scratch.arena,
    0,
    NULL,
    true,
    ast
  ));

  while (dynamic_array_length(stack)) {
    IndentedItem item = dynamic_array_pop(stack);
    AST* node = item.node;

    print_indentation(item);
    printf("%s: '%.*s'\n", ast_kind_string[node->kind], node->token.length, node->token.start);

    for_range_rev (int, i, node->num_children) {
      dynamic_array_put(stack, indented_item(
        scratch.arena,
        item.depth + 1,
        item.first_child,
        i == node->num_children-1,
        node->children[i]
      ));
    }
  }

  printf("\n");

  scratch_release(&scratch);
}