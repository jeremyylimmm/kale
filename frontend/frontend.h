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

typedef struct AST AST;

struct AST {
  ASTKind kind;
  Token token;

  int num_children;
  AST** children;
};

#define X(name, ...) SEM_OP_##name,
typedef enum {
  SEM_OP_INVALID,
  #include "sem/op.def"
} SemOp;
#undef X

#define X(name, str, ...) str,
static char* sem_op_str[] = {
  "<INVALID>",
  #include "sem/op.def"
};
#undef X

typedef struct SemInst SemInst;

#define SEM_MAX_INS 4

struct SemInst {
  int block;
  SemInst* prev;
  SemInst* next;

  SemOp op;
  int def;
  Token token;

  int num_ins;
  SemInst* ins[SEM_MAX_INS];

  void* data;
};

typedef struct {
  SemInst* start;
  SemInst* end;
} SemBlock;

typedef struct {
  String name;
  DynamicArray(SemBlock) blocks;
} SemFunc;

typedef struct {
  int num_funcs;
  SemFunc* funcs;
} SemFile;

typedef struct {
  Arena* arena;
  Allocator* allocator;
} SemContext;

SourceContents load_source(Arena* arena, char* path);

TokenizedBuffer tokenize(Arena* arena, SourceContents source);

void error_at_token(SourceContents source, Token token, char* fmt, ...);

AST* parse(Arena* arena, SourceContents source, TokenizedBuffer* tokens);
void ast_dump(AST* ast);

SemContext* sem_init(Arena* arena);
SemFile* check_ast(SemContext* context, SourceContents source, AST* ast);
uint64_t* sem_reachable(Arena* arena, SemFunc* func);
bool sem_analyze(SemContext* context, SourceContents source, SemFile* file);
void sem_dump(SemFile* file);