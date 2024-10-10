#include <ctype.h>

#include "frontend.h"
#include "dynamic_array.h"

static int check_keyword(char* start, char* end, char* keyword, int kind) {
  size_t length = end-start;
  if (length == strlen(keyword) && memcmp(start, keyword, length) == 0) {
    return kind;
  }
  
  return TOKEN_IDENTIFIER;
}

static int identifier_kind(char* start, char* end) {
  switch (start[0]) {
    default:
      return TOKEN_IDENTIFIER;
    case 'i':
      return check_keyword(start, end, "if", TOKEN_KEYWORD_IF);
    case 'e':
      return check_keyword(start, end, "else", TOKEN_KEYWORD_ELSE);
    case 'w':
      return check_keyword(start, end, "while", TOKEN_KEYWORD_WHILE);
  }
}

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
    while (true) {
      while (isspace(*cur_char)) {
        if (*cur_char == '\n') {
          ++cur_line;
        }
        ++cur_char;
      }

      if (cur_char[0] == '/' && cur_char[1] == '/') {
        while (*cur_char != '\0' && *cur_char != '\n') {
          ++cur_char;
        }
      }
      else {
        break;
      }
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
          kind = identifier_kind(start, cur_char);
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
  result.tokens = dynamic_array_bake(arena, tokens);

  scratch_release(&scratch);

  return result;
}