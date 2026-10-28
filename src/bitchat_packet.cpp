/*
 * bitchat_packet.cpp - 패킷 직렬화/역직렬화 (client library version)
 */

#include "bitchat_packet.h"
#include <string.h>

static inline uint64_t rd_be_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}
static inline uint16_t rd_be_u16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline void wr_be_u64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}
static inline void wr_be_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

bool bitchat_parse(const uint8_t* buf, size_t len, bitchat_packet_view_t* view) {
    if (!buf || !view) return false;
    if (len < BITCHAT_MIN_PKT_LEN || len > BITCHAT_MAX_PKT_LEN) return false;
    if (buf[0] != BITCHAT_MAGIC) return false;
    uint8_t ver  = buf[1];
    uint8_t type = buf[2];
    uint8_t ttl  = buf[3];
    if (ver != BITCHAT_VERSION) return false;
    if (ttl > BITCHAT_MAX_TTL) return false;
    const uint8_t* pk        = buf + 4;
    const uint8_t* counter_p = buf + 4 + BITCHAT_PUBKEY_LEN;
    const uint8_t* len_p     = counter_p + 8;
    uint16_t payload_len     = rd_be_u16(len_p);
    if (payload_len > BITCHAT_MAX_PAYLOAD_LEN) return false;
    size_t expected = BITCHAT_HEADER_LEN + (size_t)payload_len + BITCHAT_SIG_LEN;
    if (len != expected) return false;
    view->magic        = buf[0];
    view->version      = ver;
    view->type         = type;
    view->ttl          = ttl;
    view->sender_pk    = pk;
    view->counter      = rd_be_u64(counter_p);
    view->payload_len  = payload_len;
    view->payload      = len_p + 2;
    view->signature    = view->payload + payload_len;
    return true;
}

void bitchat_signing_message(const bitchat_packet_view_t* v,
                             uint8_t* out_buf, size_t* out_len) {
    if (!v || !out_buf || !out_len) { if (out_len) *out_len = 0; return; }
    size_t off = 0;
    out_buf[off++] = v->magic;
    out_buf[off++] = v->version;
    out_buf[off++] = v->type;
    out_buf[off++] = v->ttl;
    memcpy(out_buf + off, v->sender_pk, BITCHAT_PUBKEY_LEN);
    off += BITCHAT_PUBKEY_LEN;
    wr_be_u64(out_buf + off, v->counter);
    off += 8;
    wr_be_u16(out_buf + off, v->payload_len);
    off += 2;
    if (v->payload_len) {
        memcpy(out_buf + off, v->payload, v->payload_len);
        off += v->payload_len;
    }
    *out_len = off;
}

void bitchat_id_from_pk(const uint8_t* pk, char* out_id) {
    if (!pk || !out_id) return;
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 6; ++i) {
        out_id[i * 2]     = hex[(pk[i] >> 4) & 0xF];
        out_id[i * 2 + 1] = hex[pk[i] & 0xF];
    }
    out_id[12] = 0;
}

size_t bitchat_decrement_ttl(const uint8_t* src, size_t src_len,
                             uint8_t* dst, size_t dst_max) {
    bitchat_packet_view_t v;
    if (!bitchat_parse(src, src_len, &v)) return 0;
    if (v.ttl == 0) return 0;
    size_t total = BITCHAT_HEADER_LEN + v.payload_len + BITCHAT_SIG_LEN;
    if (total > dst_max) return 0;
    memcpy(dst, src, total);
    dst[3] = v.ttl - 1;
    return total;
}

size_t bitchat_build_header(bitchat_packet_builder_t* b,
                            uint8_t type, uint8_t ttl,
                            const uint8_t* sender_pk, uint32_t counter,
                            const uint8_t* payload, uint16_t payload_len) {
    if (!b || !sender_pk) return 0;
    if (payload_len > 0 && !payload) return 0;
    if (payload_len > BITCHAT_MAX_PAYLOAD_LEN) return 0;
    if (ttl > BITCHAT_MAX_TTL) return 0;

    size_t off = 0;
    b->buffer[off++] = BITCHAT_MAGIC;
    b->buffer[off++] = BITCHAT_VERSION;
    b->buffer[off++] = type;
    b->buffer[off++] = ttl;
    memcpy(b->buffer + off, sender_pk, BITCHAT_PUBKEY_LEN);
    off += BITCHAT_PUBKEY_LEN;
    wr_be_u64(b->buffer + off, counter);
    off += 8;
    wr_be_u16(b->buffer + off, payload_len);
    off += 2;
    if (payload_len) {
        memcpy(b->buffer + off, payload, payload_len);
        off += payload_len;
    }
    b->used = off;
    return off;
}

size_t bitchat_builder_reserve_sig(bitchat_packet_builder_t* b) {
    if (!b) return 0;
    if (b->used + BITCHAT_SIG_LEN > sizeof(b->buffer)) return 0;
    size_t sig_off = b->used;
    memset(b->buffer + sig_off, 0, BITCHAT_SIG_LEN);
    b->used += BITCHAT_SIG_LEN;
    return sig_off;
}

const char* bitchat_type_name(uint8_t type) {
    switch (type) {
        case BITCHAT_TYPE_CHAT:         return "CHAT";
        case BITCHAT_TYPE_ACK:          return "ACK";
        case BITCHAT_TYPE_PEER_INFO:    return "PEER_INFO";
        case BITCHAT_TYPE_PEER_REQUEST: return "PEER_REQUEST";
        case BITCHAT_TYPE_LEAVE:        return "LEAVE";
        case BITCHAT_TYPE_KEY_EXCHANGE: return "KEY_EXCHANGE";
        case BITCHAT_TYPE_PING:         return "PING";
        default:                        return "UNKNOWN";
    }
}
