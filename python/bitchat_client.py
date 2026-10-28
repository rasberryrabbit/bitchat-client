#!/usr/bin/env python3
"""
bitchat_client.py - 실제 bitchat 프로토콜 Python 클라이언트 (전면 업데이트)

BinaryProtocol.kt 호환: version byte + 8B senderID + timestamp + flags + PKCS#7 서명.
relay 펌웨어 및 bitchat Android 앱과 100% 호환.
"""
import asyncio, os, struct, time
from pathlib import Path
from typing import Callable, Optional

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    raise ImportError("pip install bleak")
try:
    from nacl.signing import SigningKey, VerifyKey
    from nacl.exceptions import BadSignatureError
except ImportError:
    raise ImportError("pip install pynacl")

# Constants (must match relay firmware)
BITCHAT_VERSION_1 = 1
BITCHAT_HEADER_V1_LEN = 14
BITCHAT_SENDER_ID_SIZE = 8
BITCHAT_SIG_LEN = 64
BITCHAT_MAX_PAYLOAD = 650
BITCHAT_MAX_PKT_LEN = BITCHAT_HEADER_V1_LEN + BITCHAT_SENDER_ID_SIZE + 8 + BITCHAT_MAX_PAYLOAD + BITCHAT_SIG_LEN + 64
BITCHAT_FLAG_HAS_RECIPIENT = 0x01
BITCHAT_FLAG_HAS_SIGNATURE = 0x02
BITCHAT_MSG_ANNOUNCE = 0x01
BITCHAT_MSG_MESSAGE = 0x02
BITCHAT_MSG_LEAVE = 0x03
BITCHAT_BROADCAST_ID = b'\xff' * 8
BITCHAT_BLE_SERVICE_UUID = "f47b5e2d-4a9e-4c5a-9b3f-8e1d2c3a4b5c"
BITCHAT_BLE_CHAR_UUID = "a1b2c3d4-e5f6-4a5b-8c9d-0e1f2a3b4c5d"
BLOCK_SIZES = [256, 512, 1024, 2048]


def build_packet(pkt_type, ttl, sender_id, timestamp, payload, recipient_id=None, has_sig=True):
    flags = 0
    if recipient_id: flags |= BITCHAT_FLAG_HAS_RECIPIENT
    if has_sig: flags |= BITCHAT_FLAG_HAS_SIGNATURE
    header = struct.pack(">BBBQBH", BITCHAT_VERSION_1, pkt_type, ttl, timestamp, flags, len(payload))
    data = header + sender_id
    if recipient_id: data += recipient_id
    data += payload
    if has_sig: data += b'\x00' * 64
    return data


def compute_signing_message(pkt_data):
    """TTL=0, HAS_SIGNATURE cleared, PKCS#7 padding."""
    # Find sig offset (last 64 bytes if HAS_SIGNATURE)
    flags = pkt_data[11]
    has_sig = (flags & BITCHAT_FLAG_HAS_SIGNATURE) != 0
    sig_off = len(pkt_data)
    if has_sig: sig_off -= BITCHAT_SIG_LEN
    msg = bytearray(pkt_data[:sig_off])
    msg[2] = 0  # TTL = 0
    msg[11] &= ~BITCHAT_FLAG_HAS_SIGNATURE  # Clear HAS_SIGNATURE
    # PKCS#7 padding
    total_size = len(msg) + 16
    target = len(msg)
    for bs in BLOCK_SIZES:
        if total_size <= bs: target = bs; break
    pad_needed = target - len(msg)
    if 0 < pad_needed <= 255:
        msg += bytes([pad_needed]) * pad_needed
    return bytes(msg)


def build_announce_payload(nickname, ed25519_pk):
    """TLV: nickname(type 1) + dummy X25519(type 2) + Ed25519 signing pk(type 3)."""
    nick = nickname.encode('utf-8')[:255]
    payload = struct.pack(">BB", 1, len(nick)) + nick  # type 1: nickname
    payload += struct.pack(">BB", 2, 32) + b'\x00' * 32  # type 2: dummy X25519
    payload += struct.pack(">BB", 3, 32) + ed25519_pk    # type 3: Ed25519 signing key
    payload += struct.pack(">BBBB", 5, 2, 0, 1)          # type 5: version
    return payload


def parse_packet(buf):
    if len(buf) < BITCHAT_HEADER_V1_LEN + BITCHAT_SENDER_ID_SIZE: return None
    version = buf[0]
    if version not in (1, 2): return None
    pkt_type, ttl = buf[1], buf[2]
    timestamp = struct.unpack(">Q", buf[3:11])[0]
    flags = buf[11]
    payload_len = struct.unpack(">H", buf[12:14])[0]
    if payload_len > BITCHAT_MAX_PAYLOAD: return None
    off = 14
    sender_id = buf[off:off+8]; off += 8
    recipient_id = None
    if flags & BITCHAT_FLAG_HAS_RECIPIENT:
        recipient_id = buf[off:off+8]; off += 8
    payload = buf[off:off+payload_len]; off += payload_len
    signature = None
    if flags & BITCHAT_FLAG_HAS_SIGNATURE:
        signature = buf[off:off+64]; off += 64
    return {"type": pkt_type, "ttl": ttl, "timestamp": timestamp, "flags": flags,
            "sender_id": sender_id, "recipient_id": recipient_id,
            "payload": payload, "signature": signature}


class BitchatClient:
    def __init__(self, storage_dir="~/.bitchat"):
        self.storage_dir = Path(storage_dir).expanduser()
        self.storage_dir.mkdir(parents=True, exist_ok=True)
        self._signing_key = None
        self._verify_key = None
        self._sender_id = None
        self._client = None
        self._remote_char = None
        self._chat_cb = None
        self._log_cb = None

    def on_chat(self, cb): self._chat_cb = cb
    def on_log(self, cb): self._log_cb = cb
    def _log(self, msg):
        if self._log_cb: self._log_cb(msg)
        else: print(f"[bchat] {msg}")

    def begin(self):
        seed_path = self.storage_dir / "seed.bin"
        if seed_path.exists():
            self._signing_key = SigningKey(seed_path.read_bytes())
        else:
            self._signing_key = SigningKey.generate()
            seed_path.write_bytes(self._signing_key.encode())
            self._log(f"Generated new keypair at {seed_path}")
        self._verify_key = self._signing_key.verify_key.encode()
        self._sender_id = self._verify_key[:8]  # Ed25519 pk 앞 8B = senderID
        sid = self._sender_id.hex()
        self._log(f"identity ready: node_id={sid}")
        return True

    async def connect_to_relay(self, timeout=10.0):
        if self.is_connected(): return True
        self._log(f"scanning for bitchat relays ({timeout}s)...")
        devices = await BleakScanner.discover(timeout=timeout)
        best = None; best_rssi = -200
        for d in devices:
            uuids = d.metadata.get("uuids", []) if hasattr(d, "metadata") else []
            if BITCHAT_BLE_SERVICE_UUID in uuids:
                rssi = d.rssi or -100
                self._log(f"found relay: {d.address} rssi={rssi}")
                if rssi > best_rssi: best_rssi = rssi; best = d
        if not best: self._log("no relay found"); return False
        self._log(f"connecting to {best.address}...")
        self._client = BleakClient(best.address)
        await self._client.connect()
        for svc in self._client.services:
            if str(svc.uuid).lower() == BITCHAT_BLE_SERVICE_UUID:
                for char in svc.characteristics:
                    if str(char.uuid).lower() == BITCHAT_BLE_CHAR_UUID:
                        self._remote_char = char
        if not self._remote_char:
            self._log("characteristic not found"); return False
        await self._client.start_notify(self._remote_char, self._on_notify)
        self._log("connected to relay")
        return True

    async def disconnect(self):
        if self._client:
            await self._client.stop_notify(self._remote_char)
            await self._client.disconnect()
            self._client = None; self._remote_char = None
            self._log("disconnected")

    def is_connected(self):
        return self._client is not None and self._client.is_connected

    async def send_announce(self, nickname):
        if not self.is_connected(): return False
        ts = int(time.time() * 1000)
        payload = build_announce_payload(nickname, self._verify_key)
        pkt = build_packet(BITCHAT_MSG_ANNOUNCE, 7, self._sender_id, ts, payload, None, True)
        sign_msg = compute_signing_message(pkt)
        sig = self._signing_key.sign(sign_msg).signature
        # Insert signature at end (before padding? no, after payload)
        pkt = pkt[:-64] + sig
        await self._client.write_gatt_char(self._remote_char, pkt, response=True)
        self._log(f"ANNOUNCE sent (nickname={nickname}, {len(pkt)} bytes)")
        return True

    async def send_message(self, text):
        if not self.is_connected(): return False
        ts = int(time.time() * 1000)
        payload = text.encode('utf-8')
        pkt = build_packet(BITCHAT_MSG_MESSAGE, 7, self._sender_id, ts, payload, BITCHAT_BROADCAST_ID, True)
        sign_msg = compute_signing_message(pkt)
        sig = self._signing_key.sign(sign_msg).signature
        pkt = pkt[:-64] + sig
        await self._client.write_gatt_char(self._remote_char, pkt, response=True)
        self._log(f"MESSAGE sent ({len(pkt)} bytes)")
        return True

    def _on_notify(self, char, data):
        pkt = parse_packet(bytes(data))
        if not pkt: return
        sender_id = pkt["sender_id"].hex()
        if pkt["type"] == BITCHAT_MSG_MESSAGE and self._chat_cb:
            try: text = pkt["payload"].decode('utf-8', errors='replace')
            except: text = pkt["payload"].hex()
            self._chat_cb(sender_id, text)

    async def loop_forever(self):
        while True:
            await asyncio.sleep(0.1)

    def get_node_id(self):
        return self._sender_id.hex() if self._sender_id else "???"


async def _cli_main():
    client = BitchatClient()
    client.on_log(lambda m: print(f"[LOG] {m}", flush=True))
    client.on_chat(lambda s, t: print(f"\n[CHAT] {s}: {t}\n> ", flush=True))
    if not client.begin(): return
    print(f"Node ID: {client.get_node_id()}")
    if not await client.connect_to_relay(10): return
    await client.send_announce("python-cli")
    await client.send_message("hello from python!")
    try: await client.loop_forever()
    except KeyboardInterrupt: await client.disconnect()

if __name__ == "__main__":
    asyncio.run(_cli_main())
