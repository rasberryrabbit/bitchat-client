/*
 * send_message.ino - bitchat-client 라이브러리 예제 1
 *
 * 한국어:
 *   가장 간단한 예제 - bitchat relay에 연결 후 5초마다 메시지 송신.
 *   Serial Monitor로 진행 상황 확인.
 *
 *   회로:
 *     - ESP32 (classic/C3/S3) 보드 + USB serial
 *     - bitchat-esp32-relay 펌웨어가 올라간 다른 ESP32 보드가 근처에 있어야 함
 *
 *   시리얼 모니터:
 *     baud 115200
 *     "Enter message: " 표시되면 텍스트 입력 + Enter
 */

#include <Arduino.h>
#include "bitchat_client.h"

BitchatClient client;

String input_buffer;

void on_log(const char* msg) {
    Serial.print("[bchat] ");
    Serial.println(msg);
}

void on_chat(const char* sender_id, const uint8_t* payload, size_t len) {
    // payload를 문자열로 가정 (실제 구현에서는 암호화된 blob일 수 있음)
    String text;
    text.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (payload[i] >= 32 && payload[i] < 127) {
            text += (char)payload[i];
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "\\x%02x", payload[i]);
            text += hex;
        }
    }
    Serial.printf("\n[RECV from %s] %s\n> ", sender_id, text.c_str());
}

void on_peer(const char* peer_id) {
    Serial.printf("\n[PEER] discovered: %s\n> ", peer_id);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== bitchat-client send_message example ===");

    // 콜백 등록
    client.on_log(on_log);
    client.on_chat_message(on_chat);
    client.on_peer_discovered(on_peer);

    // 자체 키쌍 초기화 + BLE init
    if (!client.begin("bchat-send")) {
        Serial.println("FATAL: begin() failed");
        while (true) { delay(1000); }
    }

    Serial.printf("My node ID: %s\n", client.get_node_id().c_str());

    // relay 노드에 연결 (10초 타임아웃)
    Serial.println("Connecting to bitchat relay...");
    if (!client.connect_to_relay(10000)) {
        Serial.println("FATAL: no relay found in 10s. "
                       "Make sure bitchat-esp32-relay is running nearby.");
        while (true) { delay(1000); }
    }

    Serial.println("Connected! Type a message + Enter to send.");
    Serial.print("> ");
}

void loop() {
    client.loop();

    // 시리얼 입력 처리
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (input_buffer.length() > 0) {
                Serial.println();
                Serial.printf("Sending: %s\n", input_buffer.c_str());
                if (client.send_chat(input_buffer)) {
                    Serial.println("OK");
                } else {
                    Serial.println("FAIL");
                }
                input_buffer = "";
                Serial.print("> ");
            }
        } else if (input_buffer.length() < 200) {
            input_buffer += c;
            Serial.write(c);   // echo
        }
    }
}
