#include <stdio.h>

#include "frontend.h"
#include "dynamic_array.h"

ParseNodeChildrenIterator parse_node_children_begin(ParseNode* node) {
  return (ParseNodeChildrenIterator) {
    .index = node->num_children - 1,
    .node = node - 1
  };
}

bool parse_node_children_valid(ParseNodeChildrenIterator* it) {
  return it->index >= 0;
}

void parse_node_children_next(ParseNodeChildrenIterator* it) {
  it->node -= it->node->subtree_size;
  it->index -= 1;
}

typedef struct {
  int depth;
  uint64_t* last_child;
  ParseNode* node;
} Item;

static uint64_t* alloc_last_child(Arena* arena, uint64_t* prev_last_child, int tree_count) {
  size_t alloc_size = bitset_num_u64(tree_count) * sizeof(uint64_t);

  uint64_t* last_child = arena_push(arena, alloc_size);

  if (prev_last_child) {
    memcpy(last_child, prev_last_child, alloc_size);
  } 
  else {
    memset(last_child, 0, alloc_size);
  }

  return last_child;
}

static Item make_item(Arena* arena, int tree_count, uint64_t* prev_last_child, int depth, ParseNode* node, bool is_last_child) {
  uint64_t* last_child = alloc_last_child(arena, prev_last_child, tree_count);

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

  Item* data = arena_array(scratch.arena, Item, tree.num_nodes);

  DynamicArray(Item) stack = new_dynamic_array(scratch_allocator);

  int tree_count = tree.num_nodes;

  ParseNode* root = &tree.nodes[tree.num_nodes-1];
  dynamic_array_put(stack, make_item(scratch.arena, tree_count, NULL, 0, root, true));

  while (dynamic_array_length(stack)) {
    Item item = dynamic_array_pop(stack);
    ParseNode* node = item.node;

    foreach_parse_node_child(node, child) {
      dynamic_array_put(stack, make_item(
        scratch.arena,
        tree_count,
        item.last_child,
        item.depth + 1,
        child.node,
        child.index == 0
      ));
    }

    data[node - tree.nodes] = item;
  }

  for_range_rev(int, i, tree.num_nodes) {
    Item item = data[i];
    ParseNode* node = &tree.nodes[i];
    print_indentation(item.last_child, item.depth);
    printf("%s: '%.*s'\n", parse_node_debug_name[node->kind], node->token.length, node->token.start);
  }

  scratch_release(&scratch);
}