/* bitchat_identity.h - 자체 키쌍 + 8B senderID + NVS 영속화 */
#pragma once
#include "bitchat_crypto.h"
#include <stdint.h>
#define BCHAT_CLI_NVS_NS "bchatcli"
#define BCHAT_CLI_NVS_SEED "seed"
bool bitchat_identity_init(bitchat_keypair_t* out_kp);
const uint8_t* bitchat_identity_pk(const bitchat_keypair_t* kp);
void bitchat_identity_sender_id(const bitchat_keypair_t* kp, uint8_t out_id[8]);
