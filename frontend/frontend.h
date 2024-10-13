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
  TOKEN_KEYWORD_RETURN,

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
  #include "parser/parse_node_kind.def"
  NUM_PARSE_NODE_KINDS
} ParseNodeKind;
#undef X

#define X(name, debug_name, ...) debug_name,
static char* parse_node_debug_name[NUM_PARSE_NODE_KINDS] = {
  "<ERROR>",
  #include "parser/parse_node_kind.def"
};
#undef X

#define PARSE_NODE_INLINE_CHILD_CAPACITY 4

typedef struct ParseNode ParseNode;
struct ParseNode {
  ParseNode* prev;
  ParseNodeKind kind;
  Token token;

  int subtree_size;
  int num_children;
};

typedef struct {
  int num_nodes;
  ParseNode* nodes;
} ParseTree;

#define X(name, ...) SEM_OP_##name,
typedef enum {
  SEM_OP_INVALID,
  #include "sem/sem_op.def"
  NUM_SEM_OPS
} SemOp;
#undef X

#define X(name, str, ...) str,
static char* sem_op_debug_name[NUM_SEM_OPS] = {
  "<ERROR>",
  #include "sem/sem_op.def"
};
#undef X

typedef struct SemInstr SemInstr;
typedef struct SemBlock SemBlock;

struct SemInstr {
  SemInstr *prev, *next;

  SemOp op;

  union {
    uint64_t int_const;

    struct {
      String name;
    } local;

    SemInstr* bin[2];

    SemBlock* jmp_loc;

    struct {
      SemInstr* predicate;
      SemBlock* locs[2];
    } branch;

    struct {
      SemInstr* value;
    } ret;
  } as;
};

struct SemBlock {
  SemBlock* next;
  SemInstr *start, *end;
};

typedef struct {
  SemBlock* entry;
} SemFunction;

SourceContents load_source(Arena* arena, char* path);

TokenizedBuffer tokenize(Arena* arena, SourceContents source);

bool parse(Arena* arena, SourceContents source, TokenizedBuffer tokens, ParseTree* out_parse_tree);

void dump_parse_tree(ParseTree tree);

void error_at_token(char* source_path, char* source, Token token, char* fmt, ...);

SemFunction* sem_generate(Arena* arena, ParseTree parse_tree);