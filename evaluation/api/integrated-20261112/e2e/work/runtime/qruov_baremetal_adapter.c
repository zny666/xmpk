#include "e2e_runtime.h"
#include "ref_api.h"

#include "api.h"

#include <stddef.h>
#include <stdint.h>

#ifndef PROFILE_PARAMETER
#define PROFILE_PARAMETER "1q127L3"
#endif
#ifndef PROFILE_IMPLEMENTATION
#if defined(XMPK_ACCEL)
#define PROFILE_IMPLEMENTATION "cva6-xmpk-gf127"
#else
#define PROFILE_IMPLEMENTATION "cva6-ref-portable-aes"
#endif
#endif

void qruov_randombytes(unsigned char *out, unsigned long long length) {
  (void)e2e_randombytes(out, length);
}

size_t ref_public_bytes(void) { return (size_t)CRYPTO_PUBLICKEYBYTES; }
size_t ref_secret_bytes(void) { return (size_t)CRYPTO_SECRETKEYBYTES; }
size_t ref_signature_bytes(void) { return (size_t)CRYPTO_BYTES; }
const char *ref_algorithm(void) { return "QRUOV"; }
const char *ref_parameter(void) { return PROFILE_PARAMETER; }
const char *ref_implementation(void) { return PROFILE_IMPLEMENTATION; }

int ref_keypair(uint8_t *pk, uint8_t *sk) {
  return crypto_sign_keypair(pk, sk);
}

int ref_sign(uint8_t *signed_message, size_t *length,
             const uint8_t *message, size_t length_in, const uint8_t *sk) {
  unsigned long long result_length = 0;
  int result = crypto_sign(signed_message, &result_length, message,
                           (unsigned long long)length_in, sk);
  *length = (size_t)result_length;
  return result;
}

int ref_open(uint8_t *message, size_t *length,
             const uint8_t *signed_message, size_t length_in,
             const uint8_t *pk) {
  unsigned long long result_length = 0;
  int result = crypto_sign_open(message, &result_length, signed_message,
                                (unsigned long long)length_in, pk);
  *length = (size_t)result_length;
  return result;
}
