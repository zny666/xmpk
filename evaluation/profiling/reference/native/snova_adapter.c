#include "ref_api.h"
#include <api.h>

#if !defined(SNOVA_REF) || SNOVA_REF != 1
#error "SNOVA requires the official Reference_Implementation with SNOVA_REF=1"
#endif
#ifndef AESCTR
#error "This profile only covers SNOVA I/III/V_B_AES"
#endif

_Static_assert(sizeof(size_t) == sizeof(unsigned long long),
               "This profile requires a 64-bit size_t/ULL ABI");

size_t ref_public_bytes(void) { return CRYPTO_PUBLICKEYBYTES; }
size_t ref_secret_bytes(void) { return CRYPTO_SECRETKEYBYTES; }
size_t ref_signature_bytes(void) { return CRYPTO_BYTES; }
const char *ref_algorithm(void) { return "SNOVA"; }
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
