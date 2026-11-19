#include "ref_api.h"
#include <api.h>

#if !defined(MAYO_BUILD_TYPE_REF) || !defined(ENABLE_PARAMS_DYNAMIC)
#error "MAYO requires the upstream reference build with dynamic parameters"
#endif
#if defined(MAYO_VARIANT) || defined(MAYO_AVX) || defined(MAYO_NEON) || defined(MAYO_GFNI)
#error "Do not mix the dynamic reference API with fixed or vector backends"
#endif

/* The selected, unmodified src/mayo_N/api.c chooses &MAYO_N. */
size_t ref_public_bytes(void) { return CRYPTO_PUBLICKEYBYTES; }
size_t ref_secret_bytes(void) { return CRYPTO_SECRETKEYBYTES; }
size_t ref_signature_bytes(void) { return CRYPTO_BYTES; }
const char *ref_algorithm(void) { return "MAYO"; }
const char *ref_parameter(void) { return PROFILE_PARAMETER; }
const char *ref_implementation(void) { return PROFILE_IMPLEMENTATION; }

int ref_keypair(uint8_t *pk, uint8_t *sk) {
    return crypto_sign_keypair(pk, sk);
}

int ref_sign(uint8_t *sm, size_t *length, const uint8_t *message,
             size_t length_in, const uint8_t *sk) {
    *length = 0;
    return crypto_sign(sm, length, message, length_in, sk);
}

int ref_open(uint8_t *message, size_t *length, const uint8_t *sm,
             size_t length_in, const uint8_t *pk) {
    *length = 0;
    return crypto_sign_open(message, length, sm, length_in, pk);
}
