#ifndef REF_PROFILE_API_H
#define REF_PROFILE_API_H
#include <stddef.h>
#include <stdint.h>
size_t ref_public_bytes(void);
size_t ref_secret_bytes(void);
size_t ref_signature_bytes(void);
const char *ref_algorithm(void);
const char *ref_parameter(void);
const char *ref_implementation(void);
int ref_keypair(uint8_t *pk,uint8_t *sk);
int ref_sign(uint8_t *signed_message,size_t *length,const uint8_t *message,size_t length_in,const uint8_t *sk);
int ref_open(uint8_t *message,size_t *length,const uint8_t *signed_message,size_t length_in,const uint8_t *pk);
#endif
