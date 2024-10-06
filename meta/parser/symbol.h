#pragma once

#include <stdio.h>

#include "bitmatrix.h"
#include "../common.h"

#define SYMBOL_SET_CAPACITY 1024

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

typedef struct {
  uint64_t bits[BITSET_WORDS(SYMBOL_SET_CAPACITY)];
  Symbol symbols[SYMBOL_SET_CAPACITY];
} SymbolSet;

static uint64_t symbol_hash(Symbol* sym) {
  switch (sym->kind) {
    default:
      assert(0);
      return 0;
    case SYM_NON_TERMINAL: {
      uint64_t h[] = {
        sym->kind,
        sym->as.non_terminal
      };
      return fnv1a_hash(h, sizeof(h)); 
    }
    case SYM_CHAR: {
      uint64_t h[] = {
        sym->kind,
        sym->as.chr
      };
      return fnv1a_hash(h, sizeof(h)); 
    }
    case SYM_EOF: {
      uint64_t h[] = {
        sym->kind,
      };
      return fnv1a_hash(h, sizeof(h)); 
    }
  }
}

static int symbol_equal(Symbol* a, Symbol* b) {
  if (a->kind != b->kind) {
    return 0;
  }

  switch (a->kind) {
    default:
      assert(0);
      return 0;
    case SYM_NON_TERMINAL:
      return a->as.non_terminal == b->as.non_terminal; 
    case SYM_CHAR:
      return a->as.chr == b->as.chr; 
    case SYM_EOF:
      return 1;
  }
}

static int symbol_set_find(SymbolSet* set, Symbol sym) {
  int i = fnv1a_hash(&sym, sizeof(sym)) % SYMBOL_SET_CAPACITY;

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

static int symbol_set_table_index(SymbolSet* set, Symbol sym) {
  int idx = symbol_set_find(set, sym);
  assert(bitset_query(set->bits, idx));
  return idx;
}