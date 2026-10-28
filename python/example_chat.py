#!/usr/bin/env python3
"""example_chat.py - 실제 bitchat 프로토콜 Python 채팅 클라이언트"""
import asyncio, sys
from bitchat_client import BitchatClient

async def main():
    client = BitchatClient()
    client.on_log(lambda m: print(f"[LOG] {m}", file=sys.stderr, flush=True))
    def on_chat(sender, text):
        print(f"\n[CHAT] {sender}: {text}\n> ", end="", flush=True)
    client.on_chat(on_chat)
    if not client.begin(): print("FATAL", file=sys.stderr); return
    print(f"Node ID: {client.get_node_id()}")
    print("Connecting...", file=sys.stderr)
    if not await client.connect_to_relay(10): return
    await client.send_announce("python")
    print("Type messages + Enter. Ctrl+C to quit.\n> ", flush=True)
    loop = asyncio.get_event_loop()
    async def read_stdin():
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line: break
            line = line.rstrip()
            if line: await client.send_message(line)
            print("> ", end="", flush=True)
    try: await read_stdin()
    except KeyboardInterrupt: print("\nExiting...")
    finally: await client.disconnect()

if __name__ == "__main__":
    asyncio.run(main())
