/*
 * bitchat_client.cpp - BitchatClient 구현
 *
 * NimBLE-Arduino를 사용한 BLE 클라이언트 + bitchat 패킷 빌더/파서 통합.
 */

#include "bitchat_client.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// 서비스/캐릭터리스틱 UUID (relay와 동일)
#define BCHAT_SERVICE_UUID "0000bc00-0000-1000-8000-00805f9b34fb"
#define BCHAT_TX_CHAR_UUID "0000bc01-0000-1000-8000-00805f9b34fb"
#define BCHAT_RX_CHAR_UUID "0000bc02-0000-1000-8000-00805f9b34fb"

BitchatClient* BitchatClient::_instance = nullptr;

// 싱글톤 스캔 콜백
class BchatScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (!BitchatClient::_instance) return;
        BitchatClient::_instance->on_scan_result(dev);
    }
};
static BchatScanCallbacks g_scan_callbacks;

// ============================================================
// 생성자/소멸자
// ============================================================
BitchatClient::BitchatClient()
    : _client(nullptr), _tx_remote(nullptr), _rx_remote(nullptr),
      _scanning(false), _best_device(nullptr),
      _counter(0),
      _chat_cb(nullptr), _peer_cb(nullptr),
      _raw_cb(nullptr), _log_cb(nullptr) {
    _instance = this;
    memset(_relay_addr, 0, sizeof(_relay_addr));
    memset(&_keypair, 0, sizeof(_keypair));
}

BitchatClient::~BitchatClient() {
    if (_client) {
        if (_client->isConnected()) _client->disconnect();
        NimBLEDevice::deleteClient(_client);
    }
    _instance = nullptr;
}

void BitchatClient::log(const char* fmt, ...) {
    if (!_log_cb) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    _log_cb(buf);
}

// ============================================================
// begin()
// ============================================================
bool BitchatClient::begin(const char* device_name) {
    // 자체 키쌍 초기화
    if (!bitchat_identity_init(&_keypair)) {
        log("ERROR: identity init failed");
        return false;
    }
    char id[13];
    bitchat_id_from_pk(_keypair.public_key, id);
    log("identity ready: node_id=%s", id);

    // NimBLE 초기화
    NimBLEDevice::init(device_name);
    NimBLEDevice::setMTU(247);
    return true;
}

// ============================================================
// scan + connect
// ============================================================
void BitchatClient::on_scan_result(NimBLEAdvertisedDevice* dev) {
    // 우리 서비스 UUID 광고하는 노드만
    if (!dev->isAdvertisingService(NimBLEUUID(BCHAT_SERVICE_UUID))) {
        return;
    }
    int rssi = dev->getRSSI();
    _instance->log("found relay: %s rssi=%d", std::string(dev->getAddress()).c_str(), rssi);

    // 가장 가까운 노드 선택 (RSSI 강한 것)
    if (!_instance->_best_device || rssi > _instance->_best_device->getRSSI()) {
        if (_instance->_best_device) delete _instance->_best_device;
        _instance->_best_device = new NimBLEAdvertisedDevice(*dev);
    }
}

bool BitchatClient::connect_to_relay(uint32_t timeout_ms) {
    if (is_connected()) {
        log("already connected");
        return true;
    }
    if (_best_device) { delete _best_device; _best_device = nullptr; }

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&g_scan_callbacks);
    scan->setInterval(100);
    scan->setWindow(50);
    scan->setActiveScan(true);

    uint32_t start = millis();
    _scanning = true;
    scan->start(0, nullptr, false);   // continuous, stop manually

    while (_scanning && (timeout_ms == 0 || millis() - start < timeout_ms)) {
        delay(100);
        if (_best_device) break;
    }
    scan->stop();
    _scanning = false;

    if (!_best_device) {
        log("no relay found");
        return false;
    }

    // relay에 연결
    _client = NimBLEDevice::createClient();
    NimBLEAddress addr = _best_device->getAddress();
    snprintf(_relay_addr, sizeof(_relay_addr), "%s",
             std::string(addr).c_str());
    delete _best_device;
    _best_device = nullptr;

    log("connecting to %s ...", _relay_addr);
    if (!_client->connect(addr)) {
        log("connect failed");
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
        return false;
    }

    NimBLERemoteService* svc = _client->getService(BCHAT_SERVICE_UUID);
    if (!svc) {
        log("service not found");
        _client->disconnect();
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
        return false;
    }

    _tx_remote = svc->getCharacteristic(BCHAT_TX_CHAR_UUID);
    _rx_remote = svc->getCharacteristic(BCHAT_RX_CHAR_UUID);
    if (!_tx_remote || !_rx_remote) {
        log("characteristics not found");
        _client->disconnect();
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
        return false;
    }

    // notify 구독 - relay에서 오는 패킷 수신
    if (_tx_remote->canNotify()) {
        _tx_remote->subscribe(true,
            [](NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
                if (!isNotify || !data || len == 0) return;
                if (BitchatClient::_instance) {
                    BitchatClient::_instance->handle_notify(data, len);
                }
            });
    } else {
        log("WARNING: tx char doesn't support notify");
    }

    log("connected to relay %s", _relay_addr);
    return true;
}

void BitchatClient::disconnect() {
    if (_client && _client->isConnected()) {
        _client->disconnect();
        log("disconnected");
    }
    if (_client) {
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
    }
    _tx_remote = nullptr;
    _rx_remote = nullptr;
}

bool BitchatClient::ensure_connected() {
    if (!is_connected()) {
        log("not connected");
        return false;
    }
    return true;
}

// ============================================================
// 송신 (서명 + counter)
// ============================================================
bool BitchatClient::send_signed_packet(bitchat_packet_builder_t* b) {
    if (!ensure_connected() || !b) return false;

    // 서명 메시지 계산 (magic..payload 직렬화)
    uint8_t sign_msg[BITCHAT_HEADER_LEN + BITCHAT_MAX_PAYLOAD_LEN];
    bitchat_packet_view_t v;
    if (!bitchat_parse(b->buffer, b->used, &v)) {
        log("parse built packet failed");
        return false;
    }
    size_t msg_len = 0;
    bitchat_signing_message(&v, sign_msg, &msg_len);

    // 서명 생성
    uint8_t sig[BITCHAT_SIG_LEN];
    if (!bitchat_sign(&_keypair, sign_msg, msg_len, sig, sizeof(sig))) {
        log("signing failed");
        return false;
    }

    // 빌더에 서명 채우기 (마지막 64B 자리)
    size_t sig_off = bitchat_builder_reserve_sig(b);
    if (sig_off == 0) return false;
    memcpy(b->buffer + sig_off, sig, BITCHAT_SIG_LEN);

    // BLE로 송신 (relay의 RX char에 write)
    std::string val(reinterpret_cast<const char*>(b->buffer), b->used);
    if (!_rx_remote->writeValue(val, true)) {
        log("BLE write failed");
        return false;
    }
    return true;
}

bool BitchatClient::send_packet(uint8_t type, uint8_t ttl,
                                 const uint8_t* payload, size_t len) {
    if (!ensure_connected()) return false;
    if (len > BITCHAT_MAX_PAYLOAD_LEN) {
        log("payload too large: %u > %u", (unsigned)len, BITCHAT_MAX_PAYLOAD_LEN);
        return false;
    }

    bitchat_packet_builder_t b;
    if (bitchat_build_header(&b, type, ttl, _keypair.public_key,
                              (uint32_t)(++_counter),  // counter 증가
                              payload, (uint16_t)len) == 0) {
        log("build_header failed");
        return false;
    }
    return send_signed_packet(&b);
}

bool BitchatClient::send_chat(const uint8_t* payload, size_t len) {
    log("send_chat: type=CHAT len=%u", (unsigned)len);
    return send_packet(BITCHAT_TYPE_CHAT, BITCHAT_MAX_TTL, payload, len);
}

bool BitchatClient::send_chat(const String& text) {
    return send_chat(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

bool BitchatClient::send_ack(const uint8_t* sender_pk, uint64_t counter) {
    // ACK payload: 원본 sender_pk(32B) + counter(8B)
    uint8_t ack_payload[40];
    memcpy(ack_payload, sender_pk, 32);
    for (int i = 0; i < 8; ++i) {
        ack_payload[32 + i] = (counter >> (8 * (7 - i))) & 0xFF;
    }
    log("send_ack: type=ACK for counter=%llu", (unsigned long long)counter);
    return send_packet(BITCHAT_TYPE_ACK, BITCHAT_MAX_TTL, ack_payload, sizeof(ack_payload));
}

bool BitchatClient::send_ping() {
    log("send_ping");
    return send_packet(BITCHAT_TYPE_PING, BITCHAT_MAX_TTL, nullptr, 0);
}

bool BitchatClient::send_peer_info(const String& nickname) {
    log("send_peer_info: nickname=%s", nickname.c_str());
    return send_packet(BITCHAT_TYPE_PEER_INFO, BITCHAT_MAX_TTL,
                       reinterpret_cast<const uint8_t*>(nickname.c_str()),
                       nickname.length());
}

// ============================================================
// 수신 처리
// ============================================================
void BitchatClient::handle_notify(const uint8_t* data, size_t len) {
    if (_raw_cb) _raw_cb(data, len);

    bitchat_packet_view_t v;
    if (!bitchat_parse(data, len, &v)) {
        log("received malformed packet, ignoring");
        return;
    }

    char sender_id[13];
    bitchat_id_from_pk(v.sender_pk, sender_id);

    // 서명 검증 (선택적 - relay가 이미 검증하지만, 클라이언트는 직접 검증 권장)
    uint8_t sign_msg[BITCHAT_HEADER_LEN + BITCHAT_MAX_PAYLOAD_LEN];
    size_t msg_len = 0;
    bitchat_signing_message(&v, sign_msg, &msg_len);
    if (!bitchat_verify(v.sender_pk, 32, sign_msg, msg_len, v.signature, 64)) {
        log("signature verification FAILED from %s", sender_id);
        return;
    }

    switch (v.type) {
        case BITCHAT_TYPE_CHAT:
            if (_chat_cb) {
                _chat_cb(sender_id, v.payload, v.payload_len);
            }
            // 자동 ACK 송신 (relay에게 도달 알림)
            send_ack(v.sender_pk, v.counter);
            break;
        case BITCHAT_TYPE_PEER_INFO:
            if (_peer_cb) {
                _peer_cb(sender_id);
            }
            break;
        case BITCHAT_TYPE_ACK:
        case BITCHAT_TYPE_PING:
        case BITCHAT_TYPE_LEAVE:
        case BITCHAT_TYPE_PEER_REQUEST:
        case BITCHAT_TYPE_KEY_EXCHANGE:
            // 저수준 패킷 - raw_callback으로 충분
            break;
        default:
            log("unknown packet type: 0x%02x", v.type);
    }
}

// ============================================================
// loop
// ============================================================
void BitchatClient::loop() {
    // NimBLE는 내부적으로 task를 돌림 - 여기서 특별히 할 일은 없음.
    // 필요하면 주기적 keep-alive (PING)을 보낼 수 있음.
    static uint32_t last_ping = 0;
    if (is_connected() && (millis() - last_ping > 60000)) {
        last_ping = millis();
        send_ping();   // 1분마다 keep-alive
    }
    delay(1);
}
