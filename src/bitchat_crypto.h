/*
 * bitchat_crypto.h - Ed25519 signing (libsodium)
 *
 * 클라이언트 라이브러리는 libsodium을 사용하여 Ed25519 서명/검증 수행.
 * - 자체 키쌍 생성 (libsodium이 자체 entropy 사용)
 * - 메시지 서명 (송신 패킷)
 * - 서명 검증 (수신 패킷 - 선택적, relay가 이미 검증함)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t private_key[64];   // libsodium secret key (64B = seed[32] + pub[32])
    uint8_t public_key[32];    // Ed25519 public key
    bool    ready;
} bitchat_keypair_t;

void bitchat_crypto_init(void);

bool bitchat_generate_keypair(bitchat_keypair_t* kp);

// 시드로부터 결정론적 키쌍 생성 (백업/복원용)
bool bitchat_keypair_from_seed(const uint8_t seed[32],
                                bitchat_keypair_t* kp);

bool bitchat_sign(const bitchat_keypair_t* kp,
                  const uint8_t* message, size_t msg_len,
                  uint8_t* out_signature, size_t sig_max);

bool bitchat_verify(const uint8_t* public_key, size_t pk_len,
                     const uint8_t* message, size_t msg_len,
                     const uint8_t* signature, size_t sig_len);

#ifdef __cplusplus
}
#endif
