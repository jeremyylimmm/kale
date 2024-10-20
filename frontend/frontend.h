#pragma once

#include "utils.h"
#include "dynamic_array.h"

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
  TOKEN_KEYWORD_FN,

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

#define X(name, ...) AST_##name,
typedef enum {
  AST_INVALID,
  #include "parser/ast_kind.def"
  NUM_AST_KINDS
} ASTKind;
#undef X

#define X(name, str, ...) str,
static char* ast_kind_string[] = {
  "<INVALID>",
  #include "parser/ast_kind.def"
};
#undef X

typedef struct {
  ASTKind kind;
  Token token;
  int subtree_size;
  int num_children;
} AST;

typedef struct {
  int count;
  AST* nodes;
} ASTBuffer;

typedef struct {
  AST* node;
  int index;
} ASTChildIterator;

#define X(name, ...) SEM_NODE_##name,
typedef enum {
  SEM_NODE_INVALID,
  #include "sem/node.def"
} SemNodeKind;
#undef X


typedef struct SemNode SemNode;
typedef struct SemBlock SemBlock;

typedef uint32_t SemValue;

#define SEM_MAX_INS 4

struct SemNode {
  SemNodeKind kind;
  SemValue def;

  int num_ins;
  SemValue ins[SEM_MAX_INS];

  void* data;
};

struct SemBlock {
  DynamicArray(SemNode) nodes;
};

typedef struct {
  DynamicArray(SemBlock) blocks;
} SemFunc;

typedef struct {
  DynamicArray(SemFunc) funcs;
} SemFile;

typedef struct {
  Arena* arena;
  Allocator* allocator;
} SemContext;

SourceContents load_source(Arena* arena, char* path);

TokenizedBuffer tokenize(Arena* arena, SourceContents source);

void error_at_token(SourceContents source, Token token, char* fmt, ...);

ASTBuffer* parse(Arena* arena, SourceContents source, TokenizedBuffer* tokens);
void ast_dump(ASTBuffer* ast_buffer);

ASTChildIterator ast_children_begin(AST* node);
bool ast_children_check(ASTChildIterator* it);
void ast_children_next(ASTChildIterator* it);

#define foreach_ast_child(node, it) for (ASTChildIterator it = ast_children_begin(node); ast_children_check(&it); ast_children_next(&it))

typedef struct {
  int count;
  AST** nodes;
} ASTRoots;

ASTRoots ast_get_roots(Arena* arena, ASTBuffer* ast_buffer);

SemContext* sem_init(Arena* arena);
SemFile* sem_translate(SemContext* context, ASTBuffer* ast_buffer);