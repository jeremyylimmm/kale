#include "allocator.h"

#define SLI 4

typedef struct Block Block;
struct Block {
  bool allocated; // used for coalescing
  uint64_t size; // size not including this header

  Block** this; // pointer to this block in the free list
  Block* list_next; // next block in free list

  Block* adj_next; // used for coalescing
  Block* adj_prev;
};

typedef struct {
  Block* data[1 << SLI];
  uint64_t state;
} Table2;

typedef struct {
  Table2 data[64];
  uint64_t state;
} Table1;

struct Allocator {
  Table1 table;
  Arena* arena;
};

Allocator* new_allocator(Arena* arena) {
  Allocator* allocator = arena_type(arena, Allocator);
  allocator->arena = arena;

  return allocator;
}

static void mapping(uint64_t amount, int* f, int* s) {
  assert(amount);
  *f = lzcnt64(amount);
  *s = (int)((amount ^ ((uint64_t)1 << *f)) >> (*f - SLI));
}

static uint64_t ones(int count) {
  return (1 << count) - 1;
}

static uint64_t round_up(uint64_t amount, int num_bits) {
  uint64_t mask = ones(num_bits);
  return (amount + mask) & ~mask;
}

// Round up amount so that it has all bits below SLI cleared
static size_t align_amount(size_t amount) {
  amount = round_up(amount, SLI);
  int f = tzcnt64(amount);
  int bottom_clear = f - SLI;
  return round_up(amount, bottom_clear);
}

// Scan through bitset for available
static int find_index(uint64_t state, int minimum) {
  uint64_t masked = state & ~ones(minimum);
  return masked == 0 ? -1 : tzcnt64(masked);
}

static Table2* get_table2(Table1* t1, int f) {
  return t1->data + f;
}

// After removing block, update bitsets
static void update_state(Table1* t1, int f, int s) {
  Table2* t2 = get_table2(t1, f);
  
  if (!t2->data[s]) {
    t2->state &= ~(1 << s);
  }

  if (!t2->state) {
    t1->state &= ~(1 << f);
  }
}

// Pop the first available block
static Block* pop_block(Table1* t1, int f, int s) {
  Table2* t2 = get_table2(t1, f);

  Block* block = t2->data[s];

  t2->data[s] = block->list_next;
  if (t2->data[s]) {
    t2->data[s]->this = &t2->data[s];
  }

  block->this = NULL;
  block->list_next = NULL;

  update_state(t1, f, s);

  return block;
}

// Remove a specific block from its free list
static void remove_block(Table1* t1, Block* block) {
  assert(block->this);
  assert(!block->allocated);

  *block->this = block->list_next;

  if (block->list_next) {
    block->list_next->this = block->this;
  }

  int f, s;
  mapping(block->size, &f, &s);
  update_state(t1, f, s);

  block->this = NULL;
}

// Put a block into the free list
static void insert_block(Table1* t1, int f, int s, Block* block) {
  t1->state |= (uint64_t)1 << f;

  Table2* t2 = get_table2(t1, f);
  t2->state |= (uint64_t)1 << s;

  block->list_next = t2->data[s];
  if (block->list_next) {
    block->list_next->this = &block->list_next;
  }

  t2->data[s] = block;
  block->this = &t2->data[s];
}

// Given indices, find a block that is sufficient size
static Block* find_block(Table1* t1, int f, int s) {
  // Try find block with fli = f
  if (t1->state & ((uint64_t)1 << f)) {
    Table2* t2 = get_table2(t1, f);
    s = find_index(t2->state, s);

    if (s != -1) {
      return pop_block(t1, f, s);
    }
  }

  s = 0; // Round up so sli can be 0
  f += 1;

  f = find_index(t1->state, f);

  if (f == -1) {
    return NULL;
  }

  Table2* t2 = get_table2(t1, f);
  
  s = find_index(t2->state, s);
  assert(s != -1);

  return pop_block(t1, f, s);
}

// Push a new block into the arena
static Block* new_block(Arena* arena, size_t size) {
  Block* block = arena_push(arena, sizeof(Block) + size);
  block->size = size;
  block->allocated = false;
  block->list_next = NULL;
  block->adj_prev = NULL;
  block->adj_next = NULL;
  return block;
}

static uint64_t minimum_size() {
  return align_amount(1);
}

// If block is big enough to split, split.
static Block* attempt_split_block(Table1* t1, Block* block, uint64_t amount) {
  // Need enough space for amount + block header + block size
  if (block->size >= (amount + minimum_size() + sizeof(Block))) {
    Block* b2 = offset_pointer(block + 1, amount);

    b2->size = (block->size - amount) - sizeof(Block);
    b2->allocated = false;
    b2->list_next = NULL;

    b2->adj_next = block->adj_next;
    if (b2->adj_next) {
      assert(b2->adj_next == block);
      b2->adj_next->adj_prev = b2;
    }

    block->adj_next = b2;
    b2->adj_prev = block;

    int f, s;
    mapping(b2->size, &f, &s);
    insert_block(t1, f, s, b2);

    block->size = amount;
  }

  return block;
}

// Given two adjacent blocks, coalesce them into one
static Block* coalesce_blocks(Block* first, Block* second) {
  assert(offset_pointer(first + 1, first->size) == second);

  first->size += second->size + sizeof(Block);
  first->adj_next = second->adj_next;

  if (first->adj_next) {
    assert(first->adj_next->adj_prev == second);
    first->adj_next->adj_prev = first;
  }

  return first;
}

void* allocator_alloc(Allocator* a, uint64_t amount) {
  if (amount == 0) {
    return NULL;
  }

  amount = align_amount(amount);

  int f, s;
  mapping(amount, &f, &s);

  Block* block = find_block(&a->table, f, s);

  if (!block) {
    block = new_block(a->arena, amount);
  }

  block = attempt_split_block(&a->table, block, amount);

  block->allocated = true;
  return block + 1;
}

void allocator_free(Allocator* a, void* pointer) {
  if (!pointer) {
    return;
  }

  Block* block = (Block*)pointer - 1;
  assert(block->allocated);
  block->allocated = false;

  if (block->adj_next && !block->adj_next->allocated) {
    Block* b2 = block->adj_next;
    remove_block(&a->table, b2);
    block = coalesce_blocks(block, b2);
  }

  if (block->adj_prev && !block->adj_prev->allocated) {
    Block* b2 = block->adj_prev;
    remove_block(&a->table, b2);
    block = coalesce_blocks(b2, block);
  }

  int f, s;
  mapping(block->size, &f, &s);

  insert_block(&a->table, f, s, block);
}