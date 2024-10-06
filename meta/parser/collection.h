#pragma once 

#include "item.h"
#include "symbol_set.h"

#define COLLECTION_CAPACITY 1024

typedef struct Transition Transition;
struct Transition {
  Transition* next;
  Symbol x;
  struct ItemSet* set;
};

typedef struct {
  ItemSet* cc0;
  ItemSet* sets[COLLECTION_CAPACITY];
  Transition* transitions[COLLECTION_CAPACITY];
} Collection;

typedef struct {
  int capacity;
  int count;
  ItemSet** sets;
} SetList;

static void set_list_add(SetList* list, ItemSet* set) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    list->sets = realloc(list->sets, list->capacity * sizeof(*list->sets));
  }

  list->sets[list->count++] = set;
}

static ItemSet* set_list_pop(SetList* list) {
  assert(list->count);
  return list->sets[--list->count];
}

static int collection_find(Collection* cc, ItemSet* set) {
  int i = hash_item_set(set) % COLLECTION_CAPACITY;

  for (int j = 0; j < COLLECTION_CAPACITY; ++j) {
    if (!cc->sets[i]) {
      return i;
    }

    if (item_set_equal(cc->sets[i], set)) {
      return i;
    }

    i = (i + 1) % COLLECTION_CAPACITY;
  }

  fprintf(stderr, "Collection capacity reached");
  exit(1);
}

static void collection_add(Collection* cc, ItemSet* set) {
  int idx = collection_find(cc, set);

  if (cc->sets[idx] == NULL) {
    cc->sets[idx] = set;
  }
}

static int collection_contains(Collection* cc, ItemSet* set) {
  int idx = collection_find(cc, set);
  return cc->sets[idx] != NULL;
}

static int collection_index(Collection* cc, ItemSet* set) {
  int idx = collection_find(cc, set);
  assert(cc->sets[idx]);
  return idx;
}

static Collection* build_canonical_collection(Grammar* grammar) {
  int goal_id = to_id(grammar, "Goal");

  Item* root = calloc(1, sizeof(Item));
  root->lhs = goal_id,
  root->rhs = grammar->first_prod[goal_id],
  root->dot = 0,
  root->lookahead = (Symbol) {
    .kind = SYM_EOF
  };

  ItemSet* initial = calloc(1, sizeof(ItemSet));
  item_set_add(initial, root);

  ItemSet* cc0 = closure(grammar, initial);

  Collection* cc = calloc(1, sizeof(Collection));
  cc->cc0 = cc0;

  SetList stack = {0};

  collection_add(cc, cc0);
  set_list_add(&stack, cc0);

  SymbolSet* xs = calloc(1, sizeof(SymbolSet));

  while (stack.count) {
    ItemSet* cci = set_list_pop(&stack);

    symbol_set_clear(xs);

    for (int i = 0; i < cci->capacity; ++i) {
      if (!bitset_query(cci->bits, i)) { continue; }

      Item* item = &cci->items[i];

      if (item->dot == item->rhs->num_symbols) {
        continue;
      }

      Symbol x = item->rhs->symbols[item->dot];
      symbol_set_add(xs, x);
    }

    int table_idx = collection_index(cc, cci);

    for (int i = 0; i < SYMBOL_SET_CAPACITY; ++i) {
      if (!bitset_query(xs->bits, i)) { continue; }
      Symbol x = xs->symbols[i];
      
      ItemSet* temp = _goto(grammar, cci, x); 

      if (!collection_contains(cc, temp)) {
        set_list_add(&stack, temp);
        collection_add(cc, temp);
      }

      Transition* t = calloc(1, sizeof(Transition));

      t->x = x;
      t->set = temp;

      t->next = cc->transitions[table_idx];
      cc->transitions[table_idx] = t;
    }
  }

  free(xs);
  free(stack.sets);

  return cc;
}