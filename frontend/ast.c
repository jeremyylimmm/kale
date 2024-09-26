#include <stdio.h>

#include "frontend.h"
#include "dynamic_array.h"

typedef struct {
  int depth;
  char* member;
  AST* node;
} DumpNode;

static DumpNode make_dump_node(int depth, char* member, AST* node) {
  return (DumpNode) {
    .depth = depth,
    .member = member,
    .node = node
  };
}

void dump_ast(AST* ast) {
  Scratch scratch = global_scratch(0, NULL);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  DynamicArray(DumpNode) stack = new_dynamic_array(scratch_allocator);
  dynamic_array_put(stack, make_dump_node(0, NULL, ast));

  while (dynamic_array_length(stack)) {
    DumpNode dn = dynamic_array_pop(stack);
    AST* node = dn.node;

    printf("%*s", dn.depth * 2, ""); 

    if (dn.member) {
      printf("%s: ", dn.member);
    }

    switch (node->kind) {
      default:
        printf("%s", ast_kind_str[dn.node->kind]);
        break;

      case AST_INTEGER_LITERAL:
        printf("$%llu", node->as.integer_literal);
        break;
    }

    printf("\n");

    #define CHILD(member, child) dynamic_array_put(stack, make_dump_node(dn.depth + 1, member, child))

    static_assert(NUM_AST_KINDS == 6, "handle ast kinds");
    switch (node->kind) {
      case AST_ADD:
      case AST_SUB:
      case AST_MUL:
      case AST_DIV:
        CHILD("rhs", node->as.bin[1]);
        CHILD("lhs", node->as.bin[0]);
    }

    #undef CHILD
  }

  scratch_release(&scratch);
}