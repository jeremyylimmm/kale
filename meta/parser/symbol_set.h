#pragma once

#include <stdio.h>

#include "grammar.h"

#define SYMBOL_SET_CAPACITY 1024

typedef struct {
  uint64_t bits[(SYMBOL_SET_CAPACITY + 63)/64];
  Symbol symbols[SYMBOL_SET_CAPACITY];
} SymbolSet;

static int symbol_set_check(SymbolSet* set, int i) {
  return (set->bits[i/64] >> (i % 64)) & 1;
}

static int symbol_set_find(SymbolSet* set, Symbol sym) {
  int i = symbol_hash(&sym) % SYMBOL_SET_CAPACITY;

  for (int j = 0; j < SYMBOL_SET_CAPACITY; ++j) {
    if (!symbol_set_check(set, i)) {
      return i;
    }

    if (symbol_equal(&set->symbols[i], &sym)) {
      return i;
    }

    i = (i + 1) % SYMBOL_SET_CAPACITY;
  }

  fprintf(stderr, "Symbol capacity exceeded.\n");
  exit(1);
}

static void symbol_set_add(SymbolSet* set, Symbol sym) {
  int idx = symbol_set_find(set, sym);

  if (!symbol_set_check(set, idx)) {
    set->symbols[idx] = sym;
    set->bits[idx/64] |= (uint64_t)1 << (idx%64);
  }
}

static int symbol_set_contains(SymbolSet* set, Symbol sym) {
  int idx = symbol_set_find(set, sym);
  return symbol_set_check(set, idx);
}

static void symbol_set_clear(SymbolSet* set) {
  memset(set->bits, 0, sizeof(set->bits));
}

static int symbol_set_get_index(SymbolSet* set, Symbol sym) {
  int idx = symbol_set_find(set, sym);
  assert(symbol_set_check(set, idx));
  return idx;
}
