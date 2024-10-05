#pragma once

#include <stdint.h>

#define GRAMMAR_MAX_STRINGS 1024
#define GRAMMAR_MAX_SYMBOLS_PER_PRODUCTION 32

typedef enum {
  SYM_INVALID,
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

int to_id(Grammar* grammar, char* string);

Grammar* parse_grammar(char* grammar_def);

void pretty_print_symbol(Grammar* grammar, Symbol* sym);

void dump_symbol(Grammar* grammar, Symbol* sym);
void dump_production_rhs(Grammar* grammar, ProductionRHS* prod);
void dump_non_terminal(Grammar* grammar, int nt);
void dump_grammar(Grammar* grammar);

uint64_t symbol_hash(Symbol* symbol);
uint64_t production_rhs_hash(ProductionRHS* prod);

int symbol_equal(Symbol* a, Symbol* b);
int production_rhs_equal(ProductionRHS* a, ProductionRHS* b);