#include "e2e_runtime.h"
#include "ref_api.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern void *malloc(size_t size);
extern void free(void *ptr);
extern void printhex(uint64_t value);
extern void printstr(const char *text);
extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

typedef struct {
  uint64_t cycles;
  uint64_t instret;
} e2e_measurement;

// #region debug-point B-D:preserve-raw-counter-boundaries
static volatile uint64_t debug_sign_cycle_start;
static volatile uint64_t debug_sign_cycle_end;
static volatile uint64_t debug_verify_cycle_start;
static volatile uint64_t debug_verify_cycle_end;
// #endregion

static inline uint64_t read_cycle(void) {
  uint64_t value;
  __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
  return value;
}

static inline uint64_t read_instret(void) {
  uint64_t value;
  __asm__ volatile("rdinstret %0" : "=r"(value) :: "memory");
  return value;
}

static void metric(const char *operation, e2e_measurement result) {
  printstr("E2E_METRIC ");
  printstr(operation);
  printstr(" cycles=0x");
  printhex(result.cycles);
  printstr(" instret=0x");
  printhex(result.instret);
  printstr("\n");
}

static e2e_measurement stop_measurement(uint64_t cycle_start,
                                        uint64_t instret_start) {
  e2e_measurement result;
  __asm__ volatile("fence rw,rw" ::: "memory");
  result.cycles = read_cycle() - cycle_start;
  result.instret = read_instret() - instret_start;
  return result;
}

int main(void) {
  static const uint8_t message[32] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
  size_t public_bytes;
  size_t secret_bytes;
  size_t signature_bytes;
  uint8_t *public_key;
  uint8_t *secret_key;
  uint8_t *signed_message;
  uint8_t *opened_message;
  size_t signed_length = 0;
  size_t opened_length = 0;
  volatile uint64_t cycle_start;
  uint64_t instret_start;
  e2e_measurement keygen;
  e2e_measurement sign;
  e2e_measurement verify;

  e2e_heap_reset();
  e2e_rng_reset(UINT64_C(0x243f6a8885a308d3));
  public_bytes = ref_public_bytes();
  secret_bytes = ref_secret_bytes();
  signature_bytes = ref_signature_bytes() + sizeof(message);
  public_key = malloc(public_bytes);
  secret_key = malloc(secret_bytes);
  signed_message = malloc(signature_bytes);
  opened_message = malloc(signature_bytes);
  if (!public_key || !secret_key || !signed_message || !opened_message)
    tohost_exit(10);

  __asm__ volatile("fence rw,rw" ::: "memory");
  cycle_start = read_cycle();
  instret_start = read_instret();
  if (ref_keypair(public_key, secret_key) != 0)
    tohost_exit(11);
  keygen = stop_measurement(cycle_start, instret_start);

  __asm__ volatile("fence rw,rw" ::: "memory");
  cycle_start = read_cycle();
  instret_start = read_instret();
  // #region debug-point B-C:sign-start
  debug_sign_cycle_start = cycle_start;
  // #endregion
  if (ref_sign(signed_message, &signed_length, message, sizeof(message),
               secret_key) != 0 ||
      signed_length > signature_bytes)
    tohost_exit(12);
  sign = stop_measurement(cycle_start, instret_start);
  // #region debug-point B-D:sign-end
  debug_sign_cycle_end = read_cycle();
  // #endregion

  __asm__ volatile("fence rw,rw" ::: "memory");
  cycle_start = read_cycle();
  instret_start = read_instret();
  // #region debug-point B-C:verify-start
  debug_verify_cycle_start = cycle_start;
  // #endregion
  if (ref_open(opened_message, &opened_length, signed_message, signed_length,
               public_key) != 0)
    tohost_exit(13);
  verify = stop_measurement(cycle_start, instret_start);
  // #region debug-point B-D:verify-end
  debug_verify_cycle_end = read_cycle();
  // #endregion
  if (opened_length != sizeof(message) ||
      memcmp(opened_message, message, sizeof(message)) != 0)
    tohost_exit(14);

  signed_message[0] ^= 1;
  opened_length = 0;
  if (ref_open(opened_message, &opened_length, signed_message, signed_length,
               public_key) == 0)
    tohost_exit(15);
  signed_message[0] ^= 1;

  printstr("E2E_BEGIN algorithm=");
  printstr(ref_algorithm());
  printstr(" parameter=");
  printstr(ref_parameter());
  printstr(" implementation=");
  printstr(ref_implementation());
  printstr("\n");
  metric("keygen", keygen);
  metric("sign", sign);
  metric("verify", verify);
  // #region debug-point B-D:raw-counter-report
  printstr("DEBUG_COUNTER sign_start=0x");
  printhex(debug_sign_cycle_start);
  printstr(" sign_end=0x");
  printhex(debug_sign_cycle_end);
  printstr(" sign_delta=0x");
  printhex(sign.cycles);
  printstr("\nDEBUG_COUNTER verify_start=0x");
  printhex(debug_verify_cycle_start);
  printstr(" verify_end=0x");
  printhex(debug_verify_cycle_end);
  printstr(" verify_delta=0x");
  printhex(verify.cycles);
  printstr("\n");
  // #endregion
  printstr("E2E_SUCCESS\n");

  free(opened_message);
  free(signed_message);
  free(secret_key);
  free(public_key);
  tohost_exit(0);
}
