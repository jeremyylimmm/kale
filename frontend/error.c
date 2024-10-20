#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#include "frontend.h"

static void find_line(char* source, int line, char** out_start, int* out_length) {
  for (int cur = 1; cur < line; ++cur) {
    while (*source != '\n' && *source != '\0') {
      ++source;
    }

    assert(*source == '\n');
    source++;
  }

  while (isspace(*source)) {
    ++source;
  }

  int length = 0;
  while (source[length] != '\0' && source[length] != '\n') {
    length++;
  }

  *out_start = source;
  *out_length = length;
}

void error_at_token(SourceContents source, Token token, char* fmt, ...) {
  char* line_start;
  int line_length;
  find_line(source.contents, token.line, &line_start, &line_length);

  int offset = fprintf(stderr, "%s(%d): error: ", source.path, token.line);
  fprintf(stderr, "%.*s\n", line_length, line_start);

  offset += (int)(token.start - line_start);

  fprintf(stderr, "%*s^ ", offset, "");

  va_list ap;
  va_start(ap, fmt);

  vfprintf(stderr, fmt, ap);

  va_end(ap);

  fprintf(stderr, "\n");
}