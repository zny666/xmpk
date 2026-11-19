#include "prim.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern void ov_fips202_ref_shake128_init(keccak_state *state);
extern void ov_fips202_ref_shake128_absorb(keccak_state *state,
                                           const uint8_t *in, size_t inlen);
extern void ov_fips202_ref_shake128_finalize(keccak_state *state);
extern void ov_fips202_ref_shake128_squeeze(uint8_t *out, size_t outlen,
                                            keccak_state *state);
extern void ov_fips202_ref_shake256_init(keccak_state *state);
extern void ov_fips202_ref_shake256_absorb(keccak_state *state,
                                           const uint8_t *in, size_t inlen);
extern void ov_fips202_ref_shake256_finalize(keccak_state *state);
extern void ov_fips202_ref_shake256_squeeze(uint8_t *out, size_t outlen,
                                            keccak_state *state);

void shake256_init(shake256 *ctx) {
  ov_fips202_ref_shake256_init(&ctx->state);
  ctx->finalized = 0;
}

void shake256_reset(shake256 *ctx) {
  shake256_init(ctx);
}

void shake256_update(shake256 *ctx, const uint8_t *data, size_t size) {
  ov_fips202_ref_shake256_absorb(&ctx->state, data, size);
}

static void shake256_finish(shake256 *ctx) {
  if (!ctx->finalized) {
    ov_fips202_ref_shake256_finalize(&ctx->state);
    ctx->finalized = 1;
  }
}

void shake256_digestfinal(shake256 *ctx, uint8_t *dst, size_t size) {
  shake256_finish(ctx);
  ov_fips202_ref_shake256_squeeze(dst, size, &ctx->state);
}

void shake256_squeeze(shake256 *ctx, uint8_t *dst, size_t size) {
  shake256_finish(ctx);
  ov_fips202_ref_shake256_squeeze(dst, size, &ctx->state);
}

void shake256_free(shake256 *ctx) {
  memset(ctx, 0, sizeof(*ctx));
}

void shake128_init(shake128 *ctx) {
  ov_fips202_ref_shake128_init(&ctx->state);
  ctx->finalized = 0;
}

void shake128_update(shake128 *ctx, const uint8_t *data, size_t size) {
  ov_fips202_ref_shake128_absorb(&ctx->state, data, size);
}

void shake128_digestfinal(shake128 *ctx, uint8_t *dst, size_t size) {
  if (!ctx->finalized) {
    ov_fips202_ref_shake128_finalize(&ctx->state);
    ctx->finalized = 1;
  }
  ov_fips202_ref_shake128_squeeze(dst, size, &ctx->state);
}

void shake128_free(shake128 *ctx) {
  memset(ctx, 0, sizeof(*ctx));
}

static void store_counter(uint8_t block[16], const uint8_t iv[8],
                          uint64_t counter) {
  memcpy(block, iv, 8);
  for (unsigned i = 0; i < 8; ++i)
    block[15 - i] = (uint8_t)(counter >> (8 * i));
}

void aes128ctr_init(aes128 *ctx, const uint8_t key[16]) {
  aes128_keyschedule_ffs_lut(ctx->round_keys, key);
}

void aes128ctr_stream(aes128 *ctx, const uint8_t iv[8], uint8_t *dst,
                      size_t size) {
  uint64_t counter = 0;
  while (size != 0) {
    uint8_t block0[16];
    uint8_t block1[16];
    uint8_t encrypted0[16];
    uint8_t encrypted1[16];
    size_t first = size < 16 ? size : 16;
    size_t second = size > 16 ? (size - 16 < 16 ? size - 16 : 16) : 0;

    store_counter(block0, iv, counter);
    store_counter(block1, iv, counter + 1);
    aes128_encrypt_ffs(encrypted0, encrypted1, block0, block1,
                       ctx->round_keys);
    memcpy(dst, encrypted0, first);
    if (second)
      memcpy(dst + 16, encrypted1, second);
    dst += first + second;
    size -= first + second;
    counter += 2;
  }
}

void aes128ctr_free(aes128 *ctx) {
  memset(ctx, 0, sizeof(*ctx));
}
