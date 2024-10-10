#include "frontend.h"
#include "dynamic_array.h"

#define X(name, ...) STATE_##name,
typedef enum { 
  STATE_INVALID,
  #include "parse_state.def"
  NUM_STATES
} ParseStateKind;
#undef X

typedef struct {
  ParseStateKind kind;
  union {
    struct { int prec; } binary_begin;
    struct { int prec; } binary_infix;
    struct { Token op; int prec; } binary_accept;
    struct { bool is_expr; } block_stmt_accept;
    struct { Token if_token; } if_accept;
    struct { Token else_token; } else_accept;
  } as;
} ParseState;

typedef struct {
  SourceContents source;

  DynamicArray(ParseState) state_stack;
  DynamicArray(int) node_stack;
  
  TokenizedBuffer tokens;
  int cur_token;
  
  ParseNode* nodes;
  int num_nodes;
  int node_capacity;
} Context;

typedef bool(*HandleStateFunc)(Context*, ParseState);

static void push_state(Context* context, ParseState state) {
  dynamic_array_put(context->state_stack, state);
}

static void push_node(Context* context, ParseNode* node) {
  int index = (int)(node - context->nodes);
  dynamic_array_put(context->node_stack, index);
}

static ParseNode* pop_node(Context* context) {
  return &context->nodes[dynamic_array_pop(context->node_stack)];
}

static Token peekn(Context* context, int n) {
  int index = context->cur_token + n;

  if (index > context->tokens.length-1) {
    index = context->tokens.length - 1;
  }

  return context->tokens.tokens[index];
}

static Token peek(Context* context) {
  return peekn(context, 0);
}

static Token lex(Context* context) {
  Token token = peek(context);

  if (context->cur_token < context->tokens.length-1) {
    context->cur_token++;
  }

  return token;
}

static ParseNode* new_node(Context* context, ParseNodeKind kind, Token token) {
  assert(context->num_nodes < context->node_capacity);
  ParseNode* node = &context->nodes[context->num_nodes++];
  node->kind = kind;
  node->token = token;
  return node;
}

static bool match(Context* context, int kind, char* message) {
  if (peek(context).kind != kind) {
    error_at_token(context->source.path, context->source.contents, peek(context), message);
    return false;
  }

  lex(context);
  return true;
}

#define REQUIRE(context, kind, message) \
  do { \
    if (!match(context, kind, message)) { \
      return false; \
    } \
  } while (false)

static bool handle_PRIMARY(Context* context, ParseState state) {
  (void)state;

  switch (peek(context).kind) {
    default:
      error_at_token(context->source.path, context->source.contents, peek(context), "expected an expression");
      return false;

    case TOKEN_INTEGER_LITERAL: {
      ParseNode* node = new_node(context, PARSE_NODE_INTEGER_LITERAL, lex(context));
      push_node(context, node);
      return true;
    }
  }
}

static bool handle_BINARY_BEGIN(Context* context, ParseState state) {
  push_state(context, (ParseState) {
    .kind = STATE_BINARY_INFIX,
    .as.binary_infix.prec = state.as.binary_begin.prec
  });

  push_state(context, (ParseState) { .kind = STATE_PRIMARY });

  return true;
}

static int binary_prec(Token op) {
  switch (op.kind) {
    default:
      return 0;
    case '*':
    case '/':
      return 20;
    case '+':
    case '-':
      return 10;
  }
}

static ParseNodeKind binary_node_kind(Token op) {
  switch (op.kind) {
    default:
      assert(false);
      return PARSE_NODE_INVALID;
    case '*':
      return PARSE_NODE_MUL;
    case '/':
      return PARSE_NODE_DIV;
    case '+':
      return PARSE_NODE_ADD;
    case '-':
      return PARSE_NODE_SUB;
  }
}

static bool handle_BINARY_INFIX(Context* context, ParseState state) {
  if (binary_prec(peek(context)) > state.as.binary_infix.prec) {
    Token op = lex(context);

    push_state(context, (ParseState) {
      .kind = STATE_BINARY_ACCEPT,
      .as.binary_accept.op = op,
      .as.binary_accept.prec = state.as.binary_infix.prec
    });

    push_state(context, (ParseState){
      .kind = STATE_BINARY_BEGIN,
      .as.binary_begin.prec = binary_prec(op)
    });
  }

  return true;
}

static bool handle_BINARY_ACCEPT(Context* context, ParseState state) {
  ParseNode* rhs = pop_node(context);
  ParseNode* lhs = pop_node(context);

  Token op = state.as.binary_accept.op;

  ParseNode* node = new_node(context, binary_node_kind(op), op);
  node->as.bin.lhs = lhs;
  node->as.bin.rhs = rhs;

  push_node(context, node);

  push_state(context, (ParseState){
    .kind = STATE_BINARY_INFIX,
    .as.binary_infix.prec = state.as.binary_accept.prec
  });

  return true;
}

static bool handle_EXPR(Context* context, ParseState state) {
  (void)state;

  push_state(context, (ParseState){
    .kind = STATE_BINARY_BEGIN,
    .as.binary_begin.prec = 0
  });

  return true;
}

static bool handle_BLOCK_BEGIN(Context* context, ParseState state) {
  (void)state;

  Token token = peek(context);
  REQUIRE(context, '{', "expected a block '{'");

  ParseNode* open = new_node(context, PARSE_NODE_BLOCK_OPEN, token);
  push_node(context, open);

  push_state(context, (ParseState) {
    .kind = STATE_BLOCK_STMT_BEGIN
  });

  return true;
}

static void close_block(Context* context) {
  ParseNode* open = NULL;

  ParseNode tail = {0};
  ParseNode* cur = &tail;

  while (!open) {
    ParseNode* node = pop_node(context);
    
    if (node->kind == PARSE_NODE_BLOCK_OPEN) {
      open = node;
    }
    else {
      cur = cur->prev = node;
    }
  }

  ParseNode* block = new_node(context, PARSE_NODE_BLOCK, lex(context));
  block->as.block.open = open;
  block->as.block.tail_stmt = tail.prev;

  push_node(context, block);
}

static bool handle_BLOCK_STMT_BEGIN(Context* context, ParseState state) {
  (void)state;

  if (peek(context).kind == '}') {
    close_block(context);
    return true;
  }

  bool is_expr = false;
  ParseState stmt_state = {0};

  switch (peek(context).kind) {
    default:
      is_expr = true;
      stmt_state = (ParseState) { .kind = STATE_EXPR };
      break;

    case '{':
      stmt_state = (ParseState) { .kind = STATE_BLOCK_BEGIN };
      break;
    
    case TOKEN_KEYWORD_IF:
      stmt_state = (ParseState) { .kind = STATE_IF_BEGIN };
      break;
  }

  push_state(context, (ParseState) {
    .kind = STATE_BLOCK_STMT_ACCEPT,
    .as.block_stmt_accept.is_expr = is_expr
  });

  push_state(context, stmt_state);

  return true;
}

static bool handle_BLOCK_STMT_ACCEPT(Context* context, ParseState state) {
  (void)state;

  if (state.as.block_stmt_accept.is_expr) {
    Token semi = peek(context);
    REQUIRE(context, ';', "expected a semi-colon ';'");

    ParseNode* stmt = new_node(context, PARSE_NODE_EXPR_STATEMENT, semi);
    stmt->as.expr_stmt.expr = pop_node(context);

    push_node(context, stmt);
  }

  push_state(context, (ParseState){ .kind = STATE_BLOCK_STMT_BEGIN });

  return true;
}

static bool handle_IF_BEGIN(Context* context, ParseState state) {
  (void)state;

  Token token = peek(context);
  REQUIRE(context, TOKEN_KEYWORD_IF, "expected 'if' statement");


  push_state(context, (ParseState){
    .kind=STATE_IF_ACCEPT,
    .as.if_accept.if_token = token
  });

  push_state(context, (ParseState){.kind=STATE_ELSE_BEGIN});
  push_state(context, (ParseState){.kind=STATE_BLOCK_BEGIN});
  push_state(context, (ParseState){.kind=STATE_EXPR});

  return true;
}

static bool handle_ELSE_BEGIN(Context* context, ParseState state) {
  (void)state;

  if (peek(context).kind == TOKEN_KEYWORD_ELSE) {
    Token else_token = lex(context);                        

    push_state(context, (ParseState){
      .kind=STATE_ELSE_ACCEPT,
      .as.else_accept.else_token = else_token
    });

    if (peek(context).kind == TOKEN_KEYWORD_IF) {
      push_state(context, (ParseState){.kind=STATE_IF_BEGIN});
    }
    else {
      push_state(context, (ParseState){.kind=STATE_BLOCK_BEGIN});
    }
  }

  return true;
}

static bool handle_ELSE_ACCEPT(Context* context, ParseState state) {
  Token token = state.as.else_accept.else_token;

  ParseNode* else_block = pop_node(context);
  ParseNode* then_block = pop_node(context);

  ParseNode* node = new_node(context, PARSE_NODE_ELSE, token);
  node->as.else_.first = then_block;
  node->as.else_.second = else_block;

  push_node(context, node);

  return true;
}

static bool handle_IF_ACCEPT(Context* context, ParseState state) {
  Token token = state.as.if_accept.if_token;

  ParseNode* body = pop_node(context);
  ParseNode* predicate = pop_node(context);

  ParseNode* node = new_node(context, PARSE_NODE_IF, token);
  node->as.if_.predicate = predicate;
  node->as.if_.body = body;

  push_node(context, node);

  return true;
}

#define X(name, ...) [STATE_##name] = handle_##name,
static HandleStateFunc handle_func_table[NUM_STATES] = {
  #include "parse_state.def"
};
#undef X

bool parse(Arena* arena, SourceContents source, TokenizedBuffer tokens, ParseTree* out_parse_tree) {
  Scratch scratch = global_scratch(1, &arena);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  bool return_value = false;

  Context context = {
    .source = source,
    .state_stack = new_dynamic_array(scratch_allocator),
    .node_stack = new_dynamic_array(scratch_allocator),
    .tokens = tokens,
    .nodes = arena_array(arena, ParseNode, tokens.length-1),
    .node_capacity = tokens.length-1
  };

  push_state(&context, (ParseState){.kind = STATE_BLOCK_BEGIN});

  while (dynamic_array_length(context.state_stack)) {
    ParseState state = dynamic_array_pop(context.state_stack);
    HandleStateFunc handle = handle_func_table[state.kind];

    if (!handle(&context, state)) {
      goto end;
    }
  }

  assert(context.num_nodes == context.node_capacity);

  memset(out_parse_tree, 0, sizeof(*out_parse_tree));
  out_parse_tree->num_nodes = context.num_nodes;
  out_parse_tree->nodes = context.nodes;

  return_value = true;

  end:
  scratch_release(&scratch);
  return return_value;
}