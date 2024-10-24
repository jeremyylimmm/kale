#include "frontend.h"

typedef struct {
  int processed;
  AST* node;

  union {
    struct { int start_tail; int then_head; int then_tail; int else_head; int end; } _if;
    struct { int og_stack_count; } block;
    struct { int start_head, start_tail; int body_head; } _while;
  } data;
} CheckItem;

typedef struct {
  SemValue val;
  Token token;
} Value;

typedef struct {
  String name;
  SemValue val;
} Symbol;

typedef struct Scope Scope;
struct Scope {
  int capacity;
  int count;
  uint64_t* occ;
  Symbol* table;
};

typedef struct {
  SemContext* context;
  SourceContents source;
  Allocator* scratch_allocator;

  DynamicArray(CheckItem) item_stack;
  DynamicArray(Value) value_stack;
  DynamicArray(Scope) scope_stack;

  DynamicArray(SemBlock) blocks;
  
  SemValue next_value;
} Checker;

static String token_string_view(Token token) {
  return (String) {
    .str = token.start,
    .length = token.length
  };
}

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

static int _new_block(Checker* c) {
  SemBlock block = {
    .insts = new_dynamic_array(c->context->allocator)
  };

  dynamic_array_put(c->blocks, block);
  return dynamic_array_length(c->blocks)-1;
}

static void new_block(Checker* c, int* cur, int* new) {
  *cur =  dynamic_array_length(c->blocks)-1;
  *new = _new_block(c);
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
  int cur_block = dynamic_array_length(c->blocks)-1;
  add_inst_in_block(c, cur_block, op, token, has_def, num_ins, data);
}

static int scope_find(Scope* scope, String key) {
  assert(scope->capacity);

  uint64_t hash = fnv1a_hash(key.str, key.length * sizeof(key.str[0]));
  int i =  hash % scope->capacity;

  for_range(int, j, scope->capacity) {
    if (!bitset_query(scope->occ, i)) {
      return i;
    }

    if (strings_ident(scope->table[i].name, key)) {
      return i;
    }

    i = (i + 1) % scope->capacity;
  }

  assert(false); // This should be unreachable - load factor should be maintained at certain level
  return -1;
}

static void free_scope(Checker* c, Scope scope) {
  allocator_free(c->scratch_allocator, scope.table);
  allocator_free(c->scratch_allocator, scope.occ);
}

static void _add_local(Scope* scope, Symbol symbol) {
  int i = scope_find(scope, symbol.name);

  if (!bitset_query(scope->occ, i)) {
    scope->table[i] = symbol;
    bitset_set(scope->occ, i);
    scope->count++;
  }
}

static void add_local(Checker* c, Scope* scope, String name, SemValue val) {
  if (!scope->capacity || (float)scope->count > (float)scope->capacity * 0.5f) {
    int new_capacity = scope->capacity ? scope->capacity * 2 : 8;

    Scope new_scope = {
      .capacity = new_capacity,
      .occ = allocator_alloc(c->scratch_allocator, bitset_num_u64(new_capacity) * sizeof(uint64_t)),
      .table = allocator_alloc(c->scratch_allocator, new_capacity * sizeof(scope->table[0]))
    };

    for_range(int, i, scope->capacity) {
      if (bitset_query(scope->occ, i)) {
        _add_local(&new_scope, scope->table[i]);
      }
    }

    free_scope(c, *scope);
    *scope = new_scope;
  }

  _add_local(scope, (Symbol){.name = name, .val = val});
}

static SemValue find_local(Checker* c, String name) {
  for_range_rev(int, s, dynamic_array_length(c->scope_stack)) {
    Scope* scope = &c->scope_stack[s];

    if (scope->capacity) {
      int i = scope_find(scope, name);

      if (bitset_query(scope->occ, i)) {
        return scope->table[i].val;
      }
    }
  }

  return 0;
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
  assert(item.node->num_children == 2);

  Token name_tok = item.node->children[0]->token;
  Token ty_tok = item.node->children[1]->token;

  if (strncmp("int", ty_tok.start, 3) != 0) {
    error_at_token(c->source, ty_tok, "only 'int' type supported");
    return false;
  }

  add_inst(c, SEM_OP_LOCAL, item.node->token, true, 0, NULL);
  SemValue val = dynamic_array_back(c->value_stack).val;

  String name = token_string_view(name_tok);
  if (find_local(c, name)) {
    error_at_token(c->source, name_tok, "this symbol name overwrites an existing symbol");
    return false;
  }

  add_local(c, &dynamic_array_back(c->scope_stack), name, val);

  return true;
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
      Scope new_scope = {0};
      dynamic_array_put(c->scope_stack, new_scope);

      item.processed += 1;
      item.data.block.og_stack_count = dynamic_array_length(c->value_stack);
      push_item(c, item);

      for_range_rev(int, i, item.node->num_children) {
        push_node(c, false, item.node->children[i]);
      }
    } break;

    case 1: {
      while (dynamic_array_length(c->value_stack) > item.data.block.og_stack_count) {
        dynamic_array_pop(c->value_stack);
      }

      free_scope(c, dynamic_array_pop(c->scope_stack));
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
    _new_block(c);
  }

  return true;
}

static void add_branch(Checker* c, Token if_token, int tail, int then_head, int else_head) {
  int* locs = arena_array(c->context->arena, int, 2);
  locs[0] = then_head;
  locs[1] = else_head;

  add_inst_in_block(c, tail, SEM_OP_BRANCH, if_token, false, 1, locs);
}

static void add_goto(Checker* c, Token token, int tail, int head) {
  int* locs = arena_type(c->context->arena, int);
  locs[0] = head;
  add_inst_in_block(c, tail, SEM_OP_GOTO, token, false, 0, locs);
}

static bool check_ast_WHILE(Checker* c, CheckItem item) {
  switch (item.processed) {
    case 0: {
      int prev_tail, start_head;
      new_block(c, &prev_tail, &start_head);

      add_goto(c, item.node->token, prev_tail, start_head);

      item.data._while.start_head = start_head;
      item.processed = 1;
      push_item(c, item);
      push_node(c, false, item.node->children[0]); // Predicate
    } break;

    case 1: {
      new_block(c, &item.data._while.start_tail, &item.data._while.body_head);
      item.processed = 2;
      push_item(c, item);
      push_node(c, false, item.node->children[1]); // Body
    } break;
    
    case 2: {
      int body_tail, end_head;
      new_block(c, &body_tail, &end_head);
      add_branch(c, item.node->token, item.data._while.start_tail, item.data._while.body_head, end_head);
      add_goto(c, item.node->token, body_tail, item.data._while.start_head);
    } break;
  }

  return true;
}

static bool check_ast_IF(Checker* c, CheckItem item) {
  switch (item.processed) {
    case 0: {
      item.processed = 1;
      push_item(c, item);
      push_node(c, false, item.node->children[0]); // Predicate
    } break;

    case 1: {
      new_block(c, &item.data._if.start_tail, &item.data._if.then_head);
      item.processed = 2;
      push_item(c, item);
      push_node(c, false, item.node->children[1]); // Body
    } break;

    case 2: {
      if (item.node->num_children == 3) { // Has else 
        new_block(c, &item.data._if.then_tail, &item.data._if.else_head);
        item.processed = 3;
        push_item(c, item);
        push_node(c, false, item.node->children[2]); // else
      }
      else {
        int then_tail, end_head;
        new_block(c, &then_tail, &end_head);
        add_branch(c, item.node->token, item.data._if.start_tail, item.data._if.then_head, end_head);
        add_goto(c, item.node->token, then_tail, end_head);
      }
    } break;

    case 3: {
      int then_tail = item.data._if.then_tail;
      int else_tail, end_head;
      new_block(c, &else_tail, &end_head);

      Token if_token = item.node->token;
      add_branch(c, if_token, item.data._if.start_tail, item.data._if.then_head, item.data._if.else_head);
      add_goto(c, if_token, then_tail, end_head);
      add_goto(c, if_token, else_tail, end_head);
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

    .scratch_allocator = scratch.allocator,

    .item_stack = new_dynamic_array(scratch.allocator),
    .value_stack = new_dynamic_array(scratch.allocator),
    .scope_stack = new_dynamic_array(scratch.allocator),

    .blocks = new_dynamic_array(context->allocator),

    .next_value = 1
  };

  assert(fn->num_children == 2);
  AST* name = fn->children[0];
  AST* body = fn->children[1];

  assert(name->kind == AST_IDENTIFIER);
  func_out->name = token_string(context, name->token);

  push_node(&c, false, body);
  _new_block(&c);

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