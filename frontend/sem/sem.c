#if 0

#include "frontend.h"
#include "dynamic_array.h"

typedef struct {
  bool processed;
  ParseNode* node;
} Item;

static Item make_item(ParseNode* node, bool processed) {
  return (Item) {
    .node = node,
    .processed = processed
  };
}

SemFunction* sem_generate(Arena* arena, ParseTree parse_tree) {
  Scratch scratch = global_scratch(1, &arena);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  SemFunction* return_value = NULL;

  DynamicArray(Item) stack = new_dynamic_array(scratch_allocator);

  ParseNode* root = &parse_tree.nodes[parse_tree.num_nodes-1];
  dynamic_array_put(stack, make_item(root, false)); 

  while (dynamic_array_length(stack)) {
    Item item = dynamic_array_pop(stack);
    ParseNode* node = item.node;

    if (!item.processed) {

    }
    else {
    }
  }

  scratch_release(&scratch);

  end:
  return return_value;
}
#endif