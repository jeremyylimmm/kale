#include <stdio.h>

#include "frontend.h"

SemContext* sem_init(Arena* arena) {
  SemContext* ctx = arena_type(arena, SemContext);
  ctx->arena = arena;
  ctx->allocator = new_allocator(arena);
  return ctx;
}

void sem_dump(SemFile* file) {
  for_range(int, func_id, dynamic_array_length(file->funcs)) {
    SemFunc* func = &file->funcs[func_id];
    printf("fn @%d {\n", func_id);

    for_range (int, block_id, dynamic_array_length(func->blocks)) {
      SemBlock* block = &func->blocks[block_id];
      printf("!bb_%d:\n", block_id);

      for_range(int, inst_id, dynamic_array_length(block->insts)) {
        SemInst* inst = &block->insts[inst_id];

        printf("  ");

        if (inst->def) {
          printf("%%%d = ", inst->def);
        }

        printf("%s ", sem_op_str[inst->op]);

        for_range(int, i, inst->num_ins) {
          if (i > 0) {
            printf(", ");
          }

          printf("%%%d", inst->ins[i]);
        }

        printf("\n");
      }
    }

    printf("}\n\n");
  }
}