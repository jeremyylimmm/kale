#pragma once

#include <stdlib.h>
#include <stdint.h>

typedef struct {
  uint64_t rows;
  uint64_t columns;
  uint64_t words[1];
} BitMatrix;

typedef struct {
  uint64_t count;
  uint64_t words[1];
} Bitset;

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

static Bitset* new_bitset(uint64_t count) {
  uint64_t num_words = (count + 63)/64;
  size_t alloc_size = offsetof(Bitset, words) + num_words * sizeof(uint64_t);

  Bitset* bs = calloc(1, alloc_size);
  bs->count = count;

  return bs;
}

static int bitset_query(Bitset* bs, uint64_t index) {
  assert(index < bs->count);
  return (bs->words[index/64] >> (index%64)) & 1;
}

static void bitset_set(Bitset* bs, uint64_t index) {
  assert(index < bs->count);
  bs->words[index/64] |= (uint64_t)1 << (index%64);
}