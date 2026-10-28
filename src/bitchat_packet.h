/*
 * bitchat_packet.h - 실제 bitchat 프로토콜 패킷 (클라이언트용)
 * BinaryProtocol.kt 기반. 패킷 빌더 + 파서 + ANNOUNCE TLV + PKCS#7 서명.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BITCHAT_VERSION_1     1
#define BITCHAT_HEADER_V1_LEN 14
#define BITCHAT_SENDER_ID_SIZE 8
#define BITCHAT_SIG_LEN       64
#define BITCHAT_MAX_PAYLOAD   650
#define BITCHAT_MAX_PKT_LEN   (BITCHAT_HEADER_V1_LEN + BITCHAT_SENDER_ID_SIZE + 8 + BITCHAT_MAX_PAYLOAD + BITCHAT_SIG_LEN + 64)
#define BITCHAT_FLAG_HAS_RECIPIENT 0x01
#define BITCHAT_FLAG_HAS_SIGNATURE 0x02
#define BITCHAT_MSG_ANNOUNCE 0x01
#define BITCHAT_MSG_MESSAGE  0x02
#define BITCHAT_MSG_LEAVE    0x03
#define BITCHAT_BROADCAST_ID 0xFF
#define BITCHAT_BLE_SERVICE_UUID "f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c"
#define BITCHAT_BLE_CHAR_UUID    "a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d"
#define BITCHAT_ID_STR_LEN    17

typedef struct {
    uint8_t version, type, ttl, flags;
    uint64_t timestamp; uint32_t payload_len;
    const uint8_t *sender_id, *recipient_id, *payload, *signature;
    size_t total_len;
} bitchat_packet_view_t;

typedef struct { uint8_t buffer[BITCHAT_MAX_PKT_LEN]; size_t used; } bitchat_packet_builder_t;

bool bitchat_parse(const uint8_t* buf, size_t len, bitchat_packet_view_t* view);
size_t bitchat_compute_signing_message(const uint8_t* pkt, size_t pkt_len,
    const bitchat_packet_view_t* v, uint8_t* out, size_t out_max);
size_t bitchat_decrement_ttl(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_max);
void bitchat_id_hex(const uint8_t* id, char* out);
size_t bitchat_build_packet(bitchat_packet_builder_t* b, uint8_t type, uint8_t ttl,
    const uint8_t* sender_id, uint64_t timestamp,
    const uint8_t* payload, uint16_t payload_len,
    const uint8_t* recipient_id, bool has_signature);
size_t bitchat_builder_sig_offset(const bitchat_packet_builder_t* b);
size_t bitchat_build_announce_payload(uint8_t* out, size_t out_max,
    const char* nickname, const uint8_t* x25519_pk, const uint8_t* ed25519_pk);
const char* bitchat_type_name(uint8_t type);
