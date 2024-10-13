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
    struct { int stmt_count; } block_stmt;
    struct { bool require_semicolon; int stmt_count; } block_stmt_accept;
    struct { Token if_token; } if_accept;
    struct { Token else_token; } else_accept;
    struct { Token while_token; } while_accept;
    struct { Token return_token; } return_accept;
  } as;
} ParseState;

typedef struct {
  Arena* arena;
  SourceContents source;

  DynamicArray(ParseState) state_stack;
  
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

static ParseNode* new_node(Context* context, ParseNodeKind kind, Token token, int num_children) {
  assert(context->num_nodes < context->node_capacity);

  int index = context->num_nodes++;

  ParseNode* node = &context->nodes[index];
  node->kind = kind;
  node->token = token;
  node->num_children = num_children;

  int subtree_size = 1;

  int child = index - 1;

  for (int i = 0; i < num_children; ++i) {
    assert(child >= 0);
    ParseNode* c = &context->nodes[child];
    subtree_size += c->subtree_size;
    child -= c->subtree_size;
  }

  node->subtree_size = subtree_size;

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
      new_node(context, PARSE_NODE_INTEGER_LITERAL, lex(context), 0);
      return true;
    }

    case TOKEN_IDENTIFIER: {
      new_node(context, PARSE_NODE_IDENTIFIER, lex(context), 0);
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

static int binary_prec(Token op, bool inner) {
  switch (op.kind) {
    default:
      return 0;
    case '*':
    case '/':
      return 20;
    case '+':
    case '-':
      return 10;
    case '=':
      return 5 - (inner ? 1 : 0);
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
    case '=':
      return PARSE_NODE_ASSIGN;
  }
}

static bool handle_BINARY_INFIX(Context* context, ParseState state) {
  if (binary_prec(peek(context), false) > state.as.binary_infix.prec) {
    Token op = lex(context);

    push_state(context, (ParseState) {
      .kind = STATE_BINARY_ACCEPT,
      .as.binary_accept.op = op,
      .as.binary_accept.prec = state.as.binary_infix.prec
    });

    push_state(context, (ParseState){
      .kind = STATE_BINARY,
      .as.binary.prec = binary_prec(op, true)
    });
  }

  return true;
}

static bool handle_BINARY_ACCEPT(Context* context, ParseState state) {
  Token op = state.as.binary_accept.op;

  new_node(context, binary_node_kind(op), op, 2);

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

  new_node(context, PARSE_NODE_BLOCK_OPEN, token, 0);

  push_state(context, (ParseState) {
    .kind = STATE_BLOCK_STMT,
    .as.block_stmt.stmt_count = 0
  });

  return true;
}

static bool handle_BLOCK_STMT(Context* context, ParseState state) {
  if (peek(context).kind == '}') {
    new_node(context, PARSE_NODE_BLOCK, lex(context), state.as.block_stmt.stmt_count + 1);
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
      stmt_state = (ParseState) { .kind = STATE_BLOCK };
      break;
    
    case TOKEN_KEYWORD_IF:
      stmt_state = (ParseState) { .kind = STATE_IF };
      break;

    case TOKEN_KEYWORD_WHILE:
      stmt_state = (ParseState) { .kind = STATE_WHILE };
      break;

    case TOKEN_KEYWORD_RETURN:
      require_semicolon = true;
      stmt_state = (ParseState) { .kind = STATE_RETURN };
      break;
  }

  push_state(context, (ParseState) {
    .kind = STATE_BLOCK_STMT_ACCEPT,
    .as.block_stmt_accept.require_semicolon = require_semicolon,
    .as.block_stmt_accept.stmt_count = state.as.block_stmt.stmt_count + 1
  });

  push_state(context, stmt_state);

  return true;
}

static bool handle_BLOCK_STMT_ACCEPT(Context* context, ParseState state) {
  (void)state;

  if (state.as.block_stmt_accept.require_semicolon) {
    Token semi = peek(context);
    REQUIRE(context, ';', "expected a semi-colon ';'");

    new_node(context, PARSE_NODE_SEMICOLON_STATEMENT, semi, 1);
  }

  push_state(context, (ParseState){
    .kind = STATE_BLOCK_STMT,
    .as.block_stmt.stmt_count = state.as.block_stmt_accept.stmt_count
  });

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

  new_node(context, PARSE_NODE_ELSE, token, 2);

  return true;
}

static bool handle_IF_ACCEPT(Context* context, ParseState state) {
  Token token = state.as.if_accept.if_token;

  new_node(context, PARSE_NODE_IF, token, 2);

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

  new_node(context, PARSE_NODE_WHILE, while_token, 2);

  return true;
}

static bool handle_LOCAL_DECL(Context* context, ParseState state) {
  (void)state;

  new_node(context, PARSE_NODE_IDENTIFIER, peek(context), 0);
  REQUIRE(context, TOKEN_IDENTIFIER, "expected a local declaration");

  Token colon = peek(context);
  REQUIRE(context, ':', "expected a local declaration");

  new_node(context, PARSE_NODE_IDENTIFIER, peek(context), 0);
  REQUIRE(context, TOKEN_IDENTIFIER, "expected a type name");

  new_node(context, PARSE_NODE_LOCAL_DECL, colon, 2);

  if (peek(context).kind == '=') {
    push_state(context, (ParseState) {
      .kind=STATE_BINARY_INFIX,
      .as.binary_infix.prec = binary_prec(peek(context), true)
    });
  }

  return true;
}

static bool handle_RETURN(Context* context, ParseState state) {
  (void)state;

  Token token = peek(context);
  REQUIRE(context, TOKEN_KEYWORD_RETURN, "expected a 'return' statement");

  push_state(context, (ParseState) {
    .kind = STATE_RETURN_ACCEPT,
    .as.return_accept.return_token = token
  });

  push_state(context, (ParseState) {.kind=STATE_EXPR});

  return true;
}

static bool handle_RETURN_ACCEPT(Context* context, ParseState state) {
  new_node(context, PARSE_NODE_RETURN, state.as.return_accept.return_token, 1);
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
  assert(context.nodes[context.node_capacity-1].subtree_size = context.node_capacity);

  memset(out_parse_tree, 0, sizeof(*out_parse_tree));
  out_parse_tree->num_nodes = context.num_nodes;
  out_parse_tree->nodes = context.nodes;

  return_value = true;

  end:
  scratch_release(&scratch);
  return return_value;
}