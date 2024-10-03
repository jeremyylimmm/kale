#pragma once

#define GRAMMAR_MAX_STRINGS 1024
#define GRAMMAR_MAX_SYMBOLS_PER_PRODUCTION 32

typedef enum {
  SYM_NON_TERMINAL,
  SYM_CHAR,
  SYM_EOF
} SymbolKind;

typedef struct Symbol Symbol;
struct Symbol {
  SymbolKind kind;

  union {
    char chr;
    int non_terminal;
  } as;
};

typedef struct ProductionRHS ProductionRHS;
struct ProductionRHS { 
  ProductionRHS* next;
  int num_symbols;
  Symbol symbols[GRAMMAR_MAX_SYMBOLS_PER_PRODUCTION];
};

typedef struct {
  char* strings[GRAMMAR_MAX_STRINGS];
  ProductionRHS* first_prod[GRAMMAR_MAX_STRINGS];
} Grammar;

Grammar* parse_grammar(char* grammar_def);

void dump_grammar(Grammar* grammar);