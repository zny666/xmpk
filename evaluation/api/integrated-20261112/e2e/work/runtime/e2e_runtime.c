#include "e2e_runtime.h"

#include <stdint.h>
#include <string.h>

#define E2E_HEAP_BYTES (32u * 1024u * 1024u)

typedef struct {
  size_t size;
  size_t reserved;
} e2e_alloc_header;

static unsigned char e2e_heap[E2E_HEAP_BYTES]
    __attribute__((aligned(64)));
static size_t e2e_heap_used;
static uint64_t e2e_rng_state;

extern void abort(void);

static size_t align_up(size_t value, size_t alignment) {
  return (value + alignment - 1u) & ~(alignment - 1u);
}

void e2e_heap_reset(void) {
  e2e_heap_used = 0;
}

void *malloc(size_t size) {
  size_t start;
  e2e_alloc_header *header;

  if (size == 0)
    size = 1;
  start = align_up(e2e_heap_used, 16);
  if (size > E2E_HEAP_BYTES - start - sizeof(*header))
    return 0;
  header = (e2e_alloc_header *)(void *)(e2e_heap + start);
  header->size = size;
  header->reserved = 0;
  e2e_heap_used = start + sizeof(*header) + align_up(size, 16);
  return header + 1;
}

void free(void *ptr) {
  (void)ptr;
}

void *calloc(size_t count, size_t size) {
  void *ptr;

  if (count != 0 && size > SIZE_MAX / count)
    return 0;
  size *= count;
  ptr = malloc(size);
  if (ptr)
    memset(ptr, 0, size);
  return ptr;
}

void *realloc(void *ptr, size_t size) {
  void *next;
  e2e_alloc_header *header;
  size_t copy;

  if (!ptr)
    return malloc(size);
  if (size == 0)
    return 0;
  header = ((e2e_alloc_header *)ptr) - 1;
  next = malloc(size);
  if (!next)
    return 0;
  copy = header->size < size ? header->size : size;
  memcpy(next, ptr, copy);
  return next;
}

void *aligned_alloc(size_t alignment, size_t size) {
  uintptr_t raw;
  uintptr_t aligned;
  size_t extra;

  if (alignment < sizeof(void *) || (alignment & (alignment - 1u)) != 0)
    return 0;
  if (size > SIZE_MAX - alignment - sizeof(void *))
    return 0;
  extra = alignment + sizeof(void *);
  raw = (uintptr_t)malloc(size + extra);
  if (!raw)
    return 0;
  aligned = align_up(raw + sizeof(void *), alignment);
  ((void **)aligned)[-1] = (void *)raw;
  return (void *)aligned;
}

int posix_memalign(void **result, size_t alignment, size_t size) {
  void *ptr = aligned_alloc(alignment, size);
  if (!ptr)
    return 12;
  *result = ptr;
  return 0;
}

void e2e_rng_reset(uint64_t seed) {
  e2e_rng_state = seed ? seed : UINT64_C(0x9e3779b97f4a7c15);
}

static uint64_t e2e_rng_next(void) {
  uint64_t x = e2e_rng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  e2e_rng_state = x;
  return x;
}

int e2e_randombytes(unsigned char *out, unsigned long long length) {
  while (length != 0) {
    uint64_t word = e2e_rng_next();
    unsigned take = length < 8 ? (unsigned)length : 8;
    for (unsigned i = 0; i < take; ++i)
      out[i] = (unsigned char)(word >> (8 * i));
    out += take;
    length -= take;
  }
  return 0;
}

int ref_system_randombytes(unsigned char *out, unsigned long long length) {
  return e2e_randombytes(out, length);
}
