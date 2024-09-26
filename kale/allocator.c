#include <stdio.h>

#include "allocator.h"

#define SLI 4
static_assert((1<<SLI) < 32, "invalid value for SLI");

//#define DEBUG_PRINT_ALLOCATIONS

typedef struct Block Block;
struct Block {
  bool allocated; // used for coalescing
  uint64_t size; // size not including this header

  Block** this; // pointer to this block in the free list
  Block* next; // next block in free list

  Block* left; // used for coalescing
  Block* right;
};

typedef struct {
  Block* head[1 << SLI];
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

static int most_significant_bit(uint64_t value) {
  assert(value);
  return bitscan_backward(value);
}

static void mapping(uint64_t amount, int* f, int* s) {
  assert(amount);
  *f = most_significant_bit(amount);
  *s = (int)((amount ^ ((uint64_t)1 << *f)) >> (*f - SLI));
}

static uint64_t ones(int count) {
  return ((uint64_t)1 << count) - 1;
}

static uint64_t round_up(uint64_t amount, int num_bits) {
  uint64_t mask = ones(num_bits);
  return (amount + mask) & ~mask;
}

// Round up amount so that it has all bits below SLI cleared
static size_t align_amount(size_t amount) {
  amount = round_up(amount, SLI);
  int f = most_significant_bit(amount);
  int bottom_clear = f - SLI;
  return round_up(amount, bottom_clear);
}

// Scan through bitset for available
static int find_index(uint64_t state, int minimum) {
  uint64_t masked = state & ~ones(minimum);
  return masked == 0 ? -1 : bitscan_forward(masked);
}

static Table2* get_table2(Table1* t1, int f) {
  return t1->data + f;
}

// After removing block, update bitsets
static void handle_remove(Table1* t1, int f, int s) {
  Table2* t2 = get_table2(t1, f);
  
  if (!t2->head[s]) {
    t2->state &= ~(1 << s);
  }

  if (!t2->state) {
    t1->state &= ~(1 << f);
  }
}

// Remove a specific block from its free list
static void remove_block(Table1* t1, Block* block) {
  assert(block->this);
  assert(!block->allocated);

  Block* next = *block->this = block->next;

  if (next) {
    next->this = block->this;
  }

  int f, s;
  mapping(block->size, &f, &s);
  handle_remove(t1, f, s);

  block->this = NULL;
  block->next = NULL;
}

// Pop the first available block
static Block* pop_block(Table1* t1, int f, int s) {
  Block* block = get_table2(t1, f)->head[s];
  
  assert(block);
  remove_block(t1, block);

  return block;
}

// Put a block into the free list
static void insert_block(Table1* t1, int f, int s, Block* block) {
  t1->state |= (uint64_t)1 << f;

  Table2* t2 = get_table2(t1, f);
  t2->state |= (uint64_t)1 << s;

  Block* next = block->next = t2->head[s];

  if (next) {
    next->this = &block->next;
  }

  t2->head[s] = block;
  block->this = &t2->head[s];
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

static void initialize_block(Block* block, size_t size) {
  block->size = size;
  block->allocated = false;
  block->next = NULL;
  block->left = NULL;
  block->right = NULL;
}

// Push a new block into the arena
static Block* new_block(Arena* arena, size_t size) {
  Block* block = arena_push(arena, sizeof(Block) + size);
  initialize_block(block, size);
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

    size_t b2_size = (block->size - amount) - sizeof(Block); 
    initialize_block(b2, b2_size);

    Block* right = b2->right = block->right;

    if (right) {
      assert(right->left == block);
      right->left = b2;
    }

    block->right = b2;
    b2->left = block;

    int f, s;
    mapping(b2->size, &f, &s);
    insert_block(t1, f, s, b2);

    block->size = amount;

    #ifdef DEBUG_PRINT_ALLOCATIONS
    printf("  Split [%llu] -> [%llu], [%llu][%llu]\n", block->size + b2->size + sizeof(Block), block->size, sizeof(block), b2->size);
    #endif
  }

  return block;
}

// Given two adjacent blocks, coalesce them into one
static Block* coalesce_blocks(Block* first, Block* second) {
  assert(offset_pointer(first + 1, first->size) == second);

  first->size += second->size + sizeof(Block);

  Block* right = first->right = second->right;

  if (right) {
    assert(right->left == second);
    right->left = first;
  }

  #ifdef DEBUG_PRINT_ALLOCATIONS
  printf("  Coalesce [%llu], [%llu][%llu] -> [%llu]\n", first->size - second->size - sizeof(Block), sizeof(Block), second->size, first->size);
  #endif

  return first;
}

void* allocator_alloc(Allocator* a, uint64_t amount) {
  #ifdef DEBUG_PRINT_ALLOCATIONS
  printf("Allocate (%llu):\n", amount);
  #endif

  if (amount == 0) {
    return NULL;
  }

  amount = align_amount(amount);

  int f, s;
  mapping(amount, &f, &s);

  Block* block = find_block(&a->table, f, s);

  if (!block) {
    #ifdef DEBUG_PRINT_ALLOCATIONS
    printf("  New block\n");
    #endif
    block = new_block(a->arena, amount);
  }
  else {
    #ifdef DEBUG_PRINT_ALLOCATIONS
    printf("  Existing block [%llu]\n", block->size);
    #endif
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

  #ifdef DEBUG_PRINT_ALLOCATIONS
  printf("Free (%llu):\n", block->size);
  #endif

  if (block->right && !block->right->allocated) {
    Block* right = block->right;
    remove_block(&a->table, right);
    block = coalesce_blocks(block, right);
  }

  if (block->left && !block->left->allocated) {
    Block* left = block->left;
    remove_block(&a->table, left);
    block = coalesce_blocks(left, block);
  }

  int f, s;
  mapping(block->size, &f, &s);

  insert_block(&a->table, f, s, block);
}

bool allocator_tests(ScratchLibrary* scratch_lib) {
  Scratch scratch = scratch_get(scratch_lib, 0, NULL);

  #define ASSERT(cond, message) do { if (!(cond)) { printf("Failure %s(%d): %s\n", __FILE__, __LINE__, message); return false; } } while (false)

  ASSERT(round_up(0b1011, 2) == 0b1100, "round up bug");
  ASSERT(round_up(0b1111, 3) == 0b10000, "round up bug");
  ASSERT(round_up(0b111, 3) == 0b1000, "round up bug");

  ASSERT(most_significant_bit(0b10001) == 4, "most significant bit bug");
  ASSERT(most_significant_bit(0b10101) == 4, "most significant bit bug");
  ASSERT(most_significant_bit(0b10000) == 4, "most significant bit bug");

  ASSERT(most_significant_bit(0b1000) == 3, "most significant bit bug");
  ASSERT(most_significant_bit(0b10) == 1, "most significant bit bug");

  ASSERT(align_amount(0b1111) == 0b10000, "align amount bug");
  ASSERT(align_amount(0b001) == 0b10000, "align amount bug");
  ASSERT(align_amount(0b10000) == 0b10000, "align amount bug");
  ASSERT(align_amount(0b11000) == 0b100000, "align amount bug");
  ASSERT(align_amount(0b100010001) == 0b100100000, "align amount bug");
  ASSERT(align_amount(0b1000000) == 0b1000000, "align amount bug");
  ASSERT(align_amount(0b1000100) == 0b1010000, "align amount bug");
  ASSERT(align_amount(0b11111100000) == 0b100000000000, "align amount bug");
  ASSERT(align_amount(0b10111100000) == 0b11000000000, "align amount bug");
  ASSERT(align_amount(0b100001000000000000000000000000) == 0b100010000000000000000000000000, "align amount bug");
  ASSERT(align_amount(0b100000000000000000000000000001) == 0b100010000000000000000000000000, "align amount bug");
  ASSERT(align_amount(0b100010000000000000000000000000) == 0b100010000000000000000000000000, "align amount bug");
  ASSERT(align_amount(0b101000000000000000000000000000) == 0b101000000000000000000000000000, "align amount bug");

  int f, s;

  mapping(0b0011010, &f, &s);
  ASSERT(f == 4 && s == 0b1010, "mapping bug");

  mapping(0b1010010, &f, &s);
  ASSERT(f == 6 && s == 0b0100, "mapping bug");

  Allocator* a = new_allocator(scratch.arena); 

  uint64_t block_size = align_amount(10 * 1024 * 1024);
  allocator_free(a, allocator_alloc(a, block_size));

  int n = 1000;
  int** v = allocator_alloc(a, sizeof(int*) * n);

  for_range(int, i, n) {
    v[i] = allocator_alloc(a, sizeof(int));
    *v[i] = i;
  }

  for_range(int, i, n) {
    ASSERT(*v[i] == i, "memory corruped");

    if (i % 2 == 0) {
      allocator_free(a, v[i]);
    }
  }

  for_range(int, i, n) {
    if (i % 2 != 0) {
      allocator_free(a, v[i]);
    }
  }

  allocator_free(a, v);

  int x = most_significant_bit(a->table.state);
  ASSERT(a->table.state == (uint64_t)1 << x, "not fully coalesced");

  int y = most_significant_bit(a->table.data[x].state);
  ASSERT(a->table.data[x].state == (uint64_t)1 << y, "not fully coalesced");

  ASSERT(a->table.data[x].head[y]->size == block_size, "block doesn't coalesce properly");

  a = new_allocator(scratch.arena);

  n = 4;
  uint64_t size = (n-1) * sizeof(Block) + n * 32 + 1;

  allocator_free(a, allocator_alloc(a, size));

  void* prev = allocator_alloc(a, 32);

  for (int i = 1; i < n; ++i) {
    void* cur = allocator_alloc(a, 32);
    ASSERT(cur == offset_pointer(prev, sizeof(Block) + 32), "block split bug");
    prev = cur;
  }

  ASSERT(allocator_alloc(a, 32) != offset_pointer(prev, sizeof(Block) + 32), "block split even though too small");

  printf("All tests passed.\n");

  scratch_release(&scratch);

  return true;
}