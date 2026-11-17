#include "xmpk_asm.h"
extern void __attribute__((noreturn)) tohost_exit(unsigned long code);
static uint64_t a[1] __attribute__((aligned(8))) = {1};
static uint64_t b[1] __attribute__((aligned(8))) = {0x123456789abcdef0ULL};
static uint64_t c[1] __attribute__((aligned(8)));
int main(void) {
  uint64_t result = xmpk_gf16_mul(b[0], 0x1111111111111111ULL, 0);
  if (result != b[0]) tohost_exit(0x3fffffff);
  xmpk_qgemm_setup((uint64_t)a, (uint64_t)b, (uint64_t)c);
  if (xmpk_qgemm_run(1 | (1 << 8) | (1 << 16), 0, 0) != 16 ||
      c[0] != b[0]) tohost_exit(0x3fffffff);
  tohost_exit(0);
}
