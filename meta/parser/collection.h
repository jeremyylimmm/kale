#pragma once 

#include "item.h"
#include "symbol.h"
#include "vec.h"

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

static int collection_table_index(Collection* cc, ItemSet* set) {
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

  Vec(ItemSet*) stack = NULL;

  collection_add(cc, cc0);
  vec_put(stack, cc0);

  SymbolSet* xs = calloc(1, sizeof(SymbolSet));

  while (vec_length(stack)) {
    ItemSet* cci = vec_pop(stack);

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

    int table_idx = collection_table_index(cc, cci);

    for (int i = 0; i < SYMBOL_SET_CAPACITY; ++i) {
      if (!bitset_query(xs->bits, i)) { continue; }
      Symbol x = xs->symbols[i];
      
      ItemSet* temp = _goto(grammar, cci, x); 

      if (!collection_contains(cc, temp)) {
        vec_put(stack, temp);
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
  vec_free(stack);

  return cc;
}