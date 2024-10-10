#include <stdio.h>

#include "frontend.h"
#include "dynamic_array.h"

typedef struct {
  int depth;
  uint64_t* last_child;
  ParseNode* node;
} Item;

static uint64_t* alloc_last_child(Allocator* allocator, uint64_t* prev_last_child, int tree_count) {
  size_t alloc_size = bitset_num_u64(tree_count) * sizeof(uint64_t);

  uint64_t* last_child = allocator_alloc(allocator, alloc_size);

  if (prev_last_child) {
    memcpy(last_child, prev_last_child, alloc_size);
  } 
  else {
    memset(last_child, 0, alloc_size);
  }

  return last_child;
}

static Item make_item(Allocator* allocator, int tree_count, uint64_t* prev_last_child, int depth, ParseNode* node, bool is_last_child) {
  uint64_t* last_child = alloc_last_child(allocator, prev_last_child, tree_count);

  if (is_last_child) {
    bitset_set(last_child, depth);
  }

  return (Item) {
    .depth = depth,
    .last_child = last_child,
    .node = node
  };
}

static void print_indentation(uint64_t* last_child, int depth) {
  for (int i = 1; i < depth+1; ++i) {
    if (bitset_query(last_child, i)) {
      printf("%c", i == depth ? 192 : ' ');
    }
    else {
      printf("%c", i == depth ? 195 : 179);
    }

    if (i == depth) {
      printf("%c", 196);
    }
    else {
      printf(" ");
    }
  }
}

void dump_parse_tree(ParseTree tree) {
  Scratch scratch = global_scratch(0, NULL);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  DynamicArray(Item) stack = new_dynamic_array(scratch_allocator);

  int tree_count = tree.num_nodes;

  ParseNode* root = &tree.nodes[tree.num_nodes-1];
  dynamic_array_put(stack, make_item(scratch_allocator, tree_count, NULL, 0, root, true));

  #define CHILD(x, is_last_child) dynamic_array_put(stack, make_item(scratch_allocator, tree_count, item.last_child, item.depth + 1, &tree.nodes[x], is_last_child))

  while (dynamic_array_length(stack)) {
    Item item = dynamic_array_pop(stack);
    ParseNode* node = item.node;

    print_indentation(item.last_child, item.depth);
    printf("%s: '%.*s'\n", parse_node_debug_name[node->kind], node->token.length, node->token.start);

    static_assert(NUM_PARSE_NODE_KINDS == 6, "handle all parse tree dump");
    switch (node->kind) {
      default:
        assert(false);
        break;
      case PARSE_NODE_INTEGER_LITERAL:
        break;
      case PARSE_NODE_ADD:
      case PARSE_NODE_SUB:
      case PARSE_NODE_MUL:
      case PARSE_NODE_DIV:
        CHILD(node->as.bin.rhs, true);
        CHILD(node->as.bin.lhs, false);
    }

    allocator_free(scratch_allocator, item.last_child);
  }

  #undef CHILD

  scratch_release(&scratch);
}