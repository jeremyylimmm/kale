#pragma once

#include "base.h"

typedef struct {
  char* contents;
  char* path;
} SourceContents;

enum {
  TOKEN_EOF,

  TOKEN_IDENTIFIER = 256,
  TOKEN_INTEGER_LITERAL
};

typedef struct {
  int kind;
  int length;
  int line;
  char* start;
} Token;

SourceContents load_source(Arena* arena, char* path);

typedef struct {
  int length;
  Token* tokens;
} TokenizedBuffer;

TokenizedBuffer tokenize(SourceContents source);
