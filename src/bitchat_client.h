/*
 * bitchat_client.h - High-level bitchat client API
 *
 * 한국어:
 *   bitchat relay 노드(또는 다른 bitchat 클라이언트)에 연결하여
 *   메시지를 송수신하는 간단한 API.
 *
 *   사용 흐름:
 *     1) begin() - 자체 키쌍 초기화
 *     2) connect_to_relay() - BLE scan 후 가장 가까운 relay에 연결
 *     3) send_chat(text) - 채팅 메시지 전송
 *     4) on_chat_message(callback) - 수신 메시지 콜백
 *     5) loop() - 주기적 호출 (BLE 이벤트 처리)
 *
 *   의존성:
 *     - NimBLE-Arduino 라이브러리 (BLE 스텍)
 *     - libsodium (Ed25519 - arduino-esp32 내장)
 *
 *   예제는 examples/ 폴더 참조.
 */

#pragma once

#include <Arduino.h>
#include "NimBLEDevice.h"
#include "bitchat_packet.h"
#include "bitchat_crypto.h"
#include "bitchat_identity.h"

// 콜백 함수 타입 정의
typedef void (*bitchat_chat_callback)(const char* sender_id, const uint8_t* payload, size_t len);
typedef void (*bitchat_peer_callback)(const char* peer_id);
typedef void (*bitchat_raw_callback)(const uint8_t* data, size_t len);
typedef void (*bitchat_log_callback)(const char* message);

class BitchatClient {
public:
    BitchatClient();
    ~BitchatClient();

    // ============================================================
    // 1. 초기화
    // ============================================================

    // 자체 키쌍 초기화 (NVS에서 복원 또는 새로 생성)
    // 다른 begin 전에 호출
    bool begin(const char* device_name = "bchat-cli");

    // ============================================================
    // 2. 연결 관리
    // ============================================================

    // 주변 bitchat relay 노드 스캔 후 가장 가까운(RSSI 강한) 노드에 연결
    // timeout_ms: 0 = 무한 대기 (권장 안 함), ms = 제한 시간
    bool connect_to_relay(uint32_t timeout_ms = 10000);

    // 현재 연결된 relay 정보 (없으면 nullptr)
    const char* get_connected_relay() const { return _relay_addr; }

    // 연결 해제
    void disconnect();

    bool is_connected() const { return _client && _client->isConnected(); }

    // ============================================================
    // 3. 메시지 송신 (고수준 API)
    // ============================================================

    // 채팅 메시지 송신 - payload는 자유 형식 (보통 암호화된 텍스트 또는 JSON)
    // 자동으로 Ed25519 서명 + counter 증가 + 패킷 빌드
    // channel: 빈 문자열이면 기본 채널, 닉네임/그룹명 가능 (선택)
    bool send_chat(const uint8_t* payload, size_t len);
    bool send_chat(const String& text);   // ASCII 텍스트 간편 버전

    // ACK 송신 (수신 패킷에 대한 응답)
    // sender_pk + counter = 원본 패킷 식별자
    bool send_ack(const uint8_t* sender_pk, uint64_t counter);

    // PING 송신 (keep-alive, 연결 상태 확인)
    bool send_ping();

    // PEER_INFO 송신 - 닉네임 broadcast (다른 노드에게 알림)
    bool send_peer_info(const String& nickname);

    // 원시 패킷 송신 (저수준)
    bool send_packet(uint8_t type, uint8_t ttl,
                     const uint8_t* payload, size_t len);

    // ============================================================
    // 4. 콜백 등록 (수신 처리)
    // ============================================================

    // 채팅 메시지 수신 콜백
    // callback(sender_id, payload, len) - sender_id는 12 hex chars
    void on_chat_message(bitchat_chat_callback callback) {
        _chat_cb = callback;
    }

    // 새 피어 발견 콜백
    void on_peer_discovered(bitchat_peer_callback callback) {
        _peer_cb = callback;
    }

    // 원시 패킷 수신 콜백 (디버그용, 모든 패킷)
    void on_raw_packet(bitchat_raw_callback callback) {
        _raw_cb = callback;
    }

    // 로그 콜백 (라이브러리 내부 로그를 사용자 코드로)
    void on_log(bitchat_log_callback callback) {
        _log_cb = callback;
    }

    // ============================================================
    // 5. 메인 루프
    // ============================================================

    // Arduino loop()에서 주기적 호출
    // BLE 이벤트 처리 + 수신 패킷 dispatch (콜백 호출)
    void loop();

    // ============================================================
    // 6. 정보 조회
    // ============================================================

    // 자체 공개키 (32B)
    const uint8_t* get_public_key() const {
        return _keypair.public_key;
    }

    // 자체 노드 ID (12 hex chars, pk 앞 6B)
    String get_node_id() const {
        char id[13];
        bitchat_id_from_pk(_keypair.public_key, id);
        return String(id);
    }

    // 송신 counter (자체 메시지당 1씩 증가)
    uint64_t get_counter() const { return _counter; }

private:
    NimBLEClient* _client;
    NimBLERemoteCharacteristic* _tx_remote;   // notify 받는 char (relay → us)
    NimBLERemoteCharacteristic* _rx_remote;   // write 보내는 char (us → relay)
    bool _scanning;
    char _relay_addr[32];                      // "aa:bb:cc:dd:ee:ff"
    NimBLEAdvertisedDevice* _best_device;     // 스캔에서 가장 가까운 노드

    // 자체 ID
    bitchat_keypair_t _keypair;
    uint64_t _counter;

    // 콜백
    bitchat_chat_callback _chat_cb;
    bitchat_peer_callback _peer_cb;
    bitchat_raw_callback  _raw_cb;
    bitchat_log_callback  _log_cb;

    // 내부
    void log(const char* fmt, ...);
    bool ensure_connected();
    bool send_signed_packet(bitchat_packet_builder_t* b);
    void handle_notify(const uint8_t* data, size_t len);

    // 스캔 콜백 (public - NimBLEAdvertisedDeviceCallbacks에서 접근)
public:
    void on_scan_result(NimBLEAdvertisedDevice* dev);
    static BitchatClient* _instance;
};
