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

TokenizedBuffer tokenize(Arena* arena, SourceContents source);
