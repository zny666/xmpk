#define XMPK_E2E_ASM_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef struct { uint64_t lo, hi; } __attribute__((aligned(16))) xmpk_gf127_slot;
static uint8_t mul_bits(uint8_t a, uint8_t b, unsigned bits) {
  uint8_t r = 0;
  unsigned mask = bits == 4 ? 15 : 255, poly = bits == 4 ? 3 : 27;
  for (unsigned i = 0; i < bits; i++) {
    r ^= (uint8_t)(-(b & 1u) & a);
    unsigned hi = a >> (bits - 1);
    a = ((a << 1) & mask) ^ (poly & -hi);
    b >>= 1;
  }
  return r;
}
static const uint64_t *emu_a, *emu_b;
static uint64_t *emu_c;
static void xmpk_qgemm_setup(const uint64_t *a, const uint64_t *b, uint64_t *c) {
  emu_a = a; emu_b = b; emu_c = c;
}
static uint64_t xmpk_qgemm_run(unsigned m, unsigned k, unsigned nw, unsigned mode) {
  unsigned bits = mode ? 8 : 4, lanes = 64 / bits, mask = (1u << bits) - 1;
  for (unsigned r = 0; r < m; r++)
    for (unsigned w = 0; w < nw; w++) {
      uint64_t z = 0;
      for (unsigned lane = 0; lane < lanes; lane++) {
        uint8_t v = 0;
        for (unsigned i = 0; i < k; i++)
          v ^= mul_bits((emu_a[r * ((k + lanes - 1) / lanes) + i / lanes] >>
                         (bits * (i % lanes))) & mask,
                        (emu_b[i * nw + w] >> (bits * lane)) & mask, bits);
        z |= (uint64_t)v << (bits * lane);
      }
      emu_c[r * nw + w] = z;
    }
  return (uint64_t)m * k * nw * lanes;
}
static void xmpk_linxfm_setup(const uint64_t *a, const uint64_t *b, uint64_t *c) {
  xmpk_qgemm_setup(a, b, c);
}
static uint64_t xmpk_linxfm_run(unsigned d, unsigned n) {
  for (unsigned c = 0; c < n; c++) {
    uint64_t z = 0;
    for (unsigned r = 0; r < d; r++) {
      uint8_t v = 0;
      for (unsigned i = 0; i < d; i++)
        v ^= mul_bits((emu_a[r] >> (4 * i)) & 15,
                      (emu_b[c] >> (4 * i)) & 15, 4);
      z |= (uint64_t)v << (4 * r);
    }
    emu_c[c] = z;
  }
  return d * n;
}
static uint64_t xmpk_gf256_mul8(uint64_t a, uint64_t b) {
  uint64_t z = 0;
  for (unsigned i = 0; i < 8; i++)
    z |= (uint64_t)mul_bits(a >> (8 * i), b >> (8 * i), 8) << (8 * i);
  return z;
}
/* Unused runtime entry points must never execute in these experiments. */
static void xmpk_solveq_setup(const uint64_t *a, uint64_t *b) { abort(); }
static uint64_t xmpk_solveq_run(unsigned n) { abort(); }
static void xmpk_extgemm_setup(const void *a, const void *b, void *c) { abort(); }
static uint64_t xmpk_extgemm_run(unsigned m,unsigned k,unsigned n,unsigned d) { abort(); }
int xmpk_gf256_matvec_batch(const uint8_t *, size_t, const uint8_t *, uint8_t *,
                          size_t, unsigned, unsigned, unsigned);
int xmpk_gf127_extgemm_run_prepared_a_packed_rows(
    const xmpk_gf127_slot *, xmpk_gf127_slot *, unsigned, unsigned, unsigned);
void xmpk_gf127_extgemm_clear_prepared_a(void);
