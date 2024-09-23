#include <stdio.h>
#include <stdlib.h>

#include "frontend.h"

SourceContents load_source(Arena* arena, char* path) {
  FILE* file = fopen(path, "r");

  if (!file) {
    fprintf(stderr, "Missing file '%s'\n", path);
    exit(1);
  }

  fseek(file, 0, SEEK_END);
  int source_length = ftell(file);
  rewind(file);

  char* source = arena_push(arena, source_length + 1);
  source_length = (int)fread(source, 1, source_length, file);
  source[source_length] = '\0';

  fclose(file);

  return (SourceContents) {
    .contents = source,
    .path = copy_cstr(arena, path).str,
  };
}