/*
 * bitchat_packet.h - bitchat wire packet (client library version)
 *
 * 한국어:
 *   bitchat-esp32-relay 펌웨어와 동일한 패킷 포맷 사용.
 *   호환성을 위해 모든 매직넘버/필드/상수가 동일하게 정의됨.
 *
 *   패킷 레이아웃 (relay와 동일):
 *   +-----+-----+-----+-----+------------------------+
 *   |magic| ver |type | ttl | sender_pk (32 bytes)   |
 *   +-----+-----+-----+-----+------------------------+
 *   | counter (8 bytes, BE)                              |
 *   +--------------------------------------------------+
 *   | payload_len (2 bytes, BE)                         |
 *   +--------------------------------------------------+
 *   | payload (variable)                                |
 *   +--------------------------------------------------+
 *   | signature (64 bytes Ed25519)                      |
 *   +--------------------------------------------------+
 *
 *   클라이언트는 일반적으로 TTL=7 (최대)로 송신.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define BITCHAT_MAGIC            0xBC
#define BITCHAT_VERSION          0x01
#define BITCHAT_MAX_TTL          7
#define BITCHAT_MAX_PAYLOAD_LEN  512

#define BITCHAT_PUBKEY_LEN       32
#define BITCHAT_SIG_LEN          64
#define BITCHAT_ID_STR_LEN       13   // 12 hex chars + NUL

enum BitchatPacketType : uint8_t {
    BITCHAT_TYPE_CHAT         = 0x01,
    BITCHAT_TYPE_ACK          = 0x02,
    BITCHAT_TYPE_PEER_INFO    = 0x03,
    BITCHAT_TYPE_PEER_REQUEST = 0x04,
    BITCHAT_TYPE_LEAVE        = 0x05,
    BITCHAT_TYPE_KEY_EXCHANGE = 0x06,
    BITCHAT_TYPE_PING         = 0x07,
};

#define BITCHAT_HEADER_LEN  (1 + 1 + 1 + 1 + BITCHAT_PUBKEY_LEN + 8 + 2)
#define BITCHAT_MIN_PKT_LEN (BITCHAT_HEADER_LEN + BITCHAT_SIG_LEN)
#define BITCHAT_MAX_PKT_LEN (BITCHAT_HEADER_LEN + BITCHAT_MAX_PAYLOAD_LEN + BITCHAT_SIG_LEN)

typedef struct {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  ttl;
    const uint8_t* sender_pk;
    uint64_t counter;
    uint16_t payload_len;
    const uint8_t* payload;
    const uint8_t* signature;
} bitchat_packet_view_t;

typedef struct {
    uint8_t  buffer[BITCHAT_MAX_PKT_LEN];
    size_t   used;
} bitchat_packet_builder_t;

#ifdef __cplusplus
extern "C" {
#endif

// 패킷 파싱
bool bitchat_parse(const uint8_t* buf, size_t len, bitchat_packet_view_t* view);

// 서명 메시지 추출 (magic..payload 직렬화)
void bitchat_signing_message(const bitchat_packet_view_t* v,
                             uint8_t* out_buf, size_t* out_len);

// 사람 친화적 노드 ID (pk 앞 6바이트 hex)
void bitchat_id_from_pk(const uint8_t* pk, char* out_id);

// TTL 1 감소
size_t bitchat_decrement_ttl(const uint8_t* src, size_t src_len,
                             uint8_t* dst, size_t dst_max);

// 패킷 빌드 (헤더 + payload, 서명 자리 비움)
size_t bitchat_build_header(bitchat_packet_builder_t* b,
                            uint8_t type, uint8_t ttl,
                            const uint8_t* sender_pk, uint32_t counter,
                            const uint8_t* payload, uint16_t payload_len);

// 빌더 끝에 64바이트 서명 자리 예약 (실제 서명은 caller/crypto가 채움)
size_t bitchat_builder_reserve_sig(bitchat_packet_builder_t* b);

// 패킷 타입 이름 (디버그용)
const char* bitchat_type_name(uint8_t type);

#ifdef __cplusplus
}
#endif
