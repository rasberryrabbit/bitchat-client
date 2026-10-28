/* bitchat_client.cpp - 실제 bitchat 프로토콜 클라이언트 구현 */
#include "bitchat_client.h"
#include <stdarg.h>
#include <string.h>

BitchatClient* BitchatClient::_instance = nullptr;

BitchatClient::BitchatClient() : _client(nullptr), _remote_char(nullptr),
    _msg_counter(0), _chat_cb(nullptr), _log_cb(nullptr) {
    _instance = this;
    memset(&_kp, 0, sizeof(_kp));
    memset(_sender_id, 0, sizeof(_sender_id));
}
BitchatClient::~BitchatClient() {
    if(_client){if(_client->isConnected())_client->disconnect();NimBLEDevice::deleteClient(_client);}
    _instance=nullptr;
}
void BitchatClient::log(const char* fmt, ...) {
    if(!_log_cb) return;
    char buf[256]; va_list ap; va_start(ap,fmt); vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
    _log_cb(buf);
}
bool BitchatClient::begin(const char* device_name) {
    bitchat_crypto_init();
    if(!bitchat_identity_init(&_kp)) { log("identity init failed"); return false; }
    bitchat_identity_sender_id(&_kp, _sender_id);
    char id[17]; bitchat_id_hex(_sender_id, id);
    log("identity ready: node_id=%s", id);
    NimBLEDevice::init(device_name);
    NimBLEDevice::setMTU(247);
    return true;
}
bool BitchatClient::connect_to_relay(uint32_t timeout_ms) {
    if(is_connected()) return true;
    NimBLEScan* scan = NimBLEDevice::getScan();
    NimBLEAdvertisedDevice* best = nullptr;
    int best_rssi = -200;
    scan->setInterval(100); scan->setWindow(50); scan->setActiveScan(true);
    uint32_t start = millis();
    while(millis()-start < timeout_ms) {
        NimBLEScanResults results = scan->start(1000, false);
        for(int i=0; i<results.getCount(); ++i) {
            NimBLEAdvertisedDevice dev = results.getDevice(i);
            if(dev.isAdvertisingService(NimBLEUUID(BITCHAT_BLE_SERVICE_UUID))) {
                if(dev.getRSSI() > best_rssi) { best_rssi = dev.getRSSI(); best = new NimBLEAdvertisedDevice(dev); }
            }
        }
        if(best) break;
    }
    if(!best) { log("no relay found"); return false; }
    log("connecting to relay rssi=%d...", best_rssi);
    _client = NimBLEDevice::createClient();
    if(!_client->connect(best->getAddress())) {
        log("connect failed"); NimBLEDevice::deleteClient(_client); _client=nullptr; delete best; return false;
    }
    delete best;
    NimBLERemoteService* svc = _client->getService(BITCHAT_BLE_SERVICE_UUID);
    if(!svc) { log("service not found"); _client->disconnect(); NimBLEDevice::deleteClient(_client); _client=nullptr; return false; }
    _remote_char = svc->getCharacteristic(BITCHAT_BLE_CHAR_UUID);
    if(!_remote_char) { log("char not found"); _client->disconnect(); NimBLEDevice::deleteClient(_client); _client=nullptr; return false; }
    if(_remote_char->canNotify()) {
        _remote_char->subscribe(true, [](NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
            if(isNotify && data && len > 0 && _instance) _instance->handle_notify(data, len);
        });
    }
    log("connected to relay");
    return true;
}
void BitchatClient::disconnect() {
    if(_client) { if(_client->isConnected())_client->disconnect(); NimBLEDevice::deleteClient(_client); _client=nullptr; }
    _remote_char=nullptr;
}
bool BitchatClient::send_announce(const String& nickname) {
    if(!is_connected()) return false;
    // ANNOUNCE payload 빌드 (TLV: nickname + dummy X25519 + Ed25519 pk)
    uint8_t payload[256];
    size_t plen = bitchat_build_announce_payload(payload, sizeof(payload),
        nickname.c_str(), nullptr, _kp.pk);
    // 패킷 빌드 (has_signature=true, TTL=7)
    uint64_t ts = millis(); // timestamp
    bitchat_packet_builder_t b;
    bitchat_build_packet(&b, BITCHAT_MSG_ANNOUNCE, 7, _sender_id, ts,
        payload, (uint16_t)plen, nullptr, true);
    // 서명 메시지 계산 (TTL=0, signature 제외, PKCS#7 패딩)
    uint8_t sign_msg[BITCHAT_MAX_PKT_LEN];
    bitchat_packet_view_t v;
    bitchat_parse(b.buffer, b.used, &v);
    size_t sign_len = bitchat_compute_signing_message(b.buffer, b.used, &v, sign_msg, sizeof(sign_msg));
    if(sign_len == 0) { log("signing msg failed"); return false; }
    // Ed25519 서명
    uint8_t sig[64];
    if(!bitchat_sign(&_kp, sign_msg, sign_len, sig, sizeof(sig))) { log("sign failed"); return false; }
    // 서명을 패킷에 채우기
    size_t sig_off = bitchat_builder_sig_offset(&b);
    memcpy(b.buffer + sig_off, sig, 64);
    // BLE 전송
    std::string val((const char*)b.buffer, b.used);
    bool ok = _remote_char->writeValue(val, true);
    log("ANNOUNCE sent (nickname=%s, %u bytes, sig_len=%u)", nickname.c_str(), (unsigned)b.used, (unsigned)sign_len);
    return ok;
}
bool BitchatClient::send_message(const uint8_t* payload, size_t len) {
    if(!is_connected()) return false;
    uint64_t ts = millis();
    // broadcast (recipient = all 0xFF)
    uint8_t broadcast_id[8]; memset(broadcast_id, 0xFF, 8);
    bitchat_packet_builder_t b;
    bitchat_build_packet(&b, BITCHAT_MSG_MESSAGE, 7, _sender_id, ts,
        payload, (uint16_t)len, broadcast_id, true);
    // 서명
    uint8_t sign_msg[BITCHAT_MAX_PKT_LEN];
    bitchat_packet_view_t v;
    bitchat_parse(b.buffer, b.used, &v);
    size_t sign_len = bitchat_compute_signing_message(b.buffer, b.used, &v, sign_msg, sizeof(sign_msg));
    if(sign_len == 0) return false;
    uint8_t sig[64];
    if(!bitchat_sign(&_kp, sign_msg, sign_len, sig, sizeof(sig))) return false;
    size_t sig_off = bitchat_builder_sig_offset(&b);
    memcpy(b.buffer + sig_off, sig, 64);
    std::string val((const char*)b.buffer, b.used);
    bool ok = _remote_char->writeValue(val, true);
    log("MESSAGE sent (%u bytes)", (unsigned)b.used);
    return ok;
}
bool BitchatClient::send_message(const String& text) {
    return send_message((const uint8_t*)text.c_str(), text.length());
}
void BitchatClient::handle_notify(const uint8_t* data, size_t len) {
    bitchat_packet_view_t v;
    if(!bitchat_parse(data, len, &v)) return;
    char sender_id[17]; bitchat_id_hex(v.sender_id, sender_id);
    if(v.type == BITCHAT_MSG_MESSAGE) {
        log("recv MESSAGE from %s (%u bytes)", sender_id, v.payload_len);
        if(_chat_cb) _chat_cb(sender_id, v.payload, v.payload_len);
    } else {
        log("recv %s from %s", bitchat_type_name(v.type), sender_id);
    }
}
void BitchatClient::loop() {
    // keep-alive PING every 60s
    static uint32_t last_ping = 0;
    if(is_connected() && millis()-last_ping > 60000) {
        last_ping = millis();
        // send empty ANNOUNCE as keep-alive
        send_announce("ping");
    }
    delay(1);
}
