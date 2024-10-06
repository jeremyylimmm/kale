#pragma once

#include <stdio.h>

#include "grammar.h"

#define MAX_ITEMS_PER_SET 1024
#define MAX_SYMBOLS_PER_FIRST 128

typedef struct {
  int lhs;
  ProductionRHS* rhs;
  int dot;
  Symbol lookahead;
} Item;

typedef struct Transition Transition;
typedef struct ItemSet ItemSet;

struct ItemSet {
  Item* items[MAX_ITEMS_PER_SET];
  int count;
  Transition* transitions;
};

struct Transition {
  Transition* next;
  Symbol x;
  struct ItemSet* set;
};

typedef struct {
  int capacity;
  int count;
  Item** items;
} ItemList;

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
  int i = item_hash(item) % MAX_ITEMS_PER_SET;

  for (int j = 0; j < MAX_ITEMS_PER_SET; ++j) {
    if (!set->items[i]) {
      return i;
    }

    if (item_equal(set->items[i], item)) {
      return i;
    }

    i = (i + 1) % MAX_ITEMS_PER_SET;
  }

  fprintf(stderr, "Maximum number of LR items exceeded");
  exit(1);
}

static int item_set_contains(ItemSet* set, Item* item) {
  int idx = item_set_find(set, item);
  return set->items[idx] != NULL;
}

static void item_set_add(ItemSet* set, Item* item) {
  int idx = item_set_find(set, item);
  if (!set->items[idx]) {
    set->count++;
    set->items[idx] = item;
  }
}

static void item_list_add(ItemList* list, Item* item) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    list->items = realloc(list->items, list->capacity * sizeof(*list->items));
  }

  list->items[list->count++] = item;
}

static Item* item_list_pop(ItemList* list) {
  assert(list->count);
  return list->items[--list->count];
}

static int cmp_u64(const void* a, const void* b) {
  return (int)(*(uint64_t*)a > *(uint64_t*)b);
}

static uint64_t hash_item_set(ItemSet* set) {
  int num_hashes = 0;
  uint64_t hashes[MAX_ITEMS_PER_SET] = {0};

  for (int i = 0; i < MAX_ITEMS_PER_SET; ++i) {
    if (!set->items[i]) {
      continue;
    }

    hashes[num_hashes++] = item_hash(set->items[i]);
  }

  qsort(hashes, num_hashes, sizeof(hashes[0]), cmp_u64);

  return fnv1a_hash(hashes, num_hashes * sizeof(hashes[0]));
}

static int item_set_equal(ItemSet* a, ItemSet* b) {
  if (a->count != b->count) {
    return 0;
  }

  for (int i = 0; i < MAX_ITEMS_PER_SET; ++i) {
    if (!a->items[i]) {
      continue;
    }

    if (!item_set_contains(b, a->items[i])) {
      return 0;
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

  for (int i = 0; i < MAX_ITEMS_PER_SET; ++i) {
    Item* item = set->items[i];
    if (!item) {
      continue;
    }

    printf("  ");
    dump_item(grammar, item);

    printf(",\n");
  }

  printf("}\n\n");
}

static void nt_first(Grammar* grammar, uint64_t* visited, Symbol* first, int* num_first, int nt) {
  if ((visited[nt/64] >> (nt % 64)) & 1) {
    return;
  }
  visited[nt/64] |= (uint64_t)1 << (nt % 64);

  for (ProductionRHS* prod = grammar->first_prod[nt]; prod; prod = prod->next) {
    Symbol x = prod->symbols[0];

    if (x.kind == SYM_NON_TERMINAL) {
      nt_first(grammar, visited, first, num_first, x.as.non_terminal);
    }
    else {
      first[(*num_first)++] = x;
    }
  }
}

static void first_terminal_of_remainder(Grammar* grammar, Symbol* first, int* num_first, Item* item) {
  *num_first = 0;

  int idx = item->dot + 1;

  if (idx >= item->rhs->num_symbols) {
    first[(*num_first)++] = item->lookahead;
    return;
  }

  Symbol x = item->rhs->symbols[idx];
  if (x.kind != SYM_NON_TERMINAL) {
    first[(*num_first)++] = x;
    return;
  }

  uint64_t visited[(GRAMMAR_MAX_STRINGS + 63) / 64] = {0};
  nt_first(grammar, visited, first, num_first, x.as.non_terminal);
}

static Item* new_item(int lhs, ProductionRHS* rhs, int dot, Symbol lookahead) {
  Item* item = calloc(1, sizeof(Item));
  item->lhs = lhs;
  item->rhs = rhs;
  item->dot = dot;
  item->lookahead = lookahead;
  return item;
}

static void populate_item_set_and_stack(ItemSet* set, ItemSet* set_dest, ItemList* stack) {
  for (int i = 0; i < MAX_ITEMS_PER_SET; ++i) {
    if (set->items[i]) {
      item_list_add(stack, set->items[i]);
      if (set_dest) {
        item_set_add(set_dest, set->items[i]);
      }
    }
  }
}

static ItemSet* closure(Grammar* grammar, ItemSet* set) {
  ItemSet* result = calloc(1, sizeof(ItemSet));
  ItemList stack = {0};

  populate_item_set_and_stack(set, result, &stack);

  int num_first;
  Symbol first[MAX_SYMBOLS_PER_FIRST];

  while (stack.count) {
    Item* item = item_list_pop(&stack);

    if (item->dot == item->rhs->num_symbols) {
      continue;
    }

    Symbol c = item->rhs->symbols[item->dot];

    if (c.kind != SYM_NON_TERMINAL) {
      continue;
    }

    ProductionRHS* head = grammar->first_prod[c.as.non_terminal];

    for (ProductionRHS* p = head; p; p = p->next) {
      first_terminal_of_remainder(grammar, first, &num_first, item);

      for (int i = 0; i < num_first; ++i) {
        Symbol b = first[i];
        Item* ni = new_item(c.as.non_terminal, p, 0, b);
        
        if (!item_set_contains(result, ni)) {
          item_set_add(result, ni);
          item_list_add(&stack, ni);
        }
      } 
    }
  }

  free(stack.items);

  return result;
} 

static ItemSet* _goto(Grammar* grammar, ItemSet* s, Symbol x) {
  ItemList stack = {0};
  ItemSet* moved = calloc(1, sizeof(ItemSet));

  populate_item_set_and_stack(s, NULL, &stack);

  while (stack.count) {
    Item* item = item_list_pop(&stack);

    if (item->dot == item->rhs->num_symbols) {
      continue;
    }

    Symbol next = item->rhs->symbols[item->dot];

    if (!symbol_equal(&next, &x)) {
      continue;
    }
    
    Item* ni = new_item(item->lhs, item->rhs, item->dot + 1, item->lookahead);

    if (!item_set_contains(moved, ni)) {
      item_set_add(moved, ni);
      item_list_add(&stack, ni);
    }
  }

  free(stack.items);

  ItemSet* result = closure(grammar, moved);
  free(moved);

  return result;
}
