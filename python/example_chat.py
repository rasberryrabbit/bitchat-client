#!/usr/bin/env python3
"""
example_chat.py - bitchat-client Python 예제

사용법:
    pip install bleak pynacl
    python example_chat.py

흐름:
    1. 자체 키쌍 초기화 (~/.bitchat/seed.bin에 저장)
    2. 근처 bitchat relay 검색 + 연결
    3. 닉네임 broadcast
    4. 표준 입력으로 메시지 입력 → relay로 전송
    5. relay에서 오는 메시지는 자동 수신 + 표시
    6. Ctrl+C 종료
"""

import asyncio
import sys

from bitchat_client import BitchatClient


async def main():
    client = BitchatClient()

    # 로그 출력
    client.on_log(lambda m: print(f"[LOG] {m}", file=sys.stderr))

    # 수신 채팅 처리
    def on_chat(sender_id: str, payload: bytes):
        try:
            text = payload.decode("utf-8")
        except UnicodeDecodeError:
            text = payload.hex()
        print(f"\n[CHAT] {sender_id}: {text}")
        print("> ", end="", flush=True)

    client.on_chat_message(on_chat)

    def on_peer(peer_id: str):
        print(f"\n[PEER] {peer_id} online")
        print("> ", end="", flush=True)

    client.on_peer_discovered(on_peer)

    # 초기화
    if not client.begin():
        print("FATAL: begin() failed", file=sys.stderr)
        return

    print(f"My node ID: {client.get_node_id()}")

    # relay에 연결
    print("Connecting to relay...", file=sys.stderr)
    if not await client.connect_to_relay(timeout=10):
        print("No relay found. Make sure bitchat-esp32-relay is running nearby.")
        return

    await client.send_peer_info("python")

    # 백그라운드에서 keep-alive loop 실행
    loop_task = asyncio.create_task(client.loop_forever())

    # 메인 루프 - stdin에서 읽어 relay로 전송
    print("Type messages + Enter to send. Ctrl+C to quit.")
    print("> ", end="", flush=True)

    loop = asyncio.get_event_loop()

    async def read_stdin():
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            line = line.rstrip()
            if not line:
                continue
            if line.startswith("/"):
                if line == "/ping":
                    await client.send_ping()
                elif line == "/stat":
                    print(f"node_id={client.get_node_id()} "
                          f"connected={client.is_connected()} "
                          f"counter={client.get_counter()}")
                elif line == "/quit":
                    break
                else:
                    print(f"unknown command: {line}")
            else:
                await client.send_chat(line)
            print("> ", end="", flush=True)

    try:
        await read_stdin()
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        loop_task.cancel()
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
