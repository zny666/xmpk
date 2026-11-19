#include "ref_api.h"
#include "api.h"
#include "enc.h"
#include "xof.h"
#include <limits.h>

#ifndef REF_PARAMETER
#error "REF_PARAMETER must identify the selected MQOM parameter"
#endif
#ifndef REF_IMPLEMENTATION
#error "REF_IMPLEMENTATION must identify the reference implementation"
#endif
#if MQOM3_PARAM_BASE_FIELD != 4 || MQOM3_PARAM_TRADEOFF != 0 || MQOM3_PARAM_OT_VARIANT != 0
#error "R20 MQOM requires gf16_fast_ct (ct means correlated-tree)"
#endif
#if !defined(FIELDS_REF) || !defined(RIJNDAEL_BITSLICE) || !defined(NO_MISALIGNED_ACCESSES)
#error "Explicit portable field, Rijndael and Keccak selections are required"
#endif
#if defined(FIELDS_AVX2) || defined(FIELDS_AVX512) || defined(FIELDS_NEON) || \
    defined(RIJNDAEL_AES_NI) || defined(RIJNDAEL_VAES) || defined(RIJNDAEL_ARM_AES) || \
    defined(RIJNDAEL_OPT_ARMV7M) || defined(RIJNDAEL_EXTERNAL) || \
    defined(MQOM3_XOF_EXTERNAL_API) || defined(MQOM3_FOR_LIBOQS) || \
    defined(MQOM3_FOR_MUPQ) || defined(SUPERCOP) || defined(XKCP_has_KeccakP1600times8) || \
    defined(KeccakP1600_plain64_useSHLD)
#error "Hardware/external primitive backends are not permitted"
#endif
#if defined(__AVX__) || defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__riscv_vector)
#error "Vector targets are not permitted in this reference profile"
#endif

/* Use the unmodified unsigned-long-long public API. MQOM's ordinary branch
 * already declares int randombytes(unsigned char *, unsigned long long), so
 * no LIBOQS/MUPQ platform switch or replacement internal PRG is needed.
 */
size_t ref_public_bytes(void) { return (size_t)CRYPTO_PUBLICKEYBYTES; }
size_t ref_secret_bytes(void) { return (size_t)CRYPTO_SECRETKEYBYTES; }
size_t ref_signature_bytes(void) { return (size_t)CRYPTO_BYTES; }
const char *ref_algorithm(void) { return "MQOM"; }
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
