#include "frontend.h"

typedef struct {
  int processed;
  AST* node;

  union {
    struct { int head; int then; int then_tail; int els; } _if;
    struct { int og_stack_count; } block;
  } data;
} CheckItem;

typedef struct {
  SemValue val;
  Token token;
} Value;

typedef struct {
  SemContext* context;
  SourceContents source;

  DynamicArray(CheckItem) item_stack;
  DynamicArray(Value) value_stack;
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

static void push_item(Checker* c, CheckItem item) {
  dynamic_array_put(c->item_stack, item);
}

static void push_node(Checker* c, int processed, AST* node) {
  CheckItem item = {
    .processed = processed,
    .node = node
  };

  push_item(c, item);
}

static int new_block(Checker* c) {
  SemBlock block = {
    .insts = new_dynamic_array(c->context->allocator)
  };

  dynamic_array_put(c->blocks, block);
  return dynamic_array_length(c->blocks)-1;
}

static int cur_block(Checker* c) {
  return dynamic_array_length(c->blocks)-1;
}

static void add_inst_in_block(Checker* c, int block, SemOp op, Token token, bool has_def, int num_ins, void* data) {
  assert(num_ins <= SEM_MAX_INS);

  SemInst inst = {
    .op = op,
    .num_ins = num_ins,
    .data = data
  };

  for_range_rev(int, i, num_ins) {
    inst.ins[i] = dynamic_array_pop(c->value_stack).val;
  }

  if (has_def) {
    inst.def = c->next_value++;

    Value val = {
      .val = inst.def,
      .token = token
    };

    dynamic_array_put(c->value_stack, val);
  }

  dynamic_array_put(c->blocks[block].insts, inst);
}

static void add_inst(Checker* c, SemOp op, Token token, bool has_def, int num_ins, void* data) {
  add_inst_in_block(c, cur_block(c), op, token, has_def, num_ins, data);
}

static bool check_ast_INT_LITERAL(Checker* c, CheckItem item) {
  uint64_t value = 0;
  Token token = item.node->token;

  for_range (int, i, token.length) {
    value *= 10;
    value += token.start[i] - '0';
  }

  add_inst(c, SEM_OP_INT_CONST, token, true, 0, (void*)value);

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

static bool check_binary(Checker* c, CheckItem item, SemOp op, Token token) {
  if (!item.processed) {
    push_node(c, true, item.node);

    assert(item.node->num_children == 2);
    push_node(c, false, item.node->children[1]);
    push_node(c, false, item.node->children[0]);
  }
  else {
    add_inst(c, op, token, true, 2, NULL);
  }

  return true;
}

static bool check_ast_ADD(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_ADD, item.node->token);
}

static bool check_ast_SUB(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_SUB, item.node->token);
}

static bool check_ast_MUL(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_MUL, item.node->token);
}

static bool check_ast_DIV(Checker* c, CheckItem item) {
  return check_binary(c, item, SEM_OP_DIV, item.node->token);
}

static bool check_ast_ASSIGN(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_INITIALIZE(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_BLOCK(Checker* c, CheckItem item) {
  switch (item.processed) {
    case 0: {
      item.processed += 1;
      item.data.block.og_stack_count = dynamic_array_length(c->value_stack);
      push_item(c, item);

      for_range_rev(int, i, item.node->num_children) {
        push_node(c, false, item.node->children[i]);
      }
    } break;

    case 1: {
      if (dynamic_array_length(c->value_stack) != item.data.block.og_stack_count) {
        Value val = c->value_stack[dynamic_array_length(c->value_stack)-1];
        error_at_token(c->source, val.token, "the result of this expression is unused");

        while (dynamic_array_length(c->value_stack) > item.data.block.og_stack_count) {
          dynamic_array_pop(c->value_stack);
        }

        return false;
      }
    } break;
  }

  return true;
}

static bool check_ast_RETURN(Checker* c, CheckItem item) {
  if (!item.processed) {
    push_node(c, true, item.node);

    assert(item.node->num_children == 1);
    push_node(c, false, item.node->children[0]);
  }
  else {
    add_inst(c, SEM_OP_RETURN, item.node->token, false, 1, NULL);
    new_block(c);
  }

  return true;
}

static bool check_ast_WHILE(Checker* c, CheckItem item) {
  INVALID();
}

static bool check_ast_IF(Checker* c, CheckItem item) {
  switch (item.processed) {
    case 0: {
      item.data._if.head = cur_block(c);
      item.processed = 1;
      push_item(c, item);
      push_node(c, false, item.node->children[0]); // Expression
    } break;

    case 1: {
      item.data._if.then = new_block(c);
      item.processed = 2;
      push_item(c, item);
      push_node(c, false, item.node->children[1]); // Body
    } break;

    case 2: {
      item.data._if.then_tail = cur_block(c);
      item.processed = 3;

      if (item.node->num_children == 3) { // Has else 
        item.data._if.els = new_block(c);
        push_item(c, item);

        push_node(c, false, item.node->children[2]); // else
      }
      else {
        push_item(c, item);
      }
    } break;

    case 3: {
      bool has_else = item.node->num_children == 3;

      int* locs = arena_array(c->context->arena, int, 2);
      locs[0] = item.data._if.then;

      int else_tail = cur_block(c);
      int end = new_block(c);

      if (has_else) {
        locs[1] = item.data._if.els;
      }
      else {
        locs[1] = end;
      }

      int head = item.data._if.head;
      add_inst_in_block(c, head, SEM_OP_BRANCH, item.node->token, false, 1, locs);

      locs = arena_type(c->context->arena, int);
      locs[0] = end;

      int then_tail = item.data._if.then_tail;
      add_inst_in_block(c, then_tail, SEM_OP_GOTO, item.node->children[1]->token, false, 0, locs);

      if (has_else) {
        add_inst_in_block(c, else_tail, SEM_OP_GOTO, item.node->children[2]->token, false, 0, locs);
      }
    } break;
  }

  return true;
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

  assert(fn->num_children == 2);
  AST* name = fn->children[0];
  AST* body = fn->children[1];

  assert(name->kind == AST_IDENTIFIER);
  func_out->name = token_string(context, name->token);

  push_node(&c, false, body);
  new_block(&c);

  bool had_error = false;

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
      had_error = true;
    }
  }

  bool ret_val = !had_error;

  if (!had_error) {
    func_out->blocks = c.blocks;
  }

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