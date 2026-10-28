/* bitchat_crypto.h - Ed25519 서명/검증 (libsodium) */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct { uint8_t sk[64]; uint8_t pk[32]; bool ready; } bitchat_keypair_t;
void bitchat_crypto_init(void);
bool bitchat_generate_keypair(bitchat_keypair_t* kp);
bool bitchat_keypair_from_seed(const uint8_t seed[32], bitchat_keypair_t* kp);
bool bitchat_sign(const bitchat_keypair_t* kp, const uint8_t* msg, size_t len, uint8_t* sig, size_t sig_max);
bool bitchat_verify(const uint8_t* pk, size_t pk_len, const uint8_t* msg, size_t len, const uint8_t* sig, size_t sig_len);
