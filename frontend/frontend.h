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

SourceContents load_source(Arena* arena, char* path);

TokenizedBuffer tokenize(Arena* arena, SourceContents source);

void error_at_token(char* source_path, char* source, Token token, char* fmt, ...);