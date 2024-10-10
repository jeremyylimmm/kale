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
    struct { int prec; } binary;
    struct { int prec; } binary_infix;
    struct { Token op; int prec; } binary_accept;
    struct { bool require_semicolon; } block_stmt_accept;
    struct { Token if_token; } if_accept;
    struct { Token else_token; } else_accept;
    struct { Token while_token; } while_accept;
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

static bool handle_BINARY(Context* context, ParseState state) {
  push_state(context, (ParseState) {
    .kind = STATE_BINARY_INFIX,
    .as.binary_infix.prec = state.as.binary.prec
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
      .kind = STATE_BINARY,
      .as.binary.prec = binary_prec(op)
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
    .kind = STATE_BINARY,
    .as.binary.prec = 0
  });

  return true;
}

static bool handle_BLOCK(Context* context, ParseState state) {
  (void)state;

  Token token = peek(context);
  REQUIRE(context, '{', "expected a block '{'");

  ParseNode* open = new_node(context, PARSE_NODE_BLOCK_OPEN, token);
  push_node(context, open);

  push_state(context, (ParseState) {
    .kind = STATE_BLOCK_STMT
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

static bool handle_BLOCK_STMT(Context* context, ParseState state) {
  (void)state;

  if (peek(context).kind == '}') {
    close_block(context);
    return true;
  }

  bool require_semicolon = false;
  ParseState stmt_state = {0};

  switch (peek(context).kind) {
    default:
      require_semicolon = true;
      stmt_state = (ParseState) { .kind = STATE_EXPR };
      break;

    case TOKEN_IDENTIFIER:
      require_semicolon = true;
      if (peekn(context, 1).kind == ':') {
        stmt_state = (ParseState) { .kind = STATE_LOCAL_DECL };
      }
      else {
        stmt_state = (ParseState) { .kind = STATE_EXPR };
      }
      break;

    case '{':
      stmt_state = (ParseState) { .kind = STATE_BLOCK};
      break;
    
    case TOKEN_KEYWORD_IF:
      stmt_state = (ParseState) { .kind = STATE_IF};
      break;

    case TOKEN_KEYWORD_WHILE:
      stmt_state = (ParseState) { .kind = STATE_WHILE};
      break;
  }

  push_state(context, (ParseState) {
    .kind = STATE_BLOCK_STMT_ACCEPT,
    .as.block_stmt_accept.require_semicolon = require_semicolon
  });

  push_state(context, stmt_state);

  return true;
}

static bool handle_BLOCK_STMT_ACCEPT(Context* context, ParseState state) {
  (void)state;

  if (state.as.block_stmt_accept.require_semicolon) {
    Token semi = peek(context);
    REQUIRE(context, ';', "expected a semi-colon ';'");

    ParseNode* stmt = new_node(context, PARSE_NODE_SEMICOLON_STATEMENT, semi);
    stmt->as.semi_stmt.child = pop_node(context);

    push_node(context, stmt);
  }

  push_state(context, (ParseState){ .kind = STATE_BLOCK_STMT});

  return true;
}

static bool handle_IF(Context* context, ParseState state) {
  (void)state;

  Token token = peek(context);
  REQUIRE(context, TOKEN_KEYWORD_IF, "expected 'if' statement");


  push_state(context, (ParseState){
    .kind=STATE_IF_ACCEPT,
    .as.if_accept.if_token = token
  });

  push_state(context, (ParseState){.kind=STATE_ELSE});
  push_state(context, (ParseState){.kind=STATE_BLOCK});
  push_state(context, (ParseState){.kind=STATE_EXPR});

  return true;
}

static bool handle_ELSE(Context* context, ParseState state) {
  (void)state;

  if (peek(context).kind == TOKEN_KEYWORD_ELSE) {
    Token else_token = lex(context);                        

    push_state(context, (ParseState){
      .kind=STATE_ELSE_ACCEPT,
      .as.else_accept.else_token = else_token
    });

    if (peek(context).kind == TOKEN_KEYWORD_IF) {
      push_state(context, (ParseState){.kind=STATE_IF});
    }
    else {
      push_state(context, (ParseState){.kind=STATE_BLOCK});
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

static bool handle_WHILE(Context* context, ParseState state) {
  (void)state;

  Token token = peek(context);
  REQUIRE(context, TOKEN_KEYWORD_WHILE, "expected a 'while' loop");

  push_state(context, (ParseState){
    .kind = STATE_WHILE_ACCEPT,
    .as.while_accept.while_token = token
  });

  push_state(context, (ParseState) {.kind=STATE_BLOCK});
  push_state(context, (ParseState) {.kind=STATE_EXPR});

  return true;
}

static bool handle_WHILE_ACCEPT(Context* context, ParseState state) {
  Token while_token = state.as.while_accept.while_token;

  ParseNode* body = pop_node(context);
  ParseNode* predicate = pop_node(context);

  ParseNode* node = new_node(context, PARSE_NODE_WHILE, while_token);
  node->as.while_.predicate = predicate;
  node->as.while_.body = body;

  push_node(context, node);

  return true;
}

static bool handle_LOCAL_DECL(Context* context, ParseState state) {
  (void)state;

  ParseNode* name = new_node(context, PARSE_NODE_IDENTIFIER, peek(context));
  REQUIRE(context, TOKEN_IDENTIFIER, "expected a local declaration");

  Token colon = peek(context);
  REQUIRE(context, ':', "expected a local declaration");

  ParseNode* type = new_node(context, PARSE_NODE_IDENTIFIER, peek(context));
  REQUIRE(context, TOKEN_IDENTIFIER, "expected a type name");

  ParseNode* node = new_node(context, PARSE_NODE_LOCAL_DECL, colon);
  node->as.local_decl.name = name; 
  node->as.local_decl.type = type; 

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

  push_state(&context, (ParseState){.kind = STATE_BLOCK});

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