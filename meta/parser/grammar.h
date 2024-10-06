#pragma once

#include <stdint.h>

#include "symbol.h"

#define GRAMMAR_MAX_STRINGS 1024
#define GRAMMAR_MAX_SYMBOLS_PER_PRODUCTION 32

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

uint64_t production_rhs_hash(ProductionRHS* prod);
int production_rhs_equal(ProductionRHS* a, ProductionRHS* b);