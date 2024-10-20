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

void ast_dump(ASTBuffer* ast_buffer) {
  Scratch scratch = global_scratch(0, NULL);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  DynamicArray(IndentedItem) stack = new_dynamic_array(scratch_allocator);
  IndentedItem* data = arena_array(scratch.arena, IndentedItem, ast_buffer->count);

  // Gather all roots
  for (
    int cur = ast_buffer->count-1;
    cur >= 0;
    cur -= ast_buffer->nodes[cur].subtree_size
  ) {
    dynamic_array_put(stack, indented_item(scratch.arena, 0, NULL, true, &ast_buffer->nodes[cur]));
  }

  while (dynamic_array_length(stack)) {
    IndentedItem item = dynamic_array_pop(stack);

    AST* node = item.node;
    data[node-ast_buffer->nodes] = item;

    foreach_ast_child(node, child) {
      dynamic_array_put(stack, indented_item(
        scratch.arena,
        item.depth + 1,
        item.first_child,
        child.index == 0,
        child.node
      ));
    }
  }

  for_range(int, node_idx, ast_buffer->count) {
    AST* node = &ast_buffer->nodes[node_idx];
    IndentedItem item = data[node_idx];

    for (int i = 1; i < item.depth+1; ++i) {
      if (bitset_query(item.first_child, i)) {
        printf("%c", i == item.depth ? 218 : ' ');
      }
      else {
        printf("%c", i == item.depth ? 195 : 179);
      }

      printf("%c", i == item.depth ? 196 : ' ');
    }

    printf("%s: '%.*s'\n", ast_kind_string[node->kind], node->token.length, node->token.start);
  }

  scratch_release(&scratch);
}

ASTChildIterator ast_children_begin(AST* node) {
  return (ASTChildIterator) {
    .index = node->num_children-1,
    .node = node - 1
  };
}

bool ast_children_check(ASTChildIterator* it) {
  return it->index >= 0;
}

void ast_children_next(ASTChildIterator* it) {
  it->node -= it->node->subtree_size;
  it->index--;
}