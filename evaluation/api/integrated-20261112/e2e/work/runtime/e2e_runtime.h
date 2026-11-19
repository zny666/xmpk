#ifndef XMPK_E2E_RUNTIME_H
#define XMPK_E2E_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

void e2e_heap_reset(void);
void e2e_rng_reset(uint64_t seed);
int e2e_randombytes(unsigned char *out, unsigned long long length);

#endif
