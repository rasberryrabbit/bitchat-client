/* bitchat_packet.cpp - 패킷 빌더/파서/서명 (BinaryProtocol.kt 포팅) */
#include "bitchat_packet.h"
#include <string.h>
#include <stdio.h>

static inline uint64_t rd_be_u64(const uint8_t* p){uint64_t v=0;for(int i=0;i<8;++i)v=(v<<8)|p[i];return v;}
static inline uint16_t rd_be_u16(const uint8_t* p){return(p[0]<<8)|p[1];}
static inline void wr_be_u64(uint8_t* p,uint64_t v){for(int i=7;i>=0;--i){p[i]=v&0xFF;v>>=8;}}
static inline void wr_be_u16(uint8_t* p,uint16_t v){p[0]=v>>8;p[1]=v&0xFF;}

bool bitchat_parse(const uint8_t* buf, size_t len, bitchat_packet_view_t* v) {
    if(!buf||!v||len<BITCHAT_HEADER_V1_LEN+BITCHAT_SENDER_ID_SIZE) return false;
    memset(v,0,sizeof(*v));
    v->version=buf[0]; if(v->version!=1&&v->version!=2) return false;
    v->type=buf[1]; v->ttl=buf[2]; v->timestamp=rd_be_u64(buf+3); v->flags=buf[11];
    bool has_recipient=(v->flags&BITCHAT_FLAG_HAS_RECIPIENT)!=0;
    bool has_sig=(v->flags&BITCHAT_FLAG_HAS_SIGNATURE)!=0;
    v->payload_len=rd_be_u16(buf+12);
    if(v->payload_len>BITCHAT_MAX_PAYLOAD) return false;
    size_t off=14;
    if(off+8>len) return false; v->sender_id=buf+off; off+=8;
    if(has_recipient){if(off+8>len) return false; v->recipient_id=buf+off; off+=8;}
    if(off+v->payload_len>len) return false; v->payload=buf+off; off+=v->payload_len;
    if(has_sig){if(off+64>len) return false; v->signature=buf+off; off+=64;}
    v->total_len=off; return true;
}

size_t bitchat_compute_signing_message(const uint8_t* pkt, size_t pkt_len,
    const bitchat_packet_view_t* v, uint8_t* out, size_t out_max) {
    if(!pkt||!v||!out) return 0;
    size_t sig_off=v->total_len;
    if(v->signature) sig_off-=BITCHAT_SIG_LEN;
    if(sig_off==0||sig_off>pkt_len) return 0;
    memcpy(out,pkt,sig_off);
    out[2]=0; out[11]&=~BITCHAT_FLAG_HAS_SIGNATURE;
    static const int bs[]={256,512,1024,2048};
    int ts=(int)sig_off+16, target=(int)sig_off;
    for(int i=0;i<4;++i){if(ts<=bs[i]){target=bs[i];break;}}
    if(target>(int)sig_off&&target<=(int)out_max){
        int pad=target-(int)sig_off;
        if(pad>0&&pad<=255){memset(out+sig_off,pad,pad);return target;}
    }
    return sig_off;
}

size_t bitchat_decrement_ttl(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_max) {
    bitchat_packet_view_t v;
    if(!bitchat_parse(src,src_len,&v)) return 0;
    if(v.ttl==0||v.total_len>dst_max) return 0;
    memcpy(dst,src,v.total_len); dst[2]=v.ttl-1; return v.total_len;
}

void bitchat_id_hex(const uint8_t* id, char* out) {
    static const char* h="0123456789abcdef";
    for(int i=0;i<8;++i){out[i*2]=h[(id[i]>>4)&0xF];out[i*2+1]=h[id[i]&0xF];}
    out[16]=0;
}

size_t bitchat_build_packet(bitchat_packet_builder_t* b, uint8_t type, uint8_t ttl,
    const uint8_t* sender_id, uint64_t timestamp,
    const uint8_t* payload, uint16_t payload_len,
    const uint8_t* recipient_id, bool has_signature) {
    if(!b||!sender_id) return 0;
    uint8_t flags=0;
    if(recipient_id) flags|=BITCHAT_FLAG_HAS_RECIPIENT;
    if(has_signature) flags|=BITCHAT_FLAG_HAS_SIGNATURE;
    size_t off=0;
    b->buffer[off++]=BITCHAT_VERSION_1; b->buffer[off++]=type; b->buffer[off++]=ttl;
    wr_be_u64(b->buffer+off,timestamp); off+=8;
    b->buffer[off++]=flags; wr_be_u16(b->buffer+off,payload_len); off+=2;
    memcpy(b->buffer+off,sender_id,8); off+=8;
    if(recipient_id){memcpy(b->buffer+off,recipient_id,8); off+=8;}
    if(payload_len){memcpy(b->buffer+off,payload,payload_len); off+=payload_len;}
    if(has_signature){memset(b->buffer+off,0,64); off+=64;}
    b->used=off; return off;
}

size_t bitchat_builder_sig_offset(const bitchat_packet_builder_t* b) {
    if(!b||!b->used) return 0;
    return b->used-BITCHAT_SIG_LEN;
}

size_t bitchat_build_announce_payload(uint8_t* out, size_t out_max,
    const char* nickname, const uint8_t* x25519_pk, const uint8_t* ed25519_pk) {
    if(!out||!nickname) return 0;
    size_t off=0;
    // TLV type 1: nickname
    size_t nlen=strlen(nickname); if(nlen>255) nlen=255;
    out[off++]=0x01; out[off++]=(uint8_t)nlen;
    memcpy(out+off,nickname,nlen); off+=nlen;
    // TLV type 2: X25519 encryption key (32B)
    out[off++]=0x02; out[off++]=32;
    if(x25519_pk) memcpy(out+off,x25519_pk,32); else memset(out+off,0,32);
    off+=32;
    // TLV type 3: Ed25519 signing key (32B) ← relay가 추출하는 키!
    out[off++]=0x03; out[off++]=32;
    if(ed25519_pk) memcpy(out+off,ed25519_pk,32); else memset(out+off,0,32);
    off+=32;
    // TLV type 5: version info
    out[off++]=0x05; out[off++]=2; out[off++]=0x00; out[off++]=0x01;
    return off;
}

const char* bitchat_type_name(uint8_t type) {
    switch(type){case BITCHAT_MSG_ANNOUNCE:return"ANNOUNCE";case BITCHAT_MSG_MESSAGE:return"MESSAGE";
    case BITCHAT_MSG_LEAVE:return"LEAVE";default:return"UNKNOWN";}
}
