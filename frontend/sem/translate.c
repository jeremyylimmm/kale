#include "frontend.h"

static SemBlock make_block(SemContext* context) {
  return (SemBlock) {
    .insts = new_dynamic_array(context->allocator)
  };
}

typedef struct {
  DynamicArray(SemBlock) blocks;
  DynamicArray(SemValue) value_stack;
  int cur_block;
  SemValue next_value;
} Translator;

static void inst(Translator* t, SemOp op, bool def_value, int num_ins, void* data) {
  assert(num_ins <= SEM_MAX_INS);

  SemInst inst = {
    .op = op,
    .data = data,
    .num_ins = num_ins
  };

  for_range_rev(int, i, num_ins) {
    inst.ins[i] = dynamic_array_pop(t->value_stack);
  }

  if (def_value) {
    inst.def = t->next_value++;
    dynamic_array_put(t->value_stack, inst.def);
  }
  
  dynamic_array_put(t->blocks[t->cur_block].insts, inst);
}

static void expr(Translator* t, SemOp op, int num_ins, void* data) {
  inst(t, op, true, num_ins, data);
}

static AST* eat(ASTBuffer* ast_buffer, int* cur) {
  assert((*cur) < ast_buffer->count);
  AST* node = &ast_buffer->nodes[(*cur)++];
  return node;
}

static String token_to_string(SemContext* context, Token token) {
  char* buf = arena_push(context->arena, (token.length + 1) * sizeof(char));

  memcpy(buf, token.start, token.length * sizeof(char));
  buf[token.length] = '\0';

  return (String) {
    .str = buf,
    .length = token.length
  };
}

static bool translate_fn(SemContext* context, ASTBuffer* ast_buffer, int* cur, SemFunc* result) {
  Scratch scratch = global_scratch(1, &context->arena);
 
  Translator t = {
    .blocks = new_dynamic_array(context->allocator),
    .value_stack = new_dynamic_array(scratch.allocator),
    .next_value = 1
  };

  dynamic_array_put(t.blocks, make_block(context));

  eat(ast_buffer, cur); // fn introducer

  AST* name_node = eat(ast_buffer, cur);
  assert(name_node->kind = AST_IDENTIFIER); 

  while ((*cur) < ast_buffer->count && ast_buffer->nodes[(*cur)].kind != AST_FN) {
    AST* node = eat(ast_buffer, cur);

    switch (node->kind) {
      default:
        assert(false);
        break;
      
      case AST_INT_LITERAL: {
        uint64_t value = 0;
        for_range(int, i, node->token.length) {
          value *= 10;
          value += node->token.start[i] - '0';
        }
        expr(&t, SEM_OP_INT_CONST, 0, (void*)value);
      } break;
        
      case AST_LOCAL:
      case AST_ASSIGN:
      case AST_INITIALIZE:
      case AST_IDENTIFIER: {
        assert(false && "not implemented");
      } break;

      case AST_ADD:
        expr(&t, SEM_OP_ADD, 2, NULL);
        break;
      case AST_SUB:
        expr(&t, SEM_OP_SUB, 2, NULL);
        break;
      case AST_MUL:
        expr(&t, SEM_OP_MUL, 2, NULL);
        break;
      case AST_DIV:
        expr(&t, SEM_OP_DIV, 2, NULL);
        break;

      case AST_BLOCK_INTRODUCER:
      case AST_BLOCK:
        break;

      case AST_RETURN:
        expr(&t, SEM_OP_RETURN, 1, NULL);
        break;

      case AST_WHILE:
        assert(false && "not implemented");
        break;

      case AST_IF:
        assert(false && "not implemented");
        break;

      case AST_ELSE:
        assert(false && "not implemented");
        break;
    }
  }

  eat(ast_buffer, cur);

  *result = (SemFunc){
    .name = token_to_string(context, name_node->token),
    .blocks = t.blocks
  };

  scratch_release(&scratch);
  return true;
}

SemFile* sem_translate(SemContext* context, ASTBuffer* ast_buffer) {
  Scratch scratch = global_scratch(1, &context->arena);

  SemFile* return_value = NULL;
  DynamicArray(SemFunc) funcs = new_dynamic_array(context->allocator);

  int cur = 0;

  // Loop through top-level statements and translate them into semantic IR
  while (cur < ast_buffer->count) {
    switch (ast_buffer->nodes[cur].kind) {
      default:
        assert(false);
        break;

      case AST_FN_INTRODUCER: {
        SemFunc func;
        if (!translate_fn(context, ast_buffer, &cur, &func)) {
          goto end;
        }
        dynamic_array_put(funcs, func);
      } break;
    }
  }

  return_value = arena_type(context->arena, SemFile);
  return_value->funcs = funcs;

  end:
  scratch_release(&scratch);
  return return_value;
}