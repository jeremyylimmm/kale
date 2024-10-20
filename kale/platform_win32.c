#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>

#include <stdio.h>

#include "base.h"
#include "allocator.h"

#define ARENA_CAPACITY ((size_t)5 * 1024 * 1024 * 1024)

struct Arena {
  void* base;

  void* next_page;
  size_t page_size;

  size_t used;
  size_t capacity;
};

struct ScratchLibrary {
  Arena* arenas[2];
};

Arena* new_arena() {
  Arena* arena = LocalAlloc(LMEM_ZEROINIT, sizeof(Arena));
  
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);

  arena->page_size = system_info.dwPageSize;
  size_t page_count = (ARENA_CAPACITY + arena->page_size - 1) / arena->page_size;
  size_t reserve_size = page_count * arena->page_size;

  arena->next_page = arena->base = VirtualAlloc(NULL, reserve_size, MEM_RESERVE, PAGE_NOACCESS);

  if (!arena->base) {
    fprintf(stderr, "Failed to reserve arena memory.\n");
    ExitProcess(1);
  }

  return arena;
}

void free_arena(Arena* arena) {
  VirtualFree(arena->base, 0, MEM_RELEASE);
  LocalFree(arena);
}

static size_t aligned_offset(size_t used) {
  return (used + 7) & ~7;
}

void* arena_push(Arena* arena, size_t amount) {
  if (amount == 0) {
    return NULL;
  }

  size_t offset = aligned_offset(arena->used); 

  while (arena->capacity < (offset + amount)) {
    void* result = VirtualAlloc(arena->next_page, arena->page_size, MEM_COMMIT, PAGE_READWRITE);

    if (!result) {
      fprintf(stderr, "Failed to commit page for arena.\n");
      ExitProcess(1);
    }

    arena->next_page = offset_pointer(arena->next_page, arena->page_size);
    arena->capacity += arena->page_size;
  }

  arena->used = offset + amount;

  return offset_pointer(arena->base, offset);
}

void* arena_push_zeroed(Arena* arena, size_t amount) {
  void* pointer = arena_push(arena, amount);
  memset(pointer, 0, amount);
  return pointer;
}

ScratchLibrary* new_scratch_library() {
  ScratchLibrary* lib = LocalAlloc(LMEM_ZEROINIT, sizeof(ScratchLibrary));

  for_range(int, i, LENGTH(lib->arenas)) {
    lib->arenas[i] = new_arena();
  }

  return lib;
}

void free_scratch_library(ScratchLibrary* lib) {
  for_range(int, i, LENGTH(lib->arenas)) {
    free_arena(lib->arenas[i]);
  }

  LocalFree(lib);
}

typedef struct {
  size_t used;
} ScratchImpl;

Scratch scratch_get(ScratchLibrary* lib, int num_conflicts, Arena** conflicts) {
  for_range(int, i, LENGTH(lib->arenas)) {
    Arena* arena = lib->arenas[i];

    bool can_use = true;

    for_range(int, j, num_conflicts) {
      Arena* conflict = conflicts[j];

      if (arena == conflict) {
        can_use = false;
        break;
      }
    }

    if (can_use) {
      size_t used = arena->used;

      ScratchImpl* impl = arena_type(arena, ScratchImpl);
      impl->used = used;

      return (Scratch) {
        .arena = arena,
        .allocator = new_allocator(arena),
        .impl = impl
      };
    }
  }

  assert(false);
  return (Scratch) {0};
}

void scratch_release(Scratch* scratch) {
  Arena* arena = scratch->arena;

  size_t start = ((ScratchImpl*)scratch->impl)->used;
  size_t end = scratch->arena->used;

  assert(end >= start);

  arena->used = start;
  (void)end;

  #if _DEBUG
  memset(offset_pointer(arena->base, start), 0, end-start);
  memset(scratch, 0, sizeof(*scratch));
  #endif
}

int bitscan_forward(uint64_t number) {
  DWORD index;
  if(BitScanForward64(&index, number)) {
    return index;
  }
  else {
    return 64;
  }
}

int bitscan_backward(uint64_t number) {
  DWORD index;
  if(BitScanReverse64(&index, number)) {
    return index;
  }
  else {
    return 64;
  }
}