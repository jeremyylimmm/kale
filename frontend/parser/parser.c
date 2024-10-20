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
  } as;
} State;

typedef struct {
  SourceContents source;

  TokenizedBuffer* token_buffer;
  int cur_token;

  DynamicArray(AST) nodes;
  DynamicArray(State) stack;

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

static State basic_state(StateKind kind) {
  return (State) {
    .kind = kind
  };
}

static void push(Parser* p, State state) {
  dynamic_array_put(p->stack, state);
}

static void new_node(Parser* p, ASTKind kind, Token token, int num_children) {
  int subtree_size = 1;
  int child = dynamic_array_length(p->nodes)-1;

  for_range_rev (int, i, num_children) {
    assert(child >= 0);
    AST* n = &p->nodes[child];

    subtree_size += n->subtree_size;
    child -= n->subtree_size;
  }

  AST node = {
    .token = token,
    .kind = kind,
    .num_children = num_children,
    .subtree_size = subtree_size
  };

  dynamic_array_put(p->nodes, node); 
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
  }
}

static bool do_BINARY(Parser* p) {
  push(p, (State) {
    .kind = STATE_BINARY_INFIX,
    .as.bin_infix.cur_prec = p->state.as.bin.cur_prec
  });

  push(p, basic_state(STATE_PRIMARY));

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
  }
}

static bool do_BINARY_INFIX(Parser* p) {
  Token op = p->state.as.bin_infix.op;

  if (op.start) {
    new_node(p, binary_kind(op), op, 2);
  }

  if (binary_prec(peek(p)) > p->state.as.bin_infix.cur_prec) {
    Token next_op = lex(p);

    push(p, (State) {
      .kind = STATE_BINARY_INFIX,
      .as.bin_infix.cur_prec = p->state.as.bin_infix.cur_prec,
      .as.bin_infix.op = next_op
    });

    push(p, (State) {
      .kind = STATE_BINARY,
      .as.bin.cur_prec = binary_prec(next_op)
    });
  }

  return true;
}

static bool do_EXPR(Parser* p) {
  push(p, (State) {
    .kind = STATE_BINARY,
    .as.bin.cur_prec = 0
  });

  return true;
}

ASTBuffer* parse(Arena* arena, SourceContents source, TokenizedBuffer* tokens) {
  Scratch scratch = global_scratch(1, &arena);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  ASTBuffer* ast_buffer = NULL;

  Parser p = {
    .source = source,
    .token_buffer = tokens,
    .nodes = new_dynamic_array(scratch_allocator),
    .stack = new_dynamic_array(scratch_allocator)
  };

  push(&p, basic_state(STATE_EXPR));

  while (dynamic_array_length(p.stack)) {
    p.state = dynamic_array_pop(p.stack);

    bool result = false;
    
    #define X(name, ...) case STATE_##name: result = do_##name(&p); break;
    switch (p.state.kind) {
      default:
        assert(false);
        break;
      #include "state.def"
    }
    #undef X

    if (!result) {
      goto end;
    }
  }

  ast_buffer = arena_type(arena, ASTBuffer);
  ast_buffer->count = dynamic_array_length(p.nodes);
  ast_buffer->nodes = dynamic_array_bake(arena, p.nodes);

  end:
  scratch_release(&scratch);
  return ast_buffer;
}