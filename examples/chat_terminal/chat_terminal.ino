/*
 * chat_terminal.ino - bitchat-client 라이브러리 예제 2
 *
 * 한국어:
 *   대화형 채팅 터미널 - relay에 연결 후 양방향 메시지 송수신.
 *   명령어:
 *     /nick <nickname>   - 닉네임 설정 (PEER_INFO broadcast)
 *     /ping              - PING 송신 (연결 확인)
 *     /stat              - 상태 출력 (연결 여부, counter, 노드 ID)
 *     /quit              - relay 연결 해제
 *     /help              - 도움말
 *     <아무 텍스트>       - 채팅 메시지 전송
 *
 *   시리얼 모니터:
 *     baud 115200, Newline (LF) 또은 CR+LF
 */

#include <Arduino.h>
#include "bitchat_client.h"

BitchatClient client;
String input_buffer;
String nickname = "user";

void on_log(const char* msg) {
    Serial.printf("[LOG] %s\n", msg);
}

void on_chat(const char* sender_id, const uint8_t* payload, size_t len) {
    // payload는 ASCII 텍스트로 가정
    String text;
    text.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (payload[i] >= 32 && payload[i] < 127) {
            text += (char)payload[i];
        } else {
            char hex[6];
            snprintf(hex, sizeof(hex), "<%02x>", payload[i]);
            text += hex;
        }
    }
    Serial.printf("\n[CHAT] %s: %s\n> ", sender_id, text.c_str());
}

void on_peer(const char* peer_id) {
    Serial.printf("\n[PEER] %s joined\n> ", peer_id);
}

void on_raw(const uint8_t* data, size_t len) {
    // 모든 패킷을 DEBUG로 찍고 싶으면 아래 주석 해제
    // Serial.printf("[RAW] %u bytes\n", (unsigned)len);
    (void)data; (void)len;
}

void print_help() {
    Serial.println();
    Serial.println("=== commands ===");
    Serial.println("/nick <name>  set nickname + broadcast PEER_INFO");
    Serial.println("/ping         send PING");
    Serial.println("/stat         show status");
    Serial.println("/quit         disconnect from relay");
    Serial.println("/help         show this help");
    Serial.println("<text>        send chat message");
    Serial.println("================");
}

void handle_command(const String& cmd) {
    if (cmd.startsWith("/nick ")) {
        nickname = cmd.substring(6);
        nickname.trim();
        Serial.printf("set nickname: %s\n", nickname.c_str());
        if (client.is_connected()) {
            client.send_peer_info(nickname);
        }
    } else if (cmd == "/ping") {
        Serial.println(client.send_ping() ? "ping sent" : "ping failed");
    } else if (cmd == "/stat") {
        Serial.println("=== status ===");
        Serial.printf("node_id:   %s\n", client.get_node_id().c_str());
        Serial.printf("connected: %s\n",
                      client.is_connected() ? "YES" : "NO");
        Serial.printf("relay:     %s\n", client.get_connected_relay());
        Serial.printf("counter:   %llu\n",
                      (unsigned long long)client.get_counter());
        Serial.printf("nickname:  %s\n", nickname.c_str());
        Serial.println("===============");
    } else if (cmd == "/quit") {
        client.disconnect();
        Serial.println("disconnected");
    } else if (cmd == "/help") {
        print_help();
    } else if (cmd.length() > 0) {
        // 일반 텍스트 = 채팅
        if (!client.is_connected()) {
            Serial.println("not connected - use /stat");
            return;
        }
        if (client.send_chat(cmd)) {
            Serial.printf("[%s] %s\n", nickname.c_str(), cmd.c_str());
        } else {
            Serial.println("send failed");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== bitchat-client chat terminal ===");
    Serial.println();

    client.on_log(on_log);
    client.on_chat_message(on_chat);
    client.on_peer_discovered(on_peer);
    client.on_raw(on_raw);

    if (!client.begin("bchat-term")) {
        Serial.println("FATAL: begin() failed");
        while (true) { delay(1000); }
    }

    Serial.printf("My node ID: %s\n", client.get_node_id().c_str());

    // 자동 연결 시도
    Serial.println("Connecting to relay...");
    if (client.connect_to_relay(15000)) {
        Serial.println("OK - type /help for commands");
        // 닉네임 broadcast
        client.send_peer_info(nickname);
    } else {
        Serial.println("No relay found. Will retry. Type /stat to check.");
    }
    Serial.print("> ");
}

void loop() {
    client.loop();

    // 시리얼 입력
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (input_buffer.length() > 0) {
                Serial.println();
                handle_command(input_buffer);
                input_buffer = "";
                Serial.print("> ");
            }
        } else if (input_buffer.length() < 256) {
            input_buffer += c;
            Serial.write(c);
        }
    }
}
