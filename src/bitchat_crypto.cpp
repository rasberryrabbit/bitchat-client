/*
 * bitchat_crypto.cpp - Ed25519 (libsodium)
 */

#include "bitchat_crypto.h"
#include <sodium.h>
#include <string.h>

static bool g_init = false;

void bitchat_crypto_init(void) {
    if (g_init) return;
    sodium_init();
    g_init = true;
}

bool bitchat_generate_keypair(bitchat_keypair_t* kp) {
    if (!g_init) bitchat_crypto_init();
    if (!kp) return false;
    memset(kp, 0, sizeof(*kp));
    if (crypto_sign_keypair(kp->public_key, kp->private_key) != 0) return false;
    kp->ready = true;
    return true;
}

bool bitchat_keypair_from_seed(const uint8_t seed[32],
                                bitchat_keypair_t* kp) {
    if (!g_init) bitchat_crypto_init();
    if (!seed || !kp) return false;
    memset(kp, 0, sizeof(*kp));
    // libsodium: seed -> pk, sk
    if (crypto_sign_seed_keypair(kp->public_key, kp->private_key, seed) != 0) {
        return false;
    }
    kp->ready = true;
    return true;
}

bool bitchat_sign(const bitchat_keypair_t* kp,
                  const uint8_t* message, size_t msg_len,
                  uint8_t* out_signature, size_t sig_max) {
    if (!g_init) bitchat_crypto_init();
    if (!kp || !message || !out_signature || !kp->ready) return false;
    if (sig_max < crypto_sign_BYTES) return false;
    unsigned long long sig_len = 0;
    if (crypto_sign_detached(out_signature, &sig_len,
                              message, (unsigned long long)msg_len,
                              kp->private_key) != 0) {
        return false;
    }
    return (sig_len == crypto_sign_BYTES);
}

bool bitchat_verify(const uint8_t* public_key, size_t pk_len,
                     const uint8_t* message, size_t msg_len,
                     const uint8_t* signature, size_t sig_len) {
    if (!g_init) bitchat_crypto_init();
    if (!public_key || !message || !signature) return false;
    if (pk_len != crypto_sign_PUBLICKEYBYTES) return false;
    if (sig_len != crypto_sign_BYTES) return false;
    return crypto_sign_verify_detached(signature,
                                        message, (unsigned long long)msg_len,
                                        public_key) == 0;
}
