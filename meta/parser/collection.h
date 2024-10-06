#pragma once 

#include "item.h"
#include "symbol.h"
#include "vec.h"

typedef struct Transition Transition;
struct Transition {
  Transition* next;
  Symbol x;
  ItemSet set;
};

typedef struct {
  ItemSet cc0;

  int capacity;
  int count;

  ItemSet* sets;
  Transition** transitions;
  uint64_t* bits;
} Collection;

static int collection_find(Collection* cc, ItemSet set) {
  int i = hash_item_set(set) % cc->capacity;

  for (int j = 0; j < cc->capacity; ++j) {
    if (!bitset_query(cc->bits, i)) {
      return i;
    }

    if (item_set_equal(&cc->sets[i], &set)) {
      return i;
    }

    i = (i + 1) % cc->capacity;
  }

  fprintf(stderr, "Collection capacity reached");
  exit(1);
}

static void _collection_add(Collection* cc, ItemSet set, Transition* transition) {
  int idx = collection_find(cc, set);

  if (!bitset_query(cc->bits, idx)) {
    cc->sets[idx] = set;
    cc->transitions[idx] = transition;
    bitset_set(cc->bits, idx);
    cc->count++;
  }
}

static void collection_add(Collection* cc, ItemSet set) {
  if (!cc->capacity || (float)cc->count >= (float)cc->capacity * 0.5f) {
    int new_capacity = cc->capacity ? cc->capacity * 2 : 8;

    Collection temp = {
      .cc0 = cc->cc0,
      .capacity = new_capacity,
      .sets = malloc(new_capacity * sizeof(cc->sets[0])),
      .transitions = malloc(new_capacity * sizeof(Transition*)),
      .bits = calloc(BITSET_WORDS(new_capacity), sizeof(uint64_t))
    };

    for (int i = 0; i < cc->capacity; ++i) {
      if (bitset_query(cc->bits, i)) {
        _collection_add(&temp, cc->sets[i], cc->transitions[i]);
      }
    }

    free(cc->bits);
    free(cc->sets);
    free(cc->transitions);

    *cc = temp;
  }

  _collection_add(cc, set, NULL);
}

static int collection_contains(Collection* cc, ItemSet set) {
  if (cc->capacity == 0) {
    return 0;
  }

  int idx = collection_find(cc, set);
  return bitset_query(cc->bits, idx);
}

static int collection_table_index(Collection* cc, ItemSet set) {
  int idx = collection_find(cc, set);
  assert(bitset_query(cc->bits, idx));
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

  ItemSet initial = {0};
  item_set_add(&initial, root);

  ItemSet cc0 = closure(grammar, initial);

  Collection* cc = calloc(1, sizeof(Collection));
  cc->cc0 = cc0;

  Vec(ItemSet) stack = NULL;

  collection_add(cc, cc0);
  vec_put(stack, cc0);

  SymbolSet* xs = calloc(1, sizeof(SymbolSet));

  while (vec_length(stack)) {
    ItemSet cci = vec_pop(stack);

    symbol_set_clear(xs);

    for (int i = 0; i < cci.capacity; ++i) {
      if (!bitset_query(cci.bits, i)) { continue; }

      Item* item = &cci.items[i];

      if (item->dot == item->rhs->num_symbols) {
        continue;
      }

      Symbol x = item->rhs->symbols[item->dot];
      symbol_set_add(xs, x);
    }

    for (int i = 0; i < SYMBOL_SET_CAPACITY; ++i) {
      if (!bitset_query(xs->bits, i)) { continue; }
      Symbol x = xs->symbols[i];
      
      ItemSet temp = _goto(grammar, cci, x); 

      if (!collection_contains(cc, temp)) {
        vec_put(stack, temp);
        collection_add(cc, temp);
      }

      Transition* t = calloc(1, sizeof(Transition));

      t->x = x;
      t->set = temp;

      int table_idx = collection_table_index(cc, cci);

      t->next = cc->transitions[table_idx];
      cc->transitions[table_idx] = t;
    }
  }

  free(xs);
  vec_free(stack);

  return cc;
}