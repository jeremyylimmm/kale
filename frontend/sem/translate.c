#include "frontend.h"

static bool translate_fn(SemContext* context, AST* fn, SemFunc* result) {
  assert(fn->kind == AST_FN);

  result->blocks = new_dynamic_array(context->allocator);

  AST* body = NULL;

  foreach_ast_child(fn, child) {
    if (child.index == 0) {
      body = child.node;
    }
  }

  foreach_ast_child(body,child) {
  }

  return true;
}

SemFile* sem_translate(SemContext* context, ASTBuffer* ast_buffer) {
  Scratch scratch = global_scratch(1, &context->arena);

  SemFile* return_value = NULL;
  DynamicArray(SemFunc) funcs = new_dynamic_array(context->allocator);

  ASTRoots roots = ast_get_roots(scratch.arena, ast_buffer);

  for_range(int, i, roots.count) {
    AST* node = roots.nodes[i];

    switch (node->kind) {
      default:
        assert(false && "hit an invalid top level-statement");
        break;

      case AST_FN: {
        SemFunc result;
        translate_fn(context, node, &result);
        dynamic_array_put(funcs, result);
      } break;
    }
  }

  return_value = arena_type(context->arena, SemFile);
  return_value->funcs = funcs;

  //end:
  scratch_release(&scratch);
  return return_value;
}