#include <stdio.h>

#include "frontend.h"

SemContext* sem_init(Arena* arena) {
  SemContext* ctx = arena_type(arena, SemContext);
  ctx->arena = arena;
  ctx->allocator = new_allocator(arena);
  return ctx;
}

void sem_dump(SemFile* file) {
  for_range(int, func_id, file->num_funcs) {
    SemFunc* func = &file->funcs[func_id];
    printf("fn @%s() {\n", func->name.str);

    for_range (int, block_id, dynamic_array_length(func->blocks)) {
      SemBlock* block = &func->blocks[block_id];
      printf("!bb_%d:\n", block_id);

      for_list(SemInst, inst, block->start) {
        printf("  ");

        if (inst->def) {
          printf("%%%d = ", inst->def);
        }

        printf("%s ", sem_op_str[inst->op]);

        for_range(int, i, inst->num_ins) {
          if (i > 0) {
            printf(", ");
          }

          printf("%%%d", inst->ins[i]->def);
        }

        switch (inst->op) {
          case SEM_OP_INT_CONST:
            printf("%llu", (uint64_t)inst->data);
            break;

          case SEM_OP_BRANCH: {
            int* targs = (int*)inst->data;
            printf(" [bb_%d:bb_%d]", targs[0], targs[1]);
          } break;

          case SEM_OP_GOTO: {
            int* targs = (int*)inst->data;
            printf("bb_%d", targs[0]);
          } break;
        }

        printf("\n");
      }
    }

    printf("}\n\n");
  }
}

typedef struct {
  int count;
  int blocks[2];
} Successors;

static Successors get_successors(SemFunc* func, int block) {
  Successors result = {0};

  SemBlock* b = &func->blocks[block];

  if (b->end) {
    switch (b->end->op) {
      default:
        assert(false);
        break;

      case SEM_OP_RETURN:
        break;

      case SEM_OP_GOTO: {
        int* loc = b->end->data;
        result.blocks[result.count++] = *loc;
      } break;

      case SEM_OP_BRANCH: {
        int* locs = b->end->data;
        result.blocks[result.count++] = locs[0];
        result.blocks[result.count++] = locs[1];
      } break;
    }
  }

  return result;
}

uint64_t* sem_reachable(Arena* arena, SemFunc* func) {
  Scratch scratch = global_scratch(1, &arena);

  DynamicArray(int) stack = new_dynamic_array(scratch.allocator);
  dynamic_array_put(stack, 0);

  uint64_t* reachable = arena_array(arena, uint64_t, bitset_num_u64(dynamic_array_length(func->blocks)));

  while (dynamic_array_length(stack)) {
    int b = dynamic_array_pop(stack);

    if (bitset_query(reachable, b)) {
      continue;
    }

    bitset_set(reachable, b);

    Successors successors = get_successors(func, b);

    for_range(int, i, successors.count) {
      dynamic_array_put(stack, successors.blocks[i]);
    }
  }

  scratch_release(&scratch);
  return reachable;
}

static bool is_user_code(SemInst* inst) {
  switch (inst->op) {
    default:
      return true;
    case SEM_OP_GOTO:
      return false;
  }
}

static SemInst* contains_user_code(SemBlock* block) {
  for_list(SemInst, inst, block->start) {
    if (is_user_code(inst)) {
      return inst;
    }
  }

  return NULL;
}

static bool analyze_func(SemContext* context, SourceContents source, SemFunc* func) {
  Scratch scratch = global_scratch(1, &context->arena);
  bool ret_val = true;

  uint64_t* reachable = sem_reachable(scratch.arena, func);

  for_range(int, b, dynamic_array_length(func->blocks)) {
    if (bitset_query(reachable, b)) {
      continue;
    }

    SemInst* user_code = contains_user_code(&func->blocks[b]);

    if (user_code) {
      error_at_token(source, user_code->token, "this code is unreachable");
      ret_val = false;
    }
  }

  scratch_release(&scratch);
  return ret_val;
}

bool sem_analyze(SemContext* context, SourceContents source, SemFile* file) {
  bool result = true;

  for_range(int, i, file->num_funcs) {
    result &= analyze_func(context, source, &file->funcs[i]);
  }

  return result;
}