# bitchat-client Library v2.0.0

> 실제 bitchat 프로토콜 100% 호환 클라이언트 라이브러리

bitchat Android 앱 및 bitchat-esp32-relay 펌웨어와 완전 호환되는 클라이언트 라이브러리입니다. 실제 bitchat wire protocol (BinaryProtocol.kt 기반)을 사용하여 ANNOUNCE, MESSAGE, 서명 검증, PKCS#7 패딩까지 모두 구현합니다.

## v2.0.0 변경 사항 (전면 업데이트)

| 항목 | v1.0 (구버전) | v2.0 (현재) |
|------|-------------|-------------|
| Service UUID | `0000bc00-...` (가짜) | `f47b5e2d-...` (실제 bitchat) |
| Characteristic | TX/RX 2개 분리 | 단일 `a1b2c3d4-...` (write+notify) |
| 패킷 포맷 | magic 0xBC + 32B pk | version + 8B senderID + timestamp + flags |
| ANNOUNCE | 미구현 | TLV: nickname + X25519 + Ed25519 pk |
| 서명 | 헤더 직접 서명 | TTL=0 + PKCS#7 패딩(256B) + Ed25519 |
| senderID | 32B Ed25519 pk | 8B (pk 앞 8바이트) |
| relay 호환 | ❌ 불가 | ✅ 100% 호환 |
| bitchat 앱 호환 | ❌ 불가 | ✅ 100% 호환 |

## 빠른 시작

### ESP32 Arduino

```cpp
#include "bitchat_client.h"

BitchatClient client;

void on_chat(const char* sender, const uint8_t* payload, size_t len) {
    // 수신 메시지 처리
}

void setup() {
    Serial.begin(115200);
    client.on_chat(on_chat);
    client.begin("bchat-cli");
    client.connect_to_relay(10000);
    client.send_announce("my-nickname");  // relay가 pk 추출
    client.send_message("hello!");       // 서명 + 패딩 자동 처리
}

void loop() {
    client.loop();
}
```

### Python

```bash
pip install bleak pynacl
python example_chat.py
```

## 핵심 API

| 메서드 | 설명 |
|--------|------|
| `begin(name)` | Ed25519 키쌍 초기화 (NVS 영속화) |
| `connect_to_relay(timeout)` | BLE scan → relay 연결 (RSSI 강한 노드) |
| `send_announce(nickname)` | ANNOUNCE 전송 (TLV: nickname + Ed25519 pk, 서명 포함) |
| `send_message(text)` | MESSAGE 전송 (broadcast, 서명 + PKCS#7 패딩) |
| `on_chat(cb)` | 수신 MESSAGE 콜백 |
| `get_node_id()` | 8B senderID hex 문자열 |

## 파일 구조

```
bitchat-client/
├── library.properties / library.json
├── src/
│   ├── bitchat_client.h/.cpp    # 고수준 API (BLE + 프로토콜)
│   ├── bitchat_packet.h/.cpp    # 패킷 빌더/파서 + ANNOUNCE TLV + PKCS#7
│   ├── bitchat_crypto.h/.cpp    # Ed25519 (libsodium)
│   └── bitchat_identity.h/.cpp  # 키쌍 + 8B senderID + NVS
├── examples/
│   ├── send_message/send_message.ino
│   └── chat_terminal/chat_terminal.ino
└── python/
    ├── bitchat_client.py        # Python 클라이언트 (bleak + pynacl)
    ├── example_chat.py
    └── requirements.txt
```

## 동작 흐름

```
[클라이언트]                    [ESP32 relay]
    │                                │
    ├── ANNOUNCE (TLV + 서명) ───→  │ → type 0x03에서 Ed25519 pk 추출
    │   nickname + X25519 + pk       │ → peer_keys에 매핑 저장
    │   PKCS#7 패딩 + Ed25519 서명   │ → 서명 검증 ✓
    │                                │
    ├── MESSAGE (서명 + 패딩) ────→  │ → peer_keys에서 pk 조회
    │   broadcast (0xFF×8)          │ → 서명 검증 ✓
    │   PKCS#7 패딩 + Ed25519 서명   │ → TTL 감소 → forward
    │                                │
    ←── notify (relay가 forward) ───│
```

## 라이선스
MIT License
