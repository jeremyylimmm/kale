#pragma once

#include <stdio.h>
#include <memory.h>

#include "grammar.h"
#include "bitmatrix.h"
#include "vec.h"

typedef struct {
  int lhs;
  ProductionRHS* rhs;
  int dot;
  Symbol lookahead;
} Item;

typedef struct ItemSet ItemSet;

struct ItemSet {
  int count;
  int capacity;
  Item* items;
  uint64_t* bits;
};

static uint64_t item_hash(Item* item) {
  uint64_t hashes[] = {
    fnv1a_hash(&item->lhs, sizeof(item->lhs)),
    production_rhs_hash(item->rhs),
    fnv1a_hash(&item->dot, sizeof(item->dot)),
    symbol_hash(&item->lookahead)
  };

  return fnv1a_hash(hashes, sizeof(hashes));
}

static int item_equal(Item* a, Item* b) {
  return (a->lhs == b->lhs) &&
         production_rhs_equal(a->rhs, b->rhs) &&
         (a->dot == b->dot) &&
         symbol_equal(&a->lookahead, &b->lookahead);
}

static int item_set_find(ItemSet* set, Item* item) {
  int i = item_hash(item) % set->capacity;

  for (int j = 0; j < set->capacity; ++j) {
    if (!bitset_query(set->bits, i)) {
      return i;
    }

    if (item_equal(&set->items[i], item)) {
      return i;
    }

    i = (i + 1) % set->capacity;
  }

  fprintf(stderr, "Maximum number of LR items exceeded");
  exit(1);
}

static int item_set_contains(ItemSet* set, Item* item) {
  if (set->count == 0) {
    return 0;
  }

  int idx = item_set_find(set, item);
  return bitset_query(set->bits, idx);
}

static void _item_set_add(ItemSet* set, Item* item) {
  int idx = item_set_find(set, item);

  if (!bitset_query(set->bits, idx)) {
    set->count++;
    set->items[idx] = *item;
    bitset_set(set->bits, idx);
  }
}

static void item_set_add(ItemSet* set, Item* item) {
  if (!set->capacity || (float)set->count >= (float)set->capacity * 0.5f) {
    int new_capacity = set->capacity ? set->capacity * 2 : 8;

    ItemSet temp = {
      .capacity = new_capacity,
      .items = calloc(new_capacity, sizeof(set->items[0])),
      .bits = calloc(BITSET_WORDS(new_capacity), sizeof(uint64_t))
    };

    for (int i = 0; i < set->capacity; ++i) {
      if (bitset_query(set->bits, i)) {
        _item_set_add(&temp, &set->items[i]);
      }
    }

    free(set->items);
    free(set->bits);

    *set = temp;
  }

  _item_set_add(set, item);
}

static int cmp_u64(const void* a, const void* b) {
  return (int)(*(uint64_t*)a > *(uint64_t*)b);
}

static uint64_t hash_item_set(ItemSet* set) {
  int num_hashes = 0;
  uint64_t* hashes = calloc(set->capacity, sizeof(uint64_t));

  for (int i = 0; i < set->capacity; ++i) {
    if (bitset_query(set->bits, i)) {
      hashes[num_hashes++] = item_hash(&set->items[i]);
    }
  }

  qsort(hashes, num_hashes, sizeof(hashes[0]), cmp_u64);

  uint64_t result = fnv1a_hash(hashes, num_hashes * sizeof(hashes[0]));
  
  free(hashes);

  return result;
}

static int item_set_equal(ItemSet* a, ItemSet* b) {
  if (a->count != b->count) {
    return 0;
  }

  for (int i = 0; i < a->capacity; ++i) {
    if (bitset_query(a->bits, i)) {
      if (!item_set_contains(b, &a->items[i])) {
        return 0;
      }
    }
  }

  return 1;
}

static void dump_item(Grammar* grammar, Item* item) {
  printf("[%s -> ", grammar->strings[item->lhs]);

  for (int i = 0; i < item->dot; ++i) {
    if (i > 0) {
      printf(" ");
    }

    pretty_print_symbol(grammar, &item->rhs->symbols[i]);
  }

  printf(".");

  for (int i = item->dot; i < item->rhs->num_symbols; ++i) {
    if (i > item->dot) {
      printf(" ");
    }

    pretty_print_symbol(grammar, &item->rhs->symbols[i]);
  }

  printf(", ");

  pretty_print_symbol(grammar, &item->lookahead);

  printf("]");
}

static void dump_item_set(Grammar* grammar, ItemSet* set) {
  printf("{\n");

  for (int i = 0; i < set->capacity; ++i) {
    if (bitset_query(set->bits, i)) {
      continue;
    }

    Item* item = &set->items[i];

    printf("  ");
    dump_item(grammar, item);

    printf(",\n");
  }

  printf("}\n\n");
}

static void nt_first(Grammar* grammar, uint64_t* visited, Vec(Symbol)* firsts, int nt) {
  if (bitset_query(visited, nt)) {
    return;
  }
  bitset_set(visited, nt);

  for (ProductionRHS* prod = grammar->first_prod[nt]; prod; prod = prod->next) {
    Symbol x = prod->symbols[0];

    if (x.kind == SYM_NON_TERMINAL) {
      nt_first(grammar, visited, firsts, x.as.non_terminal);
    }
    else {
      vec_put(*firsts, x);
    }
  }
}

static void first_terminal_of_remainder(Grammar* grammar, Vec(Symbol)* firsts, Item* item) {
  vec_clear(*firsts);

  int idx = item->dot + 1;

  if (idx >= item->rhs->num_symbols) {
    vec_put(*firsts, item->lookahead);
    return;
  }

  Symbol x = item->rhs->symbols[idx];
  if (x.kind != SYM_NON_TERMINAL) {
    vec_put(*firsts, x);
    return;
  }

  uint64_t visited[BITSET_WORDS(GRAMMAR_MAX_STRINGS)] = {0};
  nt_first(grammar, visited, firsts, x.as.non_terminal);
}

static Item new_item(int lhs, ProductionRHS* rhs, int dot, Symbol lookahead) {
  assert(dot <= rhs->num_symbols);
  return (Item) {
    .lhs = lhs,
    .rhs = rhs,
    .dot = dot,
    .lookahead = lookahead,
  };
}

static void populate_item_set_and_stack(ItemSet* set, ItemSet* set_dest, Vec(Item)* stack) {
  for (int i = 0; i < set->capacity; ++i)
  {
    if (bitset_query(set->bits, i)) {
      vec_put(*stack, set->items[i]);

      if (set_dest) {
        item_set_add(set_dest, &set->items[i]);
      }
    }
  }
}

static ItemSet* closure(Grammar* grammar, ItemSet* set) {
  ItemSet* result = calloc(1, sizeof(ItemSet));
  Vec(Item) stack = NULL;

  populate_item_set_and_stack(set, result, &stack);

  Vec(Symbol) firsts = NULL;

  while (vec_length(stack)) {
    Item item = vec_pop(stack);

    if (item.dot == item.rhs->num_symbols) {
      continue;
    }

    Symbol c = item.rhs->symbols[item.dot];

    if (c.kind != SYM_NON_TERMINAL) {
      continue;
    }

    ProductionRHS* head = grammar->first_prod[c.as.non_terminal];

    for (ProductionRHS* p = head; p; p = p->next) {
      first_terminal_of_remainder(grammar, &firsts, &item);

      for (int i = 0; i < vec_length(firsts); ++i) {
        Symbol b = firsts[i];
        Item ni = new_item(c.as.non_terminal, p, 0, b);
        
        if (!item_set_contains(result, &ni)) {
          item_set_add(result, &ni);
          vec_put(stack, ni);
        }
      } 
    }
  }

  vec_free(stack);
  vec_free(firsts);

  return result;
} 

static ItemSet* _goto(Grammar* grammar, ItemSet* s, Symbol x) {
  Vec(Item) stack = NULL;
  ItemSet* moved = calloc(1, sizeof(ItemSet));

  populate_item_set_and_stack(s, NULL, &stack);

  while (vec_length(stack)) {
    Item item = vec_pop(stack);

    if (item.dot == item.rhs->num_symbols) {
      continue;
    }

    Symbol next = item.rhs->symbols[item.dot];

    if (!symbol_equal(&next, &x)) {
      continue;
    }
    
    Item ni = new_item(item.lhs, item.rhs, item.dot + 1, item.lookahead);

    if (!item_set_contains(moved, &ni)) {
      item_set_add(moved, &ni);
      vec_put(stack, ni);
    }
  }

  vec_free(stack);

  ItemSet* result = closure(grammar, moved);
  free(moved);

  return result;
}
