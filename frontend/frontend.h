#pragma once

#include "base.h"

typedef struct {
  char* contents;
  char* path;
} SourceContents;

enum {
  TOKEN_EOF,

  TOKEN_IDENTIFIER = 256,
  TOKEN_INTEGER_LITERAL,

  NUM_TOKEN_KINDS
};

typedef struct {
  int kind;
  int length;
  int line;
  char* start;
} Token;

Scratch global_scratch(int num_conflicts, Arena** conflicts);

SourceContents load_source(Arena* arena, char* path);

typedef struct {
  int length;
  Token* tokens;
} TokenizedBuffer;

#define X(name, ...) AST_##name,
typedef enum {
  AST_INVALID,
  #include "ast_kind.def"
  NUM_AST_KINDS
} ASTKind;
#undef X

#define X(name, str, ...) str,
static char* ast_kind_str[NUM_AST_KINDS] = {
  "<error>",
  #include "ast_kind.def"
};
#undef X

typedef struct AST AST;

struct AST {
  ASTKind kind;
  union {
    uint64_t integer_literal;
    AST* bin[2];
  } as;
};

TokenizedBuffer tokenize(Arena* arena, SourceContents source);

AST* parse(Arena* arena, TokenizedBuffer tokens);
void dump_ast(AST* ast);