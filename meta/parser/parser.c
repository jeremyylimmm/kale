#include <stdio.h>
#include <stdlib.h>

#include "grammar.h"

int main() {
  FILE* file;
  if (fopen_s(&file, "meta/grammar.txt", "r")) {
    fprintf(stderr, "Failed to load grammar\n");
    return 1;
  }

  fseek(file, 0, SEEK_END);
  size_t file_len = ftell(file);
  rewind(file);

  char* grammar_def = malloc((file_len + 1) * sizeof(char));
  size_t grammar_def_len = fread(grammar_def, 1, file_len, file);
  grammar_def[grammar_def_len] = '\0';

  Grammar* grammar = parse_grammar(grammar_def);
  dump_grammar(grammar);

  return 0;
}