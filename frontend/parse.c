#include "frontend.h"
#include "dynamic_array.h"

#define X(name, ...) STATE_##name,
typedef enum {
  #include "parse_state.def"
  NUM_STATES
} StateKind;
#undef X

typedef struct {
  StateKind kind;
  Token token;
} State;

typedef struct {
  Arena* node_arena;

  int cur_token;
  TokenizedBuffer tokens;

  DynamicArray(State) state_stack;
  DynamicArray(AST*) node_stack;
} Parser;

static int _cap_token_index(Parser* p,int index) {
  if (index > p->tokens.length-1) {
    index = p->tokens.length-1;
  }

  return index;
}

static Token peekn(Parser* p, int offset) {
  int index = p->cur_token + offset;
  index = _cap_token_index(p, index);
  return p->tokens.tokens[index];
}

static Token peek(Parser* p) {
  return peekn(p, 0);
}

static Token lex(Parser* p) {
  Token token = peek(p);
  p->cur_token = _cap_token_index(p, p->cur_token + 1);
  return token;
}

static void push_node(Parser* p, AST* node) {
  dynamic_array_put(p->node_stack, node);
}

static AST* pop_node(Parser* p) {
  return dynamic_array_pop(p->node_stack);
}

static State pop_state(Parser* p) {
  return dynamic_array_pop(p->state_stack);
}

#define EMPTY_TOKEN ((Token){0})

static void push_state0(Parser* p, StateKind kind, Token token) {
  State state = {
    .kind = kind,
    .token =token
  };

  dynamic_array_put(p->state_stack, state);
}

static void push_state(Parser* p, StateKind kind) {
  push_state0(p, kind, EMPTY_TOKEN);
}

static AST* new_node(Parser* p, ASTKind kind) {
  assert(kind);
  AST* node = arena_type(p->node_arena, AST);
  node->kind = kind;
  return node;
}

static uint64_t parse_integer(Token token) {
  uint64_t value = 0;

  for_range(int, i, token.length) {
    value *= 10;
    value += token.start[i] - '0';
  }

  return value;
}

typedef bool(*HandleFunc)(Parser*, Token);

static bool handle_primary(Parser* p, Token token) {
  (void)token;

  switch (peek(p).kind) {
    default:
      assert(false);
      return false;

    case TOKEN_INTEGER_LITERAL:  {
      AST* node = new_node(p, AST_INTEGER_LITERAL);
      node->as.integer_literal = parse_integer(lex(p));
      push_node(p, node);
      return true;
    }
  }
}

static ASTKind addition_kind(Token token) {
  switch (token.kind) {
    default:
      return AST_INVALID;
    case '+':
      return AST_ADD;
    case '-':
      return AST_SUB;
  }
}

static ASTKind factor_kind(Token token) {
  switch (token.kind) {
    default:
      return AST_INVALID;
    case '*':
      return AST_MUL;
    case '/':
      return AST_DIV;
  }
}

static void binary(Parser* p, ASTKind kind) {
  AST* rhs = pop_node(p);
  AST* lhs = pop_node(p);

  AST* node = new_node(p, kind);
  node->as.bin[0] = lhs;
  node->as.bin[1] = rhs;

  push_node(p, node);
}

static bool handle_addition(Parser* p, Token token) {
  (void)token;

  push_state(p, STATE_addition_infix);
  push_state(p, STATE_factor);

  return true;
}

static bool handle_addition_infix(Parser* p, Token token) {
  (void)token;

  if (addition_kind(peek(p))) {
    Token op = lex(p);
    push_state0(p, STATE_addition_postfix, op);
    push_state(p, STATE_factor);
  }

  return true;
}

static bool handle_addition_postfix(Parser* p, Token token) {
  ASTKind op = addition_kind(token);
  binary(p, op);
  push_state(p, STATE_addition_infix);
  return true;
} 

static bool handle_factor(Parser* p, Token token) {
  (void)token;

  push_state(p, STATE_factor_infix);
  push_state(p, STATE_primary);

  return true;
}

static bool handle_factor_infix(Parser* p, Token token) {
  (void)token;

  if (factor_kind(peek(p))) {
    Token op = lex(p);
    push_state0(p, STATE_factor_postfix, op);
    push_state(p, STATE_primary);
  }

  return true;
}

static bool handle_factor_postfix(Parser* p, Token token) {
  ASTKind op = factor_kind(token);
  binary(p, op);
  push_state(p, STATE_factor_infix);
  return true;
} 

static bool handle_expr(Parser* p, Token token) {
  (void)token;
  push_state(p, STATE_addition);
  return true;
}

#define X(name, ...) [STATE_##name] = handle_##name,
HandleFunc handle_table[NUM_STATES] = {
  #include "parse_state.def"
};
#undef X

AST* parse(Arena* arena, TokenizedBuffer tokens) {
  AST* result = NULL;

  Scratch scratch = global_scratch(1, &arena);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  Parser p = {
    .node_arena = arena,
    .tokens = tokens,
    .state_stack = new_dynamic_array(scratch_allocator),
    .node_stack = new_dynamic_array(scratch_allocator),
  };

  push_state(&p, STATE_expr);

  while (dynamic_array_length(p.state_stack)) {
    State state = pop_state(&p);

    HandleFunc handle = handle_table[state.kind];

    if(!handle(&p, state.token)) {
      goto end;
    }
  }

  result = pop_node(&p);

  end:
  scratch_release(&scratch);
  return result;
}