#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stdarg.h>

#include "grammar.h"
#include "common.h"

#define MAX_ITEMS_PER_SET 1024
#define MAX_SYMBOLS_PER_FIRST 128
#define COLLECTION_CAPACITY 1024
#define SYMBOL_SET_CAPACITY 1024

typedef struct Item Item;
struct Item {
  int lhs;
  ProductionRHS* rhs;
  int dot;
  Symbol lookahead;
};

typedef struct Transition Transition;
typedef struct ItemSet ItemSet;

struct Transition {
  Transition* next;
  Symbol x;
  struct ItemSet* set;
};

struct ItemSet {
  Item* items[MAX_ITEMS_PER_SET];
  int count;
  Transition* transitions;
};

typedef struct {
  int capacity;
  int count;
  Item** items;
} ItemList;

typedef struct {
  ItemSet* sets[COLLECTION_CAPACITY];
  int states[COLLECTION_CAPACITY];
} Collection;

typedef struct {
  uint64_t bits[(SYMBOL_SET_CAPACITY + 63)/64];
  Symbol symbols[SYMBOL_SET_CAPACITY];
  int column[SYMBOL_SET_CAPACITY];
} SymbolSet;

typedef struct {
  int capacity;
  int count;
  ItemSet** sets;
} SetList;

typedef enum {
  ACT_ACCEPT,
  ACT_REDUCE,
  ACT_SHIFT
} ActionKind;

typedef struct {
  ActionKind kind;
  union {
    struct { int state; } shift;
    struct { int count; int nt; } reduce;
  } as;
} Action;

typedef struct GotoEntry GotoEntry;
typedef struct ActionEntry ActionEntry;

struct GotoEntry {
  GotoEntry* next;

  int state;
  int nt;

  int dest;
};

struct ActionEntry {
  ActionEntry* next;

  int state;
  Symbol sym;

  Action action;
};

typedef struct {
  uint64_t rows;
  uint64_t columns;
  uint64_t words[1];
} BitMatrix;

static BitMatrix* new_bitmatrix(int rows, int columns) {
  uint64_t n = rows * columns;
  uint64_t num_words = (n+63)/64;
  size_t alloc_size = offsetof(BitMatrix, words) + num_words * sizeof(uint64_t);

  BitMatrix* bm = calloc(1, alloc_size);
  bm->rows = rows;
  bm->columns = columns;

  return bm;
}

static uint64_t _bitmatrix_idx(BitMatrix* bm, uint64_t row, uint64_t column) {
  assert(row < bm->rows);
  assert(column < bm->columns);
  return row * bm->columns + column;
}

static int bitmatrix_query(BitMatrix* bm, uint64_t row, uint64_t column) {
  uint64_t index = _bitmatrix_idx(bm, row, column);
  return (bm->words[index/64] >> (index%64)) & 1;
}

static void bitmatrix_set(BitMatrix* bm, uint64_t row, uint64_t column) {
  uint64_t index = _bitmatrix_idx(bm, row, column);
  bm->words[index/64] |= (uint64_t)1 << (index%64);
}

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

static void print_terminal_as_token_kind(FILE* file, Symbol sym) {
  switch (sym.kind) {
    default:
      assert(0);
      break;
    case SYM_CHAR:
      fprintf(file, "'%c'", sym.as.chr);
      break;
    case SYM_EOF:
      fprintf(file, "TOKEN_EOF");
      break;
  }
}

static void print_action_entry(FILE* file, ActionEntry* e, char* member, char* fmt, ...) {
  fprintf(file, "  [STATE_%d][", e->state);
  print_terminal_as_token_kind(file, e->sym);
  fprintf(file, "].%s = ", member);

  va_list ap;
  va_start(ap, fmt);

  vfprintf(file, fmt, ap);

  va_end(ap);

  fprintf(file, ",\n");
}

int main() {
  FILE* file;
  if (fopen_s(&file, "meta/grammar.txt", "r")) {
    fprintf(stderr, "Failed to load grammar\n");
    return 1;
  }

  fseek(file, 0, SEEK_END);
  size_t file_len = ftell(file);
  rewind(file);

  char* grammar_def = malloc((file_len + 1) * sizeof(char));
  size_t grammar_def_len = fread(grammar_def, 1, file_len, file);
  grammar_def[grammar_def_len] = '\0';

  fclose(file);

  Grammar* grammar = parse_grammar(grammar_def);

  SymbolSet* terminals = calloc(1, sizeof(SymbolSet));

  for (int nt = 0; nt < GRAMMAR_MAX_STRINGS; ++nt) {
    if (!grammar->first_prod[nt]) { continue; }
    for (ProductionRHS* prod = grammar->first_prod[nt]; prod; prod = prod->next) {
      for (int i = 0; i < prod->num_symbols; ++i) {
        Symbol sym = prod->symbols[i];
        if (sym.kind != SYM_NON_TERMINAL) {
          symbol_set_add(terminals, sym);
        }
      }
    }
  }

  symbol_set_add(terminals, (Symbol){.kind = SYM_EOF});

  int goal_id = to_id(grammar, "Goal");

  Item root = (Item) {
    .lhs = goal_id,
    .rhs = grammar->first_prod[goal_id],
    .dot = 0,
    .lookahead = (Symbol) {
      .kind = SYM_EOF
    },
  };

  ItemSet* initial = calloc(1, sizeof(ItemSet));
  item_set_add(initial, &root);

  ItemSet* cc0 = closure(grammar, initial);

  Collection* cc = calloc(1, sizeof(Collection));
  SetList stack = {0};

  collection_add(cc, cc0);
  set_list_add(&stack, cc0);

  SymbolSet* xs = calloc(1, sizeof(SymbolSet));

  while (stack.count) {
    ItemSet* cci = set_list_pop(&stack);

    symbol_set_clear(xs);

    for (int i = 0; i < MAX_ITEMS_PER_SET; ++i) {
      Item* item = cci->items[i];
      if (!item) { continue; }

      if (item->dot == item->rhs->num_symbols) {
        continue;
      }

      Symbol x = item->rhs->symbols[item->dot];
      symbol_set_add(xs, x);
    }

    for (int i = 0; i < SYMBOL_SET_CAPACITY; ++i) {
      if (!symbol_set_check(xs, i)) { continue; }
      Symbol x = xs->symbols[i];
      
      ItemSet* temp = _goto(grammar, cci, x); 

      if (!collection_contains(cc, temp)) {
        set_list_add(&stack, temp);
        collection_add(cc, temp);
      }

      Transition* t = calloc(1, sizeof(Transition));

      t->x = x;
      t->set = temp;

      t->next = cci->transitions;
      cci->transitions = t;
    }
  }

  // Write the parser

  int num_states = 0;

  for (int i = 0; i < COLLECTION_CAPACITY; ++i) {
    if (!cc->sets[i]) { continue; }
    cc->states[i] = num_states++;
  }

  int num_terminals = 0;

  for (int i = 0; i < SYMBOL_SET_CAPACITY; ++i) {
    if (!symbol_set_check(terminals, i)) {
      continue;
    }

    terminals->column[i] = num_terminals++;
  }

  int num_non_terminals = 0;
  int non_terminals[GRAMMAR_MAX_STRINGS];
  int non_terminal_column[GRAMMAR_MAX_STRINGS];

  for (int nt = 0; nt < GRAMMAR_MAX_STRINGS; ++nt) {
    if (!grammar->first_prod[nt]) { continue; }
    int idx = num_non_terminals++;
    non_terminal_column[nt] = idx;
    non_terminals[idx] = nt;
  }

  BitMatrix* goto_table = new_bitmatrix(num_states, num_non_terminals);
  BitMatrix* action_table = new_bitmatrix(num_states, num_terminals);

  GotoEntry* gotos = NULL;
  ActionEntry* actions = NULL;

  #define CHECK_GOTO(state, nt)\
    do {\
      int col = non_terminal_column[nt]; \
      \
      if (bitmatrix_query(goto_table, state, col)) { \
        fprintf(stderr, "Ambiguous grammar.\n"); \
        return 1; \
      } \
      \
      bitmatrix_set(goto_table, state, col); \
    } while (0)

  #define CHECK_ACTION(state, term)\
    do {\
      int col = terminals->column[symbol_set_get_index(terminals, term)]; \
      \
      if (bitmatrix_query(action_table, state, col)) { \
        fprintf(stderr, "Ambiguous grammar.\n"); \
        return 1; \
      } \
      \
      bitmatrix_set(action_table, state, col); \
    } while (0)

  for (int set_index = 0; set_index < COLLECTION_CAPACITY; ++set_index) {
    ItemSet* cci = cc->sets[set_index];
    if (!cci) { continue; }

    int i = cc->states[set_index];

    for (Transition* t = cci->transitions; t; t = t->next) {
      int j = cc->states[collection_index(cc, t->set)];

      if (t->x.kind == SYM_NON_TERMINAL) {
        CHECK_GOTO(i, t->x.as.non_terminal);

        GotoEntry* entry = calloc(1, sizeof(GotoEntry));
        entry->state = i;
        entry->nt = t->x.as.non_terminal;
        entry->dest = j;

        entry->next = gotos;
        gotos = entry;
      }
      else {
        CHECK_ACTION(i, t->x);

        ActionEntry* entry = calloc(1, sizeof(ActionEntry));
        entry->state = i;
        entry->sym = t->x;
        entry->action = (Action){
          .kind = ACT_SHIFT,
          .as.shift.state = j
        };

        entry->next = actions;
        actions = entry;
      }
    }

    for (int item_idx = 0; item_idx < MAX_ITEMS_PER_SET; ++item_idx) {
      Item* item = cci->items[item_idx];
      if (!item || item->dot < item->rhs->num_symbols) { continue; }

      Item end_item = root;
      end_item.dot += 1;

      if (item_equal(item, &end_item)) {
        Symbol eof = {.kind=SYM_EOF};

        CHECK_ACTION(i, eof);

        ActionEntry* entry = calloc(1, sizeof(ActionEntry));
        entry->state = i;
        entry->sym = eof;
        entry->action = (Action){ .kind = ACT_ACCEPT, };

        entry->next = actions;
        actions = entry;
      }
      else {
        Symbol a = item->lookahead;

        CHECK_ACTION(i, a);

        ActionEntry* entry = calloc(1, sizeof(ActionEntry));
        entry->state = i;
        entry->sym = a;
        entry->action = (Action){
          .kind = ACT_REDUCE,
          .as.reduce.count = item->rhs->num_symbols,
          .as.reduce.nt = item->lhs
        };

        entry->next = actions;
        actions = entry;
      }
    }
  }

  if (fopen_s(&file, "generated\\parser.c", "w")) {
    fprintf(stderr, "Failed to write parser.c\n");
    return 1;
  }

  fprintf(file, "#include \"..\\frontend\\frontend.h\"\n\n");

  fprintf(file, "typedef enum {\n");

  for (int i = 0; i < num_states; ++i) {
    fprintf(file, "  STATE_%d,\n", i);
  }

  fprintf(file, "  NUM_STATES\n");
  fprintf(file, "} State;\n\n");

  fprintf(file, "typedef enum {\n");

  for (int i = 0; i < num_non_terminals; ++i) {
    int nt = non_terminals[i];
    fprintf(file, "  NON_TERMINAL_%s,\n", grammar->strings[nt]);
  }

  fprintf(file, "  NUM_NON_TERMINALS\n");
  fprintf(file, "} NonTerminal;\n\n");

  char* action_defs = (
    "typedef enum {\n"
    "  ACTION_ACCEPT,\n"
    "  ACTION_SHIFT,\n"
    "  ACTION_REDUCE,\n"
    "} ActionKind;\n"
    "\n"
    "typedef struct {\n"
    "  ActionKind kind;\n"
    "  union {\n"
    "    struct { State state; } shift;\n"
    "    struct { int count; NonTerminal nt; } reduce;\n"
    "  } as;\n"
    "} Action;\n"
    "\n"
  );

  fprintf(file, action_defs);

  fprintf(file, "static Action action_table[NUM_STATES][NUM_TOKEN_KINDS] = {\n");

  for (ActionEntry* e = actions; e; e = e->next) {
    switch (e->action.kind) {
      default:
        assert(0);
        break;
      case ACT_ACCEPT:
        print_action_entry(file, e, "kind", "ACTION_ACCEPT");
        break;
      case ACT_SHIFT:
        print_action_entry(file, e, "kind", "ACTION_SHIFT");
        print_action_entry(file, e, "as.shift.state", "STATE_%d", e->action.as.shift.state);
        break;
      case ACT_REDUCE:
        print_action_entry(file, e, "kind", "ACTION_REDUCE");
        print_action_entry(file, e, "as.reduce.count", "%d", e->action.as.reduce.count);
        print_action_entry(file, e, "as.reduce.nt", "NON_TERMINAL_%s", grammar->strings[e->action.as.reduce.nt]);
        break;
    }
  }

  fprintf(file, "};\n\n");

  fprintf(file, "static int goto_table[NUM_STATES][NUM_NON_TERMINALS] = {\n");

  for (GotoEntry* e = gotos; e; e = e->next) {
    fprintf(file, "  [STATE_%d][NON_TERMINAL_%s] = STATE_%d,\n", e->state, grammar->strings[e->nt], e->dest);
  }

  fprintf(file, "};\n\n");

  fclose(file);

  printf("Done.\n");

  return 0;
}