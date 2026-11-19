#include "ref_api.h"
#include <api.h>
#include <utils_randombytes.h>

#if !defined(_OV_CLASSIC) || defined(_OV_PKC) || defined(_OV_PKC_SKC)
#error "UOV requires the full-key classic API"
#endif
#if defined(_BLAS_UINT64_) || defined(_BLAS_SSE_) || defined(_BLAS_AVX2_) || \
    defined(_BLAS_GFNI_) || defined(_BLAS_NEON_) || defined(_BLAS_M4F_) || \
    defined(_MUL_WITH_MULTAB_)
#error "UOV requires the unmodified src/ref backend, including its original SWAR"
#endif

_Static_assert(sizeof(size_t) == sizeof(unsigned long long),
               "This profile requires a 64-bit size_t/ULL ABI");

/* Only system_random.c receives -Drandombytes=ref_system_randombytes.
 * Preserve the upstream void, namespaced UOV declaration exactly. */
int ref_system_randombytes(unsigned char *out, unsigned long long length);
void randombytes(unsigned char *out, unsigned long long length) {
    (void)ref_system_randombytes(out, length);
}

size_t ref_public_bytes(void) { return CRYPTO_PUBLICKEYBYTES; }
size_t ref_secret_bytes(void) { return CRYPTO_SECRETKEYBYTES; }
size_t ref_signature_bytes(void) { return CRYPTO_BYTES; }
const char *ref_algorithm(void) { return "UOV"; }
const char *ref_parameter(void) { return PROFILE_PARAMETER; }
const char *ref_implementation(void) { return PROFILE_IMPLEMENTATION; }

int ref_keypair(uint8_t *pk, uint8_t *sk) {
    return crypto_sign_keypair(pk, sk);
}

int ref_sign(uint8_t *sm, size_t *length, const uint8_t *message,
             size_t length_in, const uint8_t *sk) {
    unsigned long long n = 0;
    int status = crypto_sign(sm, &n, message, (unsigned long long)length_in, sk);
    *length = (size_t)n;
    return status;
}

int ref_open(uint8_t *message, size_t *length, const uint8_t *sm,
             size_t length_in, const uint8_t *pk) {
    unsigned long long n = 0;
    int status = crypto_sign_open(message, &n, sm, (unsigned long long)length_in, pk);
    *length = (size_t)n;
    return status;
}
