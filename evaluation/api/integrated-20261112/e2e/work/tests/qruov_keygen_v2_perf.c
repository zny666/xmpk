#include "keygen_trial.h"
#include "qrop.h"
#include <stdint.h>

#ifndef TRIAL_FLAGS
#define TRIAL_FLAGS 0
#endif
#ifndef TRIAL_EQ_COUNT
#define TRIAL_EQ_COUNT 2
#endif
#ifndef TRIAL_EQ_BASE
#define TRIAL_EQ_BASE 0
#endif

extern void printstr(const char *);
extern void printhex(uint64_t);
extern void __attribute__((noreturn)) tohost_exit(uintptr_t);

#define OUTPUT_BYTES BITS2BYTE(TRIAL_EQ_COUNT * P3_EQ_COEFF_LEN * QRUOV_q_LOG)
static uint8_t serialized[OUTPUT_BYTES];
static char output_hex[2 * OUTPUT_BYTES + 2];

static void metric(const char *name, uint64_t value)
{
    printstr(name);
    printstr("=0x");
    printhex(value);
    printstr("\n");
}

int main(void)
{
    SEED_PK seed_pk;
    SEED_SK seed_sk;
    keygen_trial_metrics metrics;
    for (unsigned i = 0; i < sizeof(seed_pk); i++)
        seed_pk[i] = (uint8_t)(17u * i + 3u);
    for (unsigned i = 0; i < sizeof(seed_sk); i++)
        seed_sk[i] = (uint8_t)(29u * i + 5u);
    /* Sentinels exercise initialization of shared bytes and trailing bits. */
    for (unsigned i = 0; i < sizeof(serialized); i++)
        serialized[i] = 0xa5;
    if (!qruov_keygen_candidate_trial(
            serialized, seed_pk, seed_sk, TRIAL_EQ_BASE,
            TRIAL_EQ_COUNT, TRIAL_FLAGS, &metrics))
        tohost_exit(21);
    printstr("KEYGEN_V2_BEGIN\n");
    metric("flags", TRIAL_FLAGS);
    metric("eq_base", TRIAL_EQ_BASE);
    metric("eq_count", TRIAL_EQ_COUNT);
    metric("setup", metrics.setup);
    metric("generate", metrics.generate);
    metric("products", metrics.products);
    metric("transpose", metrics.transpose);
    metric("quadratic", metrics.quadratic);
    metric("finalize", metrics.finalize);
    metric("serialize", metrics.serialize);
    metric("cleanup", metrics.cleanup);
    metric("total", metrics.total);
    metric("instructions", metrics.instructions);
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(serialized); i++) {
        output_hex[2 * i] = digits[serialized[i] >> 4];
        output_hex[2 * i + 1] = digits[serialized[i] & 15];
    }
    output_hex[2 * OUTPUT_BYTES] = '\n';
    output_hex[2 * OUTPUT_BYTES + 1] = 0;
    printstr("SERIALIZED_HEX=");
    printstr(output_hex);
    printstr("KEYGEN_V2_SUCCESS\n");
    tohost_exit(0);
}
