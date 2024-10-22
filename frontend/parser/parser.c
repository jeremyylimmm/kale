#include "frontend.h"
#include "dynamic_array.h"

#define X(name, ...) STATE_##name,
typedef enum {
  STATE_INVALID,
  #include "state.def"
} StateKind;
#undef X

typedef struct {
  StateKind kind;

  union {
    struct {
      int cur_prec;
      Token op;
    } bin_infix;

    struct {
      int cur_prec;
    } bin;

    struct {
      Token if_token;
    } els;

    struct {
      Token lbrace;
      int num_children;
    } block_stmt;

    struct {
      ASTKind kind;
      Token token;
      int num_children;
    } complete;

    struct {
      int count;
    } top_level;
  } as;
} State;

typedef struct {
  Arena* node_arena;
  SourceContents source;

  TokenizedBuffer* token_buffer;
  int cur_token;

  DynamicArray(State) state_stack;
  DynamicArray(AST*) node_stack;

  State state;
} Parser;

static Token peekn(Parser* p, int offset) {
  int index = p->cur_token + offset;

  if (index >= p->token_buffer->length) {
    index = p->token_buffer->length - 1;
  }

  return p->token_buffer->tokens[index];
}

static Token peek(Parser* p) {
  return peekn(p, 0);
}

static Token lex(Parser* p) {
  Token tok = peek(p);

  if (p->cur_token < p->token_buffer->length-1) {
    p->cur_token++;
  }

  return tok;
}

static bool match(Parser* p, int token_kind, char* message) {
  if (peek(p).kind != token_kind) {
    error_at_token(p->source, peek(p), message);
    return false;
  }

  lex(p);
  return true;
}

#define REQUIRE(p, token_kind, message) \
  do { \
    if (!match(p, token_kind, message)) { \
      return false; \
    } \
  } while (false)

static State basic_state(StateKind kind) {
  return (State) {
    .kind = kind
  };
}

static State complete(ASTKind kind, Token token, int num_children) {
  return (State) {
    .kind = STATE_COMPLETE,
    .as.complete.kind = kind,
    .as.complete.token = token,
    .as.complete.num_children = num_children
  };
}

static void push_state(Parser* p, State state) {
  dynamic_array_put(p->state_stack, state);
}

static void new_node(Parser* p, ASTKind kind, Token token, int num_children) {
  AST* node = arena_type(p->node_arena, AST);

  node->kind = kind;
  node->token = token;
  node->num_children = num_children;

  node->children = arena_array(p->node_arena, AST*, num_children);

  for_range_rev(int,i, num_children) {
    node->children[i] = dynamic_array_pop(p->node_stack);
  }

  dynamic_array_put(p->node_stack, node); 
}

static void new_leaf(Parser* p, ASTKind kind, Token token) {
  new_node(p, kind, token, 0);
}

static bool do_PRIMARY(Parser* p) {
  switch (peek(p).kind) {
    default:
      error_at_token(p->source, peek(p), "expected an expression");
      return false;
    case TOKEN_INTEGER_LITERAL:
      new_leaf(p, AST_INT_LITERAL, lex(p));
      return true;
    case TOKEN_IDENTIFIER:
      new_leaf(p, AST_IDENTIFIER, lex(p));
      return true;
  }
}

static bool do_BINARY(Parser* p) {
  push_state(p, (State) {
    .kind = STATE_BINARY_INFIX,
    .as.bin_infix.cur_prec = p->state.as.bin.cur_prec
  });

  push_state(p, basic_state(STATE_PRIMARY));

  return true;
}

static int binary_prec(Token op, bool caller) {
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
      return 5 - (caller ? 1 : 0); // Right recursive
  }
}

static ASTKind binary_kind(Token op) {
  switch (op.kind) {
    default:
      assert(false);
      return AST_INVALID;
    case '*':
      return AST_MUL;
    case '/':
      return AST_DIV;
    case '+':
      return AST_ADD;
    case '-':
      return AST_SUB;
    case '=':
      return AST_ASSIGN;
  }
}

static bool do_BINARY_INFIX(Parser* p) {
  Token op = p->state.as.bin_infix.op;

  if (op.start) {
    new_node(p, binary_kind(op), op, 2);
  }

  if (binary_prec(peek(p), false) > p->state.as.bin_infix.cur_prec) {
    Token next_op = lex(p);

    push_state(p, (State) {
      .kind = STATE_BINARY_INFIX,
      .as.bin_infix.cur_prec = p->state.as.bin_infix.cur_prec,
      .as.bin_infix.op = next_op
    });

    push_state(p, (State) {
      .kind = STATE_BINARY,
      .as.bin.cur_prec = binary_prec(next_op, true)
    });
  }

  return true;
}

static bool do_COMPLETE(Parser* p) {
  new_node(p, p->state.as.complete.kind, p->state.as.complete.token, p->state.as.complete.num_children);
  return true;
}

static bool do_EXPR(Parser* p) {
  push_state(p, (State) {
    .kind = STATE_BINARY,
    .as.bin.cur_prec = 0
  });

  return true;
}

static bool do_BLOCK(Parser* p) {
  Token lbrace = peek(p);
  REQUIRE(p, '{', "expected a block '{'");

  push_state(p, (State){
    .kind = STATE_BLOCK_STMT,
    .as.block_stmt.lbrace = lbrace
  });
  
  return true;
}

static bool do_BLOCK_STMT(Parser* p) {
  switch (peek(p).kind) {
    case '}':
      lex(p);
      new_node(p, AST_BLOCK, p->state.as.block_stmt.lbrace, p->state.as.block_stmt.num_children);
      return true;
    case TOKEN_EOF:
      error_at_token(p->source, p->state.as.block_stmt.lbrace, "this brace has no closing brace");
      return false;
  }

  p->state.as.block_stmt.num_children++;
  push_state(p, p->state);

  switch (peek(p).kind) {
    default:
      push_state(p, basic_state(STATE_SEMI));
      push_state(p, basic_state(STATE_EXPR));
      break;

    case TOKEN_IDENTIFIER:
      push_state(p, basic_state(STATE_SEMI));

      if (peekn(p, 1).kind == ':') {
        push_state(p, basic_state(STATE_LOCAL));
      }
      else {
        push_state(p, basic_state(STATE_EXPR));
      }
      break;

    case '{':
      push_state(p, basic_state(STATE_BLOCK));
      break;

    case TOKEN_KEYWORD_RETURN:
      push_state(p, complete(AST_RETURN, lex(p), 1));
      push_state(p, basic_state(STATE_SEMI));
      push_state(p, basic_state(STATE_EXPR));
      break;

    case TOKEN_KEYWORD_WHILE:
      push_state(p, basic_state(STATE_WHILE));
      break;

    case TOKEN_KEYWORD_IF:
      push_state(p, basic_state(STATE_IF));
      break;

    case TOKEN_KEYWORD_ELSE:
      error_at_token(p->source, peek(p), "an else statement must follow an if '{}' body");
      return false;
  }

  return true;
}

static bool do_WHILE(Parser* p) {
  Token while_tok = peek(p);
  REQUIRE(p, TOKEN_KEYWORD_WHILE, "expected a 'while' loop");

  push_state(p, complete(AST_WHILE, while_tok, 2));
  push_state(p, basic_state(STATE_BLOCK));
  push_state(p, basic_state(STATE_EXPR));

  return true;
}

static bool do_IF(Parser* p) {
  Token if_tok = peek(p);
  REQUIRE(p, TOKEN_KEYWORD_IF, "expected a 'if' statement");

  push_state(p, (State){
    .kind = STATE_ELSE,
    .as.els.if_token = if_tok
  });

  push_state(p, basic_state(STATE_BLOCK));
  push_state(p, basic_state(STATE_EXPR));

  return true;
}

static bool do_ELSE(Parser* p) {
  if (peek(p).kind == TOKEN_KEYWORD_ELSE) {
    lex(p);
    push_state(p, complete(AST_IF, p->state.as.els.if_token, 3));

    switch (peek(p).kind) {
      default:
        error_at_token(p->source, peek(p), "only an if statement or a '{}' block can follow an else statement");
        return false;
      case '{':
        push_state(p, basic_state(STATE_BLOCK));
        break;
      case TOKEN_KEYWORD_IF:
        push_state(p, basic_state(STATE_IF));
        break;
    }
  }
  else {
    new_node(p, AST_IF, p->state.as.els.if_token, 2);
  }

  return true;
}

static bool do_SEMI(Parser* p) {
  REQUIRE(p, ';', "expected ';'");
  return true;
}

static bool do_LOCAL(Parser* p) {
  Token name_tok = peek(p);
  REQUIRE(p, TOKEN_IDENTIFIER, "expected a local declaration, so expected a name here");

  Token colon_tok = peek(p);
  REQUIRE(p, ':', "expected a local declaration, so expected a ':' here");

  Token type_tok = peek(p);
  REQUIRE(p, TOKEN_IDENTIFIER, "expected a local declaration, so expected a typename here");

  new_leaf(p, AST_IDENTIFIER, name_tok);
  new_leaf(p, AST_IDENTIFIER, type_tok);

  new_node(p, AST_LOCAL, colon_tok, 2);

  if (peek(p).kind == '=') {
    Token equal_tok = lex(p);
    push_state(p, complete(AST_INITIALIZE, equal_tok, 2));
    push_state(p, basic_state(STATE_EXPR));
  }

  return true;
}

static bool do_FN(Parser* p) {
  Token fn_tok = peek(p);
  REQUIRE(p, TOKEN_KEYWORD_FN, "expected a function");

  Token name_tok = peek(p);
  REQUIRE(p, TOKEN_IDENTIFIER, "there must be a name after the 'fn' keyword");

  new_leaf(p, AST_IDENTIFIER, name_tok);
  push_state(p, complete(AST_FN, fn_tok, 2));
  push_state(p, basic_state(STATE_BLOCK));

  return true;
}

static bool do_TOP_LEVEL(Parser* p) {
  if (peek(p).kind == TOKEN_EOF) {
    new_node(p, AST_FILE, peek(p), p->state.as.top_level.count);
    return true;
  }

  push_state(p, (State){
    .kind = STATE_TOP_LEVEL,
    .as.top_level.count = p->state.as.top_level.count + 1,
  });

  switch (peek(p).kind) {
    default:
      error_at_token(p->source, peek(p), "expected a top-level statement; struct, fn, etc...");
      return false;

    case TOKEN_KEYWORD_FN:
      push_state(p, basic_state(STATE_FN));
      return true;
  }
}

AST* parse(Arena* arena, SourceContents source, TokenizedBuffer* tokens) {
  Scratch scratch = global_scratch(1, &arena);

  AST* result = NULL;

  Parser p = {
    .node_arena = arena,
    .source = source,
    .token_buffer = tokens,
    .state_stack = new_dynamic_array(scratch.allocator),
    .node_stack = new_dynamic_array(scratch.allocator),
  };

  push_state(&p, basic_state(STATE_TOP_LEVEL));

  while (dynamic_array_length(p.state_stack)) {
    p.state = dynamic_array_pop(p.state_stack);

    bool state_result = false;
    
    #define X(name, ...) case STATE_##name: state_result = do_##name(&p); break;
    switch (p.state.kind) {
      default:
        assert(false);
        break;
      #include "state.def"
    }
    #undef X

    if (!state_result) {
      goto end;
    }
  }

  result = dynamic_array_pop(p.node_stack);

  end:
  scratch_release(&scratch);
  return result;
}