/* bitchat_client.h - 실제 bitchat 프로토콜 클라이언트 (고수준 API) */
#pragma once
#include <Arduino.h>
#include "NimBLEDevice.h"
#include "bitchat_packet.h"
#include "bitchat_crypto.h"
#include "bitchat_identity.h"

typedef void (*bitchat_chat_cb)(const char* sender_id, const uint8_t* payload, size_t len);
typedef void (*bitchat_log_cb)(const char* msg);

class BitchatClient {
public:
    BitchatClient();
    ~BitchatClient();
    bool begin(const char* device_name = "bitchat-cli");
    bool connect_to_relay(uint32_t timeout_ms = 10000);
    void disconnect();
    bool is_connected() const { return _client && _client->isConnected(); }
    // ANNOUNCE 송신 (nickname + Ed25519 pk)
    bool send_announce(const String& nickname);
    // MESSAGE 송신 (Ed25519 서명 + PKCS#7 패딩)
    bool send_message(const uint8_t* payload, size_t len);
    bool send_message(const String& text);
    // 수신 콜백
    void on_chat(bitchat_chat_cb cb) { _chat_cb = cb; }
    void on_log(bitchat_log_cb cb) { _log_cb = cb; }
    void loop();
    String get_node_id() const { char id[17]; bitchat_id_hex(_sender_id, id); return String(id); }
private:
    NimBLEClient* _client;
    NimBLERemoteCharacteristic* _remote_char;
    bitchat_keypair_t _kp;
    uint8_t _sender_id[8];
    uint64_t _msg_counter;
    bitchat_chat_cb _chat_cb;
    bitchat_log_cb _log_cb;
    void log(const char* fmt, ...);
    void handle_notify(const uint8_t* data, size_t len);
    static BitchatClient* _instance;
};
