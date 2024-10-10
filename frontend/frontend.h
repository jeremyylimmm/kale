#pragma once

#include "utils.h"

typedef struct {
  char* contents;
  char* path;
} SourceContents;

enum {
  TOKEN_EOF,

  TOKEN_IDENTIFIER = 256,
  TOKEN_INTEGER_LITERAL,

  TOKEN_KEYWORD_IF,
  TOKEN_KEYWORD_ELSE,
  TOKEN_KEYWORD_WHILE,

  NUM_TOKEN_KINDS
};

typedef struct {
  int kind;
  int length;
  int line;
  char* start;
} Token;

typedef struct {
  int length;
  Token* tokens;
} TokenizedBuffer;

#define X(name, ...) PARSE_NODE_##name,
typedef enum {
  PARSE_NODE_INVALID,
  #include "parse_node_kind.def"
  NUM_PARSE_NODE_KINDS
} ParseNodeKind;
#undef X

#define X(name, debug_name, ...) debug_name,
static char* parse_node_debug_name[NUM_PARSE_NODE_KINDS] = {
  "<ERROR>",
  #include "parse_node_kind.def"
};
#undef X

typedef struct ParseNode ParseNode;
struct ParseNode {
  ParseNode* prev;
  ParseNodeKind kind;
  Token token;

  union {
    struct { ParseNode* lhs; ParseNode* rhs; } bin;
    struct { ParseNode* open; ParseNode* tail_stmt; } block;
    struct { ParseNode* expr; } expr_stmt;
    struct {
      ParseNode* predicate;
      ParseNode* body;
     } if_;
     struct {
      ParseNode* first;
      ParseNode* second;
     } else_;
  } as;
};

typedef struct {
  int num_nodes;
  ParseNode* nodes;
} ParseTree;

SourceContents load_source(Arena* arena, char* path);

TokenizedBuffer tokenize(Arena* arena, SourceContents source);

bool parse(Arena* arena, SourceContents source, TokenizedBuffer tokens, ParseTree* out_parse_tree);

void dump_parse_tree(ParseTree tree);

void error_at_token(char* source_path, char* source, Token token, char* fmt, ...);