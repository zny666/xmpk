/* Local functional emulator, not a timing model. Real cycles come from RTL. */
#include "xmpk_asm.h"
#include "keygen_trial.h"
#include "qrop.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t left[128 * 128 * 3];
static unsigned prepared_rows, inner_dimension;

uint64_t xmpk_gf127_mul8(uint64_t a, uint64_t b)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; i++)
        result |= (uint64_t)((((a >> (8*i)) & 255) *
                              ((b >> (8*i)) & 255)) % 127) << (8*i);
    return result;
}

int xmpk_gf127_extgemm_prepare_a(
    const uint8_t *a, unsigned m, unsigned k, unsigned degree)
{
    assert(degree == 3 && m <= 128 && k <= 128 && m && k);
    memcpy(left, a, m*k*3);
    prepared_rows = m;
    inner_dimension = k;
    return 1;
}

int xmpk_gf127_extgemm_run_prepared_a_packed_rows(
    const xmpk_gf127_slot *b, xmpk_gf127_slot *c,
    unsigned offset, unsigned rows, unsigned n)
{
    assert(prepared_rows && rows && offset+rows <= prepared_rows && n && n<=128);
    for (unsigned r = 0; r < rows; r++)
        for (unsigned col = 0; col < n; col++) {
            uint32_t acc[5] = {0};
            for (unsigned k = 0; k < inner_dimension; k++)
                for (unsigned i = 0; i < 3; i++)
                    for (unsigned j = 0; j < 3; j++)
                        acc[i+j] += left[((r+offset)*inner_dimension+k)*3+i] *
                            ((b[k*n+col].lo >> (8*j)) & 255);
            for (int t = 4; t >= 3; t--) {
                acc[t-3] += QRUOV_fc0 * acc[t];
                acc[t-3+QRUOV_fe] += QRUOV_fc * acc[t];
            }
            c[r*n+col].lo = 0;
            c[r*n+col].hi = 0;
            for (unsigned i = 0; i < 3; i++)
                c[r*n+col].lo |= (uint64_t)(acc[i] % 127) << (8*i);
        }
    return 1;
}

int xmpk_gf127_extgemm_run_prepared_a_packed(
    const xmpk_gf127_slot *b, xmpk_gf127_slot *c, unsigned n)
{
    return xmpk_gf127_extgemm_run_prepared_a_packed_rows(
        b, c, 0, prepared_rows, n);
}

int xmpk_gf127_extgemm_run_prepared_a(
    const uint8_t *b, uint8_t *c, unsigned n, int transpose)
{
    xmpk_gf127_slot *slots = calloc(inner_dimension*n, sizeof(*slots));
    xmpk_gf127_slot *result = calloc(prepared_rows*n, sizeof(*result));
    assert(slots && result);
    for (unsigned r = 0; r < inner_dimension; r++)
        for (unsigned col = 0; col < n; col++) {
            size_t source = transpose ? col*inner_dimension+r : r*n+col;
            for (unsigned i = 0; i < 3; i++)
                slots[r*n+col].lo |= (uint64_t)b[source*3+i] << (8*i);
        }
    xmpk_gf127_extgemm_run_prepared_a_packed(slots, result, n);
    for (unsigned r = 0; r < prepared_rows*n; r++)
        for (unsigned i = 0; i < 3; i++)
            c[r*3+i] = (uint8_t)(result[r].lo >> (8*i));
    free(slots);
    free(result);
    return 1;
}

int xmpk_gf127_extgemm(
    const uint8_t *a, const uint8_t *b, uint8_t *c,
    unsigned m, unsigned k, unsigned n, unsigned degree, int transpose)
{
    /* One-shot GEMM must not replace the explicitly prepared A cache. */
    uint8_t saved_left[sizeof(left)];
    memcpy(saved_left, left, sizeof(left));
    unsigned saved_rows = prepared_rows, saved_inner = inner_dimension;
    xmpk_gf127_extgemm_prepare_a(a, m, k, degree);
    int result = xmpk_gf127_extgemm_run_prepared_a(b, c, n, transpose);
    memcpy(left, saved_left, sizeof(left));
    prepared_rows = saved_rows;
    inner_dimension = saved_inner;
    return result;
}

void xmpk_gf127_extgemm_clear_prepared_a(void)
{
    prepared_rows = inner_dimension = 0;
}

void xmpk_gf127_extgemm_scrub_workspace(void)
{
    memset(left, 0, sizeof(left));
    xmpk_gf127_extgemm_clear_prepared_a();
}

int main(int argc, char **argv)
{
    const unsigned masks[] = {1,3,5,7,9,11,13,15};
    const unsigned counts[] = {1,2,6,8};
    uint8_t reference[BITS2BYTE(P3_CHUNK_COEFF_LEN*7)];
    uint8_t actual[sizeof(reference)];
    keygen_trial_metrics metrics;
    unsigned cases = 0;
    for (unsigned seed = 0; seed < 3; seed++) {
        SEED_PK pk;
        SEED_SK sk;
        for (unsigned i = 0; i < sizeof(pk); i++) pk[i] = 17*i+3+seed*43;
        for (unsigned i = 0; i < sizeof(sk); i++) sk[i] = 29*i+5+seed*67;
        for (unsigned c = 0; c < sizeof(counts)/sizeof(counts[0]); c++) {
            unsigned count = counts[c];
            unsigned base = count == 6 ? QRUOV_m-6 : seed;
            size_t bytes = BITS2BYTE(count*P3_EQ_COEFF_LEN*7);
            assert(qruov_keygen_reference_trial(
                reference, pk, sk, base, count, &metrics));
            if (argc == 2 && seed == 0 && count == 2) {
                FILE *golden = fopen(argv[1], "wb");
                assert(golden);
                assert(fwrite(reference, 1, bytes, golden) == bytes);
                assert(fclose(golden) == 0);
            }
            for (unsigned f = 0; f < sizeof(masks)/sizeof(masks[0]); f++) {
                memset(actual, 0xa5, sizeof(actual));
                assert(qruov_keygen_candidate_trial(
                    actual, pk, sk, base, count, masks[f], &metrics));
                if (memcmp(reference, actual, bytes)) {
                    fprintf(stderr, "Mismatch seed=%u count=%u base=%u flags=%u\n",
                            seed, count, base, masks[f]);
                    return 1;
                }
                for (size_t i = bytes; i < sizeof(actual); i++)
                    assert(actual[i] == 0xa5);
                cases++;
            }
        }
    }
    puts("NATIVE_FUNCTIONAL_ONLY: 96 serialized comparisons PASS");
    assert(cases == 96);
    return 0;
}
