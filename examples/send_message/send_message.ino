/*
 * send_message.ino - bitchat-client 라이브러리 예제 (실제 bitchat 프로토콜)
 *
 * 흐름:
 *   1. 자체 Ed25519 키쌍 초기화 (NVS 영속화)
 *   2. bitchat relay에 BLE 연결 (Service UUID f47b5e2d-...)
 *   3. ANNOUNCE 전송 (nickname + Ed25519 pk, 서명 포함)
 *   4. Serial 입력 → MESSAGE 전송 (서명 + PKCS#7 패딩)
 *   5. relay에서 notify로 오는 패킷 수신
 */
#include <Arduino.h>
#include "bitchat_client.h"

BitchatClient client;
String input_buffer;

void on_log(const char* msg) {
    Serial.print("[bchat] "); Serial.println(msg);
}
void on_chat(const char* sender, const uint8_t* payload, size_t len) {
    String text;
    for (size_t i = 0; i < len; ++i) {
        if (payload[i] >= 32 && payload[i] < 127) text += (char)payload[i];
    }
    Serial.printf("\n[RECV from %s] %s\n> ", sender, text.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== bitchat-client (real protocol) ===");

    client.on_log(on_log);
    client.on_chat(on_chat);

    if (!client.begin("bchat-cli")) {
        Serial.println("FATAL: begin() failed");
        while (true) { delay(1000); }
    }
    Serial.printf("My node ID: %s\n", client.get_node_id().c_str());

    Serial.println("Connecting to relay...");
    if (!client.connect_to_relay(10000)) {
        Serial.println("No relay found. Make sure bitchat-esp32-relay is running.");
        while (true) { delay(1000); }
    }

    // ANNOUNCE 전송 (relay가 Ed25519 pk 추출하여 서명 검증에 사용)
    client.send_announce("arduino-cli");
    Serial.println("ANNOUNCE sent. Type message + Enter to send.");
    Serial.print("> ");
}

void loop() {
    client.loop();
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (input_buffer.length() > 0) {
                Serial.println();
                Serial.printf("Sending: %s\n", input_buffer.c_str());
                if (client.send_message(input_buffer)) Serial.println("OK");
                else Serial.println("FAIL");
                input_buffer = "";
                Serial.print("> ");
            }
        } else if (input_buffer.length() < 200) {
            input_buffer += c;
            Serial.write(c);
        }
    }
}
