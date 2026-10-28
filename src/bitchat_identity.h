/*
 * bitchat_identity.h - 자체 키쌍 영속화 (NVS)
 *
 * 클라이언트도 자체 Ed25519 키쌍을 가지고, 이를 NVS에 저장하여
 * 재부팅 후에도 동일 ID를 유지합니다.
 */

#pragma once

#include "bitchat_crypto.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BCHAT_CLI_NVS_NS  "bchatcli"
#define BCHAT_CLI_NVS_SEED "seed"   // 32B seed (결정론적 키쌍 생성용)

bool bitchat_identity_init(bitchat_keypair_t* out_kp);

// 자체 공개키 (32B) 반환 - 패킷 빌드 시 sender_pk로 사용
const uint8_t* bitchat_identity_pk(const bitchat_keypair_t* kp);

// 사람 친화적 노드 ID (12 hex chars) - pk 앞 6바이트
void bitchat_identity_node_id(const bitchat_keypair_t* kp, char out_id[13]);

#ifdef __cplusplus
}
#endif
