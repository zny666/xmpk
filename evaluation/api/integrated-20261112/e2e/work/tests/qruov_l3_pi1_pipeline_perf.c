#include "fql.h"
#include "prg.h"
#include "qrop.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define REPEATS 4

static uint8_t vector[QRUOV_V * QRUOV_L] __attribute__((aligned(4096)));
static uint8_t old_matrix[QRUOV_V * QRUOV_V * QRUOV_L]
    __attribute__((aligned(4096)));
static qruov_l3_striped_vxv striped_matrix __attribute__((aligned(4096)));
static uint8_t old_outputs[REPEATS][QRUOV_V * QRUOV_L]
    __attribute__((aligned(4096)));
static uint8_t striped_outputs[REPEATS][QRUOV_V * QRUOV_L]
    __attribute__((aligned(4096)));

static inline uint64_t read_cycle(void)
{
    uint64_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
    return value;
}

static void run_old(uint8_t outputs[REPEATS][QRUOV_V * QRUOV_L],
                    const SEED_PK seed_pk)
{
    prg pk_prg;
    prg_init(&pk_prg, seed_pk);
    for (int eq = 0; eq < REPEATS; eq++) {
        prepare_pi1_from_prg(old_matrix, &pk_prg, (uint16_t)eq);
        fql_mul_VxV_V(outputs[eq], old_matrix, vector);
    }
    prg_free(&pk_prg);
}

static void run_striped(uint8_t outputs[REPEATS][QRUOV_V * QRUOV_L],
                        const SEED_PK seed_pk)
{
    qruov_l3_striped_v packed_vector;
    prg pk_prg;
    fql_pack_V_l3(packed_vector, vector);
    prg_init(&pk_prg, seed_pk);
    for (int eq = 0; eq < REPEATS; eq++) {
        prepare_pi1_striped_from_prg(striped_matrix, &pk_prg, (uint16_t)eq);
        fql_mul_striped_VxV_V_l3(
            outputs[eq], striped_matrix, packed_vector);
    }
    prg_free(&pk_prg);
}

int main(void)
{
    SEED_PK seed_pk;
    for (unsigned i = 0; i < sizeof(seed_pk); i++) {
        seed_pk[i] = (uint8_t)(17u * i + 3u);
    }
    for (unsigned i = 0; i < sizeof(vector); i++) {
        vector[i] = (uint8_t)((29u * i + 5u) % QRUOV_q);
    }

    run_old(old_outputs, seed_pk);
    run_striped(striped_outputs, seed_pk);
    if (memcmp(old_outputs, striped_outputs, sizeof(old_outputs)) != 0) {
        tohost_exit(0x3fffffff);
    }

    uint64_t start = read_cycle();
#if PERF_MODE == 1
    run_striped(striped_outputs, seed_pk);
#else
    run_old(old_outputs, seed_pk);
#endif
    uint64_t cycles = read_cycle() - start;
    tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
}
