#pragma once

#include <stdio.h>

#include "grammar.h"
#include "bitmatrix.h"

#define SYMBOL_SET_CAPACITY 1024

typedef struct {
  uint64_t bits[BITSET_WORDS(SYMBOL_SET_CAPACITY)];
  Symbol symbols[SYMBOL_SET_CAPACITY];
} SymbolSet;

static int symbol_set_find(SymbolSet* set, Symbol sym) {
  int i = symbol_hash(&sym) % SYMBOL_SET_CAPACITY;

  for (int j = 0; j < SYMBOL_SET_CAPACITY; ++j) {
    if (!bitset_query(set->bits, i)) {
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

  if (!bitset_query(set->bits, idx)) {
    set->symbols[idx] = sym;
    bitset_set(set->bits, idx);
  }
}

static int symbol_set_contains(SymbolSet* set, Symbol sym) {
  int idx = symbol_set_find(set, sym);
  return bitset_query(set->bits, idx);
}

static void symbol_set_clear(SymbolSet* set) {
  memset(set->bits, 0, sizeof(set->bits));
}

static int symbol_set_get_index(SymbolSet* set, Symbol sym) {
  int idx = symbol_set_find(set, sym);
  assert(bitset_query(set->bits, idx));
  return idx;
}