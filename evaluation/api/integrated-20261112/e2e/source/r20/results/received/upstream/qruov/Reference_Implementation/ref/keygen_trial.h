#ifndef QRUOV_KEYGEN_TRIAL_H
#define QRUOV_KEYGEN_TRIAL_H

#include "qruov.h"

enum {
    KEYGEN_DIRECT = 1,
    KEYGEN_BATCH = 2,
    KEYGEN_TRIANGLE = 4,
    KEYGEN_FUSED = 8
};

typedef struct {
    uint64_t setup, generate, products, transpose, quadratic;
    uint64_t finalize, serialize, cleanup, total, instructions;
} keygen_trial_metrics;

static inline uint64_t keygen_trial_cycle(void)
{
#if defined(KEYGEN_NATIVE_CHECK)
    return 0; /* Functional checks never produce performance measurements. */
#else
    uint64_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
    return value;
#endif
}

static inline uint64_t keygen_trial_instret(void)
{
#if defined(KEYGEN_NATIVE_CHECK)
    return 0;
#else
    uint64_t value;
    __asm__ volatile("rdinstret %0" : "=r"(value) :: "memory");
    return value;
#endif
}

/* Serializes one chunk exactly as production KeyGen does. Test-only APIs. */
int qruov_keygen_reference_trial(
    uint8_t *serialized, const SEED_PK seed_pk, const SEED_SK seed_sk,
    unsigned eq_base, unsigned eq_count, keygen_trial_metrics *metrics);
int qruov_keygen_candidate_trial(
    uint8_t *serialized, const SEED_PK seed_pk, const SEED_SK seed_sk,
    unsigned eq_base, unsigned eq_count, unsigned flags,
    keygen_trial_metrics *metrics);

#endif
