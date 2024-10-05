#include "frontend.h"
#include "dynamic_array.h"

#include "../generated/lr_tables.h"

typedef enum {
  STACK_ITEM_STATE,
  STACK_ITEM_TOKEN,
  STACK_ITEM_NON_TERMINAL
} StackItemKind;

typedef enum {
  CHILD_TOKEN,
  CHILD_NODE
} ChildKind;

typedef struct Node Node;

typedef struct {
  ChildKind kind;
  union {
    Token token;
    Node* node;
  } as;
} Child;

struct Node {
  NonTerminal kind;
  int num_children;
  Child* children;
};

typedef struct {
  StackItemKind kind;
  union {
    State state;
    Token token;
    struct {
      NonTerminal which;
      Node* node;
    } nt;
  } as;
} StackItem;

AST* parse(Arena* arena, TokenizedBuffer tokens) {
  Scratch scratch = global_scratch(1, &arena);
  Allocator* scratch_allocator = new_allocator(scratch.arena);

  int cur_token = 0;

  #define PEEK() (assert(cur_token < tokens.length), tokens.tokens[cur_token])
  #define LEX() (assert(cur_token < tokens.length), tokens.tokens[cur_token++])

  #define TOP_STATE() (assert(dynamic_array_length(stack) && stack[dynamic_array_length(stack)-1].kind == STACK_ITEM_STATE), stack[dynamic_array_length(stack)-1].as.state)
        
  DynamicArray(StackItem) stack = new_dynamic_array(scratch_allocator);

  StackItem first = {
    .kind = STACK_ITEM_STATE,
    .as.state = initial_state
  };

  dynamic_array_put(stack, first);

  bool done = false;

  while (!done) {
    State _state = TOP_STATE();
    Action action = action_table[_state][PEEK().kind];

    switch (action.kind) {
      default:
        assert(false && "parsing error!");
        break;
      case ACTION_REDUCE: {
        int num_children = action.as.reduce.count;
        Child* children = arena_array(arena, Child, num_children);

        for (int i = 0; i < num_children; ++i) {
          StackItem x0 = dynamic_array_pop(stack);
          StackItem x1 = dynamic_array_pop(stack);

          assert(x0.kind == STACK_ITEM_STATE);

          switch (x1.kind) {
            default:
              assert(false);
              break;
            case STACK_ITEM_NON_TERMINAL:
              children[i] = (Child) {
                .kind = CHILD_NODE,
                .as.node = x1.as.nt.node
              };
              break;
            case STACK_ITEM_TOKEN:
              children[i] = (Child) {
                .kind = CHILD_TOKEN,
                .as.token = x1.as.token
              };
              break;
          }
        }

        Node* node = arena_type(arena, Node);
        node->num_children = num_children;
        node->children = children;
        node->kind = action.as.reduce.nt;

        State prev_state = TOP_STATE(); 
        State new_state = goto_table[prev_state][node->kind];

        StackItem nt_item = {
          .kind = STACK_ITEM_NON_TERMINAL,
          .as.nt.which = node->kind,
          .as.nt.node = node
        };

        StackItem new_state_item = {
          .kind = STACK_ITEM_STATE,
          .as.state = new_state
        };

        dynamic_array_put(stack, nt_item);
        dynamic_array_put(stack, new_state_item);
      } break;

      case ACTION_SHIFT: {
        StackItem tok_item = {
          .kind = STACK_ITEM_TOKEN,
          .as.token = LEX()
        };

        StackItem state_item = {
          .kind = STACK_ITEM_STATE,
          .as.state = action.as.shift.state
        };

        dynamic_array_put(stack, tok_item);
        dynamic_array_put(stack, state_item);
      } break;

      case ACTION_ACCEPT:
        done = true;  
        break;
    }
  }

  scratch_release(&scratch);

  return NULL;
}