#include "ref_api.h"
#include "api.h"
#include <limits.h>
#include <stdlib.h>

#ifndef REF_PARAMETER
#error "REF_PARAMETER must identify the selected QRUOV parameter"
#endif
#ifndef REF_IMPLEMENTATION
#error "REF_IMPLEMENTATION must identify the reference implementation"
#endif
#if PRG_IS_AES != 1 || QRUOV_q != 127
#error "R20 QRUOV requires q127 and AESPRG"
#endif
#if QRUOV_L != 3 && QRUOV_L != 10
#error "R20 QRUOV requires L3 or L10"
#endif
#ifdef PRIM_AES_BACKEND_X86AESNI
#error "The original reference OpenSSL primitive path is required"
#endif
#if defined(__AVX__) || defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__riscv_vector)
#error "Vector targets are not permitted in this reference profile"
#endif

/* rng.h declares void randombytes, while the shared production RNG returns int.
 * Only upstream sign.c is compiled with randombytes=qruov_ref_randombytes.
 * Keep both declarations type-correct; never continue after an entropy failure.
 */
extern int randombytes(unsigned char *output, unsigned long long length);
void qruov_ref_randombytes(unsigned char *output, unsigned long long length);
void qruov_ref_randombytes(unsigned char *output, unsigned long long length)
{
    if (randombytes(output, length) != 0)
        abort();
}

size_t ref_public_bytes(void) { return (size_t)CRYPTO_PUBLICKEYBYTES; }
size_t ref_secret_bytes(void) { return (size_t)CRYPTO_SECRETKEYBYTES; }
size_t ref_signature_bytes(void) { return (size_t)CRYPTO_BYTES; }
const char *ref_algorithm(void) { return "QRUOV"; }
const char *ref_parameter(void) { return REF_PARAMETER; }
const char *ref_implementation(void) { return REF_IMPLEMENTATION; }

int ref_keypair(uint8_t *pk, uint8_t *sk)
{
    return crypto_sign_keypair(pk, sk);
}

int ref_sign(uint8_t *signed_message, size_t *length, const uint8_t *message,
             size_t length_in, const uint8_t *sk)
{
    unsigned long long result_length = 0;
    int result;
    if (length == NULL)
        return -1;
    *length = 0;
    if (length_in > SIZE_MAX - ref_signature_bytes())
        return -1;
#if SIZE_MAX > ULLONG_MAX
    if (length_in > ULLONG_MAX - ref_signature_bytes())
        return -1;
#endif
    result = crypto_sign(signed_message, &result_length, message,
                         (unsigned long long)length_in, sk);
    if (result != 0)
        return result;
#if SIZE_MAX < ULLONG_MAX
    if (result_length > SIZE_MAX)
        return -1;
#endif
    *length = (size_t)result_length;
    return 0;
}

int ref_open(uint8_t *message, size_t *length, const uint8_t *signed_message,
             size_t length_in, const uint8_t *pk)
{
    unsigned long long result_length = 0;
    int result;
    if (length == NULL)
        return -1;
    *length = 0;
#if SIZE_MAX > ULLONG_MAX
    if (length_in > ULLONG_MAX)
        return -1;
#endif
    result = crypto_sign_open(message, &result_length, signed_message,
                              (unsigned long long)length_in, pk);
    if (result != 0)
        return result;
#if SIZE_MAX < ULLONG_MAX
    if (result_length > SIZE_MAX)
        return -1;
#endif
    *length = (size_t)result_length;
    return 0;
}
