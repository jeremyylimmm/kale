#include "frontend.h"

typedef struct {
  bool processed;
  AST* node;
} CheckItem;

typedef struct {
  SemContext* context;
  SourceContents source;

  DynamicArray(CheckItem) item_stack;
  DynamicArray(SemValue) value_stack;
  DynamicArray(SemBlock) blocks;
  
  SemValue next_value;
} Checker;

static String token_string(SemContext* context, Token token) {
  char* buf = arena_push(context->arena, (token.length+1) * sizeof(char));

  memcpy(buf, token.start, token.length * sizeof(char));
  buf[token.length] = '\0';

  return (String) {
    .length = token.length,
    .str = buf
  };
}

static void push_item(Checker* c, bool processed, AST* node) {
  CheckItem item = {
    .processed = processed,
    .node = node
  };

  dynamic_array_put(c->item_stack, item);
}

static int new_block(Checker* c) {
  SemBlock block = {
    .insts = new_dynamic_array(c->context->allocator)
  };

  dynamic_array_put(c->blocks, block);
  return dynamic_array_length(c->blocks)-1;
}

static void add_inst(Checker* c, SemOp op, bool has_def, int num_ins, void* data) {
  assert(num_ins <= SEM_MAX_INS);

  SemInst inst = {
    .op = op,
    .num_ins = num_ins,
    .data = data
  };

  for_range_rev(int, i, num_ins) {
    inst.ins[i] = dynamic_array_pop(c->value_stack);
  }

  if (has_def) {
    inst.def = c->next_value++;
    dynamic_array_put(c->value_stack, inst.def);
  }

  int cur_block = dynamic_array_length(c->blocks)-1;
  dynamic_array_put(c->blocks[cur_block].insts, inst);
}

static bool check_ast_INT_LITERAL(Checker* c, CheckItem item) {
  uint64_t value = 0;
  Token token = item.node->token;

  for_range (int, i, token.length) {
    value *= 10;
    value += token.start[i] - '0';
  }

  add_inst(c, SEM_OP_INT_CONST, true, 0, (void*)value);

  return true;
}

#define INVALID() \
  do { \
    error_at_token(c->source, item.node->token, "compiler bug(check): was not expecting this '%s' here", ast_kind_string[item.node->kind]); \
    return false; \
  } while (false)

static bool check_ast_IDENTIFIER(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_LOCAL(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_binary(Checker* c, CheckItem item, SemOp op) {
  if (!item.processed) {
    push_item(c, true, item.node);

    assert(item.node->num_children == 2);
    push_item(c, false, item.node->children[1]);
    push_item(c, false, item.node->children[0]);
  }
  else {
    add_inst(c, op, true, 2, NULL);
  }

  return true;
}

static bool check_ast_ADD(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_ADD);
}

static bool check_ast_SUB(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_SUB);
}

static bool check_ast_MUL(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_MUL);
}

static bool check_ast_DIV(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_DIV);
}

static bool check_ast_ASSIGN(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_INITIALIZE(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_BLOCK(Checker* c, CheckItem item) {
  for_range_rev(int, i, item.node->num_children) {
    push_item(c, false, item.node->children[i]);
  }
  return true;
}

static bool check_ast_RETURN(Checker* c, CheckItem item) {
  if (!item.processed) {
    push_item(c, true, item.node);

    assert(item.node->num_children == 1);
    push_item(c, false, item.node->children[0]);
  }
  else {
    add_inst(c, SEM_OP_RETURN, false, 1, NULL);
    new_block(c);
  }

  return true;
}

static bool check_ast_WHILE(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_IF(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_FN(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_FILE(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_fn(SemContext* context, SourceContents source, AST* fn, SemFunc* func_out) {
  Scratch scratch = global_scratch(1, &context->arena);

  Checker c = {
    .context = context,
    .source = source,
    .item_stack = new_dynamic_array(scratch.allocator),
    .value_stack = new_dynamic_array(scratch.allocator),
    .blocks = new_dynamic_array(context->allocator),
    .next_value = 1
  };

  bool ret_val = false;

  assert(fn->num_children == 2);
  AST* name = fn->children[0];
  AST* body = fn->children[1];

  assert(name->kind == AST_IDENTIFIER);
  func_out->name = token_string(context, name->token);

  push_item(&c, false, body);
  new_block(&c);

  while (dynamic_array_length(c.item_stack)) {
    CheckItem item = dynamic_array_pop(c.item_stack);

    bool result;

    #define X(name, ...) case AST_##name: result = check_ast_##name(&c, item); break;
    switch (item.node->kind) {
      default:
        assert(false);
        result = false;
        break;
      #include "parser/ast_kind.def"
    }
    #undef X

    if (!result) {
      goto end;
    }
  }

  ret_val = true;
  func_out->blocks = c.blocks;

  end:
  scratch_release(&scratch);
  return ret_val;
}

SemFile* check_ast(SemContext* context, SourceContents source, AST* ast) {
  Scratch scratch = global_scratch(1, &context->arena);

  SemFile* ret_val = NULL;

  assert(ast->kind == AST_FILE);
  DynamicArray(SemFunc) funcs = new_dynamic_array(scratch.allocator);

  for_range (int, i, ast->num_children) {
    AST* node = ast->children[i]; 

    switch (node->kind) {
      default:
        assert(false && "top level statement not handled in check");
        break;

      case AST_FN: {
        SemFunc func;
        if (!check_fn(context, source, node, &func)) {
          goto end;
        }
        dynamic_array_put(funcs, func);
      } break;

    }
  }

  ret_val = arena_type(context->arena, SemFile);
  ret_val->num_funcs = dynamic_array_length(funcs);
  ret_val->funcs = dynamic_array_bake(context->arena, funcs);

  end:
  scratch_release(&scratch);
  return ret_val;
}