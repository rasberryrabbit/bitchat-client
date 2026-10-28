# bitchat-client Library

> 다른 클라이언트에서 bitchat을 간단하게 사용하기 위한 별도 라이브러리

bitchat-esp32-relay 펌웨어가 올라간 ESP32 보드(또는 다른 bitchat 노드)에 연결하여 메시지를 송수신하는 고수준 API를 제공합니다. Ed25519 서명, 패킷 빌드, BLE 연결 관리를 모두 라이브러리가 처리하므로 사용자는 `send_chat(text)` 한 줄로 통신할 수 있습니다.

**두 가지 언어 지원**:
- **C++ (Arduino)** - ESP32 클래식/C3/S3 보드에서 사용
- **Python** - 데스크톱/모바일/Linux 클라이언트에서 사용

---

## 빠른 시작

### ESP32 Arduino (C++)

```cpp
#include "bitchat_client.h"

BitchatClient client;

void on_chat(const char* sender_id, const uint8_t* payload, size_t len) {
    String text((const char*)payload, len);
    Serial.printf("[CHAT] %s: %s\n", sender_id, text.c_str());
}

void setup() {
    Serial.begin(115200);
    client.on_chat_message(on_chat);
    client.begin("bchat-cli");
    client.connect_to_relay(10000);   // 10초 내 relay 검색 + 연결
}

void loop() {
    client.loop();
    if (Serial.available()) {
        String msg = Serial.readStringUntil('\n');
        client.send_chat(msg);
    }
}
```

### Python

```bash
pip install bleak pynacl
python example_chat.py
```

```python
import asyncio
from bitchat_client import BitchatClient

async def main():
    client = BitchatClient()
    client.begin()                                # Ed25519 키쌍 로드/생성
    client.on_chat_message(lambda s, p: print(f"[{s}] {p.decode()}"))
    await client.connect_to_relay(timeout=10)     # BLE scan + connect
    await client.send_chat("hello from python!")
    await client.loop_forever()                   # 수신 대기

asyncio.run(main())
```

---

## 핵심 API (C++)

### 초기화 + 연결

| 메서드 | 설명 |
|--------|------|
| `bool begin(name)` | 자체 Ed25519 키쌍 초기화 (NVS 영속화), NimBLE BLE init |
| `bool connect_to_relay(timeout_ms)` | 주변 bitchat relay 스캔 후 가장 가까운 노드(RSSI 강한)에 연결 |
| `void disconnect()` | relay 연결 해제 |
| `bool is_connected()` | 연결 상태 |

### 송신 (고수준)

| 메서드 | 패킷 타입 | 설명 |
|--------|-----------|------|
| `bool send_chat(text)` | CHAT (0x01) | ASCII 텍스트 메시지 |
| `bool send_chat(payload, len)` | CHAT (0x01) | 임의 바이너리 payload (암호화된 blob 등) |
| `bool send_ack(sender_pk, counter)` | ACK (0x02) | 수신 패킷에 대한 ACK 응답 |
| `bool send_ping()` | PING (0x07) | keep-alive, 1분마다 자동 송신 |
| `bool send_peer_info(nickname)` | PEER_INFO (0x03) | 닉네임 broadcast |
| `bool send_packet(type, ttl, payload, len)` | (모든) | 저수준 원시 패킷 |

### 수신 콜백 등록

| 메서드 | 콜백 시그니처 | 트리거 |
|--------|---------------|--------|
| `on_chat_message(cb)` | `cb(sender_id, payload, len)` | CHAT 패킷 수신 시 (자동 ACK 송신) |
| `on_peer_discovered(cb)` | `cb(peer_id)` | PEER_INFO 패킷 수신 시 |
| `on_raw_packet(cb)` | `cb(data, len)` | 모든 패킷 (디버그용) |
| `on_log(cb)` | `cb(message)` | 라이브러리 내부 로그 |

### 정보 조회

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `get_node_id()` | `String` | 자체 노드 ID (pk 앞 6B hex) |
| `get_public_key()` | `const uint8_t*` | 32B Ed25519 공개키 |
| `get_counter()` | `uint64_t` | 송신 counter (패킷당 1 증가) |
| `get_connected_relay()` | `const char*` | "aa:bb:cc:dd:ee:ff" 또는 nullptr |

### 루프

```cpp
void loop() {
    client.loop();   // BLE 이벤트 처리 + 1분마다 자동 PING
}
```

---

## Python API (대칭)

| C++ | Python |
|-----|--------|
| `begin()` | `await begin()` (async) |
| `connect_to_relay(timeout_ms)` | `await connect_to_relay(timeout=10)` |
| `send_chat(text)` | `await send_chat(text)` |
| `send_ack(pk, counter)` | `await send_ack(pk, counter)` |
| `send_ping()` | `await send_ping()` |
| `on_chat_message(cb)` | `on_chat_message(cb)` (cb는 sync) |
| `loop()` | `await loop_forever()` |

Python에서는 모든 BLE 작업이 `async`입니다. 콜백 자체는 sync 함수이지만, 내부적으로 asyncio event loop에서 처리됩니다.

---

## 설치 방법

### Arduino IDE

1. `bitchat-client/` 폴더를 `~/Documents/Arduino/libraries/` 또는 스케치 폴더 안의 `libraries/`로 복사
2. Arduino IDE를 다시 실행
3. 의존성: **NimBLE-Arduino** 라이브러리 설치 (Library Manager에서 검색)

### PlatformIO (권장)

`platformio.ini`에 추가:

```ini
lib_deps =
    symlink://path/to/bitchat-client
    h2zero/NimBLE-Arduino @ ^1.4.2
```

또는 git에서 직접:

```ini
lib_deps =
    https://github.com/dim069/bitchat.git#bitchat-client
    h2zero/NimBLE-Arduino @ ^1.4.2
```

### Python

```bash
cd bitchat-client/python
pip install -r requirements.txt
python example_chat.py
```

자체 키쌍은 `~/.bitchat/seed.bin`에 자동 저장됩니다.

---

## 파일 구조

```
bitchat-client/
├── library.properties             # Arduino Library Manager 메타데이터
├── library.json                   # PlatformIO 메타데이터
├── README.md                      # 이 파일
├── keywords.txt                   # Arduino IDE syntax highlighting
├── src/                           # C++ 라이브러리 소스
│   ├── bitchat_client.h           # 고수준 API (BitchatClient 클래스)
│   ├── bitchat_client.cpp         # BLE scan + connect + 송수신 구현
│   ├── bitchat_packet.h/.cpp      # 패킷 빌더/파서 (relay와 동일 프로토콜)
│   ├── bitchat_crypto.h/.cpp      # Ed25519 서명/검증 (libsodium)
│   └── bitchat_identity.h/.cpp    # 자체 키쌍 NVS 영속화
├── examples/                      # Arduino 예제 스케치
│   ├── send_message/send_message.ino       # 단방향 메시지 송신
│   └── chat_terminal/chat_terminal.ino     # 대화형 채팅 터미널
└── python/                        # Python 참조 구현
    ├── bitchat_client.py          # asyncio + bleak + pynacl
    ├── example_chat.py            # CLI 클라이언트 예제
    └── requirements.txt           # bleak + pynacl
```

---

## 프로토콜 호환성

본 라이브러리는 `bitchat-esp32-relay` 펌웨어와 **동일한 wire protocol**을 사용합니다:

- **Magic**: `0xBC`
- **Version**: `0x01`
- **Header**: magic + ver + type + ttl + sender_pk(32) + counter(8 BE) + payload_len(2 BE)
- **Payload**: 최대 512 bytes
- **Signature**: Ed25519 64 bytes over (header + payload)
- **TTL**: 최대 7 (클라이언트는 7로 송신, relay가 감소시키며 forward)
- **Dedup**: 30초 LRU 캐시 (relay에서 처리, 클라이언트는 무관)

UUIDs (BLE Service/Characteristic):
- Service: `0000bc00-0000-1000-8000-00805f9b34fb`
- TX char (relay → client, notify): `0000bc01-...`
- RX char (client → relay, write): `0000bc02-...`

---

## 보안 고려사항

- **Ed25519 키쌍 영속화**: C++은 NVS blob, Python은 `~/.bitchat/seed.bin`
- **서명 검증**: 클라이언트도 수신 패킷 서명 검증 (relay가 이미 검증하지만 이중 확인)
- **NVS 평문 저장**: 칩 접근 시 변조 가능 → 고신뢰 환경은 NVS Encryption (`CONFIG_NVS_ENCRYPTION=y`) 활성화
- **Python seed.bin**: 파일 시스템 권한으로 보호 (chmod 600 권장)

---

## 한계 및 향후 확장

- **단일 relay 연결**: 현재는 한 번에 하나의 relay에만 연결. 멀티홉 자동 전환은 미구현
- **자동 재연결**: 연결 끊김 시 자동 재스캔+재연결 로직 미구현 (사용자가 구현 가능)
- **메시지 암호화**: CHAT 패킷의 payload는 평문 (Ed25519 서명만). 종단간 암호화는 사용자가 별도 구현 (ECDH + AES-GCM)
- **큰 메시지**: payload 512B 제한. 더 큰 메시지는 분할 필요 (BLE MTU 한계 247B NimBLE 기본)
- **Python bleak OS 호환성**: Linux/macOS/Windows 모두 지원, BLE 라디오 필요

향후 확장 아이디어:
- 자동 재연결 + connection migration
- 종단간 암호화 (ECDH 키 교환 + AES-GCM payload)
- BLE 5.0 extended advertising (C3/S3 보드)
- 파일 전송 (청크 분할)
- 그룹 채팅 채널 ID 지원

---

## 라이선스

MIT License - 자유롭게 사용/수정/배포 가능.

## 참고 자료

- [bitchat-esp32-relay firmware](https://github.com/dim069/bitchat) - 메시 relay 노드 펌웨어
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) - BLE 라이브러리
- [libsodium](https://libsodium.gitbook.io/) - Ed25519 + crypto primitives
- [bleak](https://github.com/hbldh/bleak) - Python BLE 클라이언트
- [pynacl](https://pynacl.readthedocs.io/) - Python libsodium 바인딩
