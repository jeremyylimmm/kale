#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stdarg.h>

#include "../common.h"

#include "grammar.h"
#include "collection.h"
#include "bitmatrix.h"

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
  fprintf(file, "  [%d][", e->state);
  print_terminal_as_token_kind(file, e->sym);
  fprintf(file, "].%s = ", member);

  va_list ap;
  va_start(ap, fmt);

  vfprintf(file, fmt, ap);

  va_end(ap);

  fprintf(file, ",\n");
}

static void get_terminals(Grammar* grammar, SymbolSet* set, int* ids, int* out_count) {
  memset(set, 0, sizeof(*set));

  for (int nt = 0; nt < GRAMMAR_MAX_STRINGS; ++nt) {
    if (!grammar->first_prod[nt]) { continue; }

    for (ProductionRHS* prod = grammar->first_prod[nt]; prod; prod = prod->next)
    {
      for (int i = 0; i < prod->num_symbols; ++i)
      {
        Symbol sym = prod->symbols[i];
        if (sym.kind == SYM_NON_TERMINAL) {
          continue;
        } 

        symbol_set_add(set, sym);
      }
    }
  }

  symbol_set_add(set, (Symbol){.kind = SYM_EOF});

  int count = 0;

  for (int i = 0; i < SYMBOL_SET_CAPACITY; ++i) {
    if (bitset_query(set->bits, i)) {
      ids[i] = count++;
    }
  }

  *out_count = count;
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

  SymbolSet terminals;
  int terminal_ids[SYMBOL_SET_CAPACITY];
  int num_terminals;
  get_terminals(grammar, &terminals, terminal_ids, &num_terminals);

  Collection* cc = build_canonical_collection(grammar);

  // Write the parser

  int num_states = 1;
  int states[COLLECTION_CAPACITY];

  for (int i = 0; i < COLLECTION_CAPACITY; ++i) {
    if (!cc->sets[i]) { continue; }
    states[i] = num_states++;
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
      int col = terminal_ids[symbol_set_get_index(&terminals, term)]; \
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

    int i = states[set_index];

    for (Transition* t = cc->transitions[set_index]; t; t = t->next) {
      int j = states[collection_index(cc, t->set)];

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

    int goal_id = to_id(grammar, "Goal");

    for (int item_idx = 0; item_idx < cci->capacity; ++item_idx) {
      if (!bitset_query(cci->bits, item_idx)) { continue; }

      Item* item = &cci->items[item_idx];
      if (item->dot < item->rhs->num_symbols) { continue; }

      Item end_item = {
        .lhs = goal_id,
        .rhs = grammar->first_prod[goal_id],
        .dot = 1,
        .lookahead = (Symbol){.kind=SYM_EOF}
      };

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

  if (fopen_s(&file, "generated\\lr_tables.h", "w")) {
    fprintf(stderr, "Failed to write lr tables\n");
    return 1;
  }

  fprintf(file, "#pragma once\n\n");
  fprintf(file, "#include \"..\\frontend\\frontend.h\"\n\n");

  fprintf(file, "typedef int State;\n\n");
  fprintf(file, "#define NUM_STATES %d\n\n", num_states);

  fprintf(file, "typedef enum {\n");

  for (int i = 0; i < num_non_terminals; ++i) {
    int nt = non_terminals[i];
    fprintf(file, "  NON_TERMINAL_%s%s,\n", grammar->strings[nt], i == 0 ? " = 1" : "");
  }

  fprintf(file, "  NUM_NON_TERMINALS\n");
  fprintf(file, "} NonTerminal;\n\n");

  fprintf(file, "static char* non_terminal_name[NUM_NON_TERMINALS] = {\n");

  for (int i = 0; i < num_non_terminals; ++i) {
    int nt = non_terminals[i];
    char* name = grammar->strings[nt]; 
    fprintf(file, "  [NON_TERMINAL_%s] = \"%s\",\n", name, name);
  }

  fprintf(file, "};\n\n");

  char* action_defs = (
    "typedef enum {\n"
    "  ACTION_ACCEPT = 1,\n"
    "  ACTION_SHIFT,\n"
    "  ACTION_REDUCE,\n"
    "} ActionKind;\n"
    "\n"
    "typedef struct {\n"
    "  ActionKind kind;\n"
    "  int state_or_count;\n"
    "  NonTerminal nt;\n"
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
        print_action_entry(file, e, "state_or_count", "%d", e->action.as.shift.state);
        break;
      case ACT_REDUCE:
        print_action_entry(file, e, "kind", "ACTION_REDUCE");
        print_action_entry(file, e, "state_or_count", "%d", e->action.as.reduce.count);
        print_action_entry(file, e, "nt", "NON_TERMINAL_%s", grammar->strings[e->action.as.reduce.nt]);
        break;
    }
  }

  fprintf(file, "};\n\n");

  fprintf(file, "static State goto_table[NUM_STATES][NUM_NON_TERMINALS] = {\n");

  for (GotoEntry* e = gotos; e; e = e->next) {
    fprintf(file, "  [%d][NON_TERMINAL_%s] = %d,\n", e->state, grammar->strings[e->nt], e->dest);
  }

  fprintf(file, "};\n\n");

  fprintf(file, "static State initial_state = %d;\n\n", states[collection_index(cc, cc->cc0)]);

  fclose(file);

  printf("Done.\n");

  return 0;
}