/* chat_terminal.ino - 대화형 채팅 터미널 (실제 bitchat 프로토콜) */
#include <Arduino.h>
#include "bitchat_client.h"
BitchatClient client;
String input_buffer;
String nickname = "user";
void on_log(const char* msg) { Serial.printf("[LOG] %s\n", msg); }
void on_chat(const char* sender, const uint8_t* payload, size_t len) {
    String text; for(size_t i=0;i<len;++i) if(payload[i]>=32&&payload[i]<127) text+=(char)payload[i];
    Serial.printf("\n[CHAT] %s: %s\n> ", sender, text.c_str());
}
void print_help() {
    Serial.println("\n=== commands ===");
    Serial.println("/nick <name>  set nickname + send ANNOUNCE");
    Serial.println("/stat         show status");
    Serial.println("/help         show this help");
    Serial.println("<text>        send message");
    Serial.println("================");
}
void setup() {
    Serial.begin(115200); delay(200);
    Serial.println("\n=== bitchat-client chat terminal ===");
    client.on_log(on_log); client.on_chat(on_chat);
    if(!client.begin("bchat-term")) { Serial.println("FATAL"); while(true){delay(1000);} }
    Serial.printf("Node ID: %s\n", client.get_node_id().c_str());
    Serial.println("Connecting...");
    if(client.connect_to_relay(15000)) {
        Serial.println("Connected!");
        client.send_announce(nickname);
    } else { Serial.println("No relay found."); }
    Serial.print("> ");
}
void loop() {
    client.loop();
    while(Serial.available()) {
        char c = Serial.read();
        if(c=='\r'||c=='\n') {
            if(input_buffer.length()>0) {
                Serial.println();
                if(input_buffer.startsWith("/nick ")) {
                    nickname = input_buffer.substring(6); nickname.trim();
                    Serial.printf("nickname: %s\n", nickname.c_str());
                    if(client.is_connected()) client.send_announce(nickname);
                } else if(input_buffer=="/stat") {
                    Serial.printf("node_id: %s\nconnected: %s\nnickname: %s\n",
                        client.get_node_id().c_str(),
                        client.is_connected()?"YES":"NO", nickname.c_str());
                } else if(input_buffer=="/help") { print_help(); }
                else if(client.is_connected()) {
                    if(client.send_message(input_buffer))
                        Serial.printf("[%s] %s\n", nickname.c_str(), input_buffer.c_str());
                    else Serial.println("send failed");
                } else { Serial.println("not connected"); }
                input_buffer=""; Serial.print("> ");
            }
        } else if(input_buffer.length()<256) { input_buffer+=c; Serial.write(c); }
    }
}
