#include <ctype.h>

#include "frontend.h"
#include "dynamic_array.h"

static int isident(int c) {
  return c == '_' || isalnum(c);
}

TokenizedBuffer tokenize(Arena* arena, SourceContents source) {
  Scratch scratch = global_scratch(1, &arena);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  DynamicArray(Token) tokens = new_dynamic_array(scratch_allocator);

  char* cur_char = source.contents;
  int cur_line = 1;

  while (true) {
    while (isspace(*cur_char)) {
      if (*cur_char == '\n') {
        ++cur_line;
      }
      ++cur_char;
    }

    if (*cur_char == '\0') {
      break;
    }

    char* start = cur_char++;
    int kind = *start;
    int line = cur_line;

    switch (*start) {
      default:
        if (isdigit(*start)) {
          while (isdigit(*cur_char)) {
            ++cur_char;
          }
          kind = TOKEN_INTEGER_LITERAL;
        }
        else if (isident(*start)) {
          while (isident(*cur_char)) {
            ++cur_char;
          }
          kind = TOKEN_IDENTIFIER;
        }
        break;
    }

    Token token = {
      .kind = kind,
      .line = line,
      .length = (int)(cur_char - start),
      .start = start
    };

    dynamic_array_put(tokens, token);
  }

  Token eof_token = {
    .kind = TOKEN_EOF,
    .line = cur_line,
    .length = 0,
    .start = cur_char
  };

  dynamic_array_put(tokens, eof_token);

  TokenizedBuffer result;
  result.length = dynamic_array_length(tokens);
  result.tokens = arena_array(arena, Token, result.length);
  memcpy(result.tokens, tokens, result.length * sizeof(Token));

  scratch_release(&scratch);

  return result;
}