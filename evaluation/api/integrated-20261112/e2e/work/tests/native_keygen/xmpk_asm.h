#ifndef XMPK_NATIVE_KEYGEN_EMULATOR_H
#define XMPK_NATIVE_KEYGEN_EMULATOR_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint64_t lo, hi;
} __attribute__((aligned(16))) xmpk_gf127_slot;
uint64_t xmpk_gf127_mul8(uint64_t, uint64_t);
int xmpk_gf127_extgemm(const uint8_t *, const uint8_t *, uint8_t *,
                     unsigned, unsigned, unsigned, unsigned, int);
int xmpk_gf127_extgemm_prepare_a(const uint8_t *, unsigned, unsigned, unsigned);
int xmpk_gf127_extgemm_run_prepared_a(const uint8_t *, uint8_t *, unsigned, int);
int xmpk_gf127_extgemm_run_prepared_a_packed(
    const xmpk_gf127_slot *, xmpk_gf127_slot *, unsigned);
int xmpk_gf127_extgemm_run_prepared_a_packed_rows(
    const xmpk_gf127_slot *, xmpk_gf127_slot *, unsigned, unsigned, unsigned);
void xmpk_gf127_extgemm_clear_prepared_a(void);
void xmpk_gf127_extgemm_scrub_workspace(void);
#endif
