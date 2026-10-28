#!/usr/bin/env python3
"""
bitchat_client.py - Python reference implementation of bitchat client.

Desktop/mobile client using bleak (BLE) + pynacl (Ed25519).

Usage:
    from bitchat_client import BitchatClient

    client = BitchatClient()
    client.begin()                                    # init keys (auto-saved to ~/.bitchat/seed)
    await client.connect_to_relay(timeout=10)         # BLE scan + connect
    client.on_chat_message(lambda s, p: print(f"[{s}] {p.decode()}"))
    await client.send_chat("hello from python")
    await client.loop_forever()                       # event loop
"""

import asyncio
import json
import os
import struct
from pathlib import Path
from typing import Callable, Optional

try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.characteristic import BleakGATTCharacteristic
except ImportError:
    raise ImportError("pip install bleak")

try:
    from nacl.signing import SigningKey, VerifyKey
    from nacl.exceptions import BadSignatureError
except ImportError:
    raise ImportError("pip install pynacl")


# ============================================================================
# Constants (must match firmware)
# ============================================================================
BITCHAT_MAGIC = 0xBC
BITCHAT_VERSION = 0x01
BITCHAT_MAX_TTL = 7
BITCHAT_MAX_PAYLOAD_LEN = 512

BITCHAT_PUBKEY_LEN = 32
BITCHAT_SIG_LEN = 64
BITCHAT_ID_STR_LEN = 13

BITCHAT_TYPE_CHAT = 0x01
BITCHAT_TYPE_ACK = 0x02
BITCHAT_TYPE_PEER_INFO = 0x03
BITCHAT_TYPE_PEER_REQUEST = 0x04
BITCHAT_TYPE_LEAVE = 0x05
BITCHAT_TYPE_KEY_EXCHANGE = 0x06
BITCHAT_TYPE_PING = 0x07

BITCHAT_HEADER_LEN = 1 + 1 + 1 + 1 + BITCHAT_PUBKEY_LEN + 8 + 2
BITCHAT_MIN_PKT_LEN = BITCHAT_HEADER_LEN + BITCHAT_SIG_LEN
BITCHAT_MAX_PKT_LEN = BITCHAT_HEADER_LEN + BITCHAT_MAX_PAYLOAD_LEN + BITCHAT_SIG_LEN

# BLE UUIDs (same as firmware)
BCHAT_SERVICE_UUID = "0000bc00-0000-1000-8000-00805f9b34fb"
BCHAT_TX_CHAR_UUID = "0000bc01-0000-1000-8000-00805f9b34fb"   # notify (relay -> us)
BCHAT_RX_CHAR_UUID = "0000bc02-0000-1000-8000-00805f9b34fb"   # write (us -> relay)


# ============================================================================
# Packet builder/parser
# ============================================================================
def build_packet(
    packet_type: int,
    ttl: int,
    sender_pk: bytes,
    counter: int,
    payload: bytes = b"",
    signature: bytes = b"\x00" * BITCHAT_SIG_LEN,
) -> bytes:
    """Build a bitchat packet (caller must compute signature separately)."""
    if len(sender_pk) != BITCHAT_PUBKEY_LEN:
        raise ValueError(f"sender_pk must be {BITCHAT_PUBKEY_LEN} bytes")
    if len(payload) > BITCHAT_MAX_PAYLOAD_LEN:
        raise ValueError(f"payload too long: {len(payload)}")
    if ttl > BITCHAT_MAX_TTL:
        raise ValueError(f"ttl too high: {ttl}")
    if len(signature) != BITCHAT_SIG_LEN:
        raise ValueError(f"signature must be {BITCHAT_SIG_LEN} bytes")

    header = struct.pack(
        ">BBBB32sQH",
        BITCHAT_MAGIC,
        BITCHAT_VERSION,
        packet_type,
        ttl,
        sender_pk,
        counter,
        len(payload),
    )
    return header + payload + signature


def parse_packet(buf: bytes) -> Optional[dict]:
    """Parse a bitchat packet. Returns dict or None if invalid."""
    if len(buf) < BITCHAT_MIN_PKT_LEN or len(buf) > BITCHAT_MAX_PKT_LEN:
        return None
    if buf[0] != BITCHAT_MAGIC:
        return None
    ver = buf[1]
    if ver != BITCHAT_VERSION:
        return None
    ptype = buf[2]
    ttl = buf[3]
    if ttl > BITCHAT_MAX_TTL:
        return None
    sender_pk = buf[4:36]
    counter = struct.unpack(">Q", buf[36:44])[0]
    payload_len = struct.unpack(">H", buf[44:46])[0]
    if payload_len > BITCHAT_MAX_PAYLOAD_LEN:
        return None
    expected_len = BITCHAT_HEADER_LEN + payload_len + BITCHAT_SIG_LEN
    if len(buf) != expected_len:
        return None
    payload = buf[46:46 + payload_len]
    signature = buf[46 + payload_len:46 + payload_len + BITCHAT_SIG_LEN]
    return {
        "magic": buf[0],
        "version": ver,
        "type": ptype,
        "ttl": ttl,
        "sender_pk": sender_pk,
        "counter": counter,
        "payload": payload,
        "signature": signature,
    }


def signing_message(packet: dict) -> bytes:
    """Extract the message that was signed (header + payload, no signature)."""
    return struct.pack(
        ">BBBB32sQH",
        packet["magic"],
        packet["version"],
        packet["type"],
        packet["ttl"],
        packet["sender_pk"],
        packet["counter"],
        len(packet["payload"]),
    ) + packet["payload"]


def id_from_pk(pk: bytes) -> str:
    """Return human-readable node ID (12 hex chars from first 6 bytes)."""
    return pk[:6].hex()


# ============================================================================
# BitchatClient
# ============================================================================
class BitchatClient:
    """High-level bitchat client using bleak (BLE) + pynacl (Ed25519)."""

    def __init__(self, storage_dir: str = "~/.bitchat"):
        self.storage_dir = Path(storage_dir).expanduser()
        self.storage_dir.mkdir(parents=True, exist_ok=True)

        # Identity (Ed25519 keypair)
        self._signing_key: Optional[SigningKey] = None
        self._verify_key: Optional[bytes] = None  # 32-byte public key

        # BLE state
        self._client: Optional[BleakClient] = None
        self._relay_address: Optional[str] = None
        self._tx_char: Optional[BleakGATTCharacteristic] = None
        self._rx_char: Optional[BleakGATTCharacteristic] = None

        # Counter (monotonic, persisted)
        self._counter: int = 0

        # Callbacks
        self._chat_cb: Optional[Callable[[str, bytes], None]] = None
        self._peer_cb: Optional[Callable[[str], None]] = None
        self._raw_cb: Optional[Callable[[bytes], None]] = None
        self._log_cb: Optional[Callable[[str], None]] = None

        self._loop_task: Optional[asyncio.Task] = None

    # ============================================================
    # Logging
    # ============================================================
    def _log(self, msg: str):
        if self._log_cb:
            self._log_cb(msg)
        else:
            print(f"[bchat] {msg}")

    # ============================================================
    # Callback registration
    # ============================================================
    def on_chat_message(self, cb: Callable[[str, bytes], None]):
        """cb(sender_id: str, payload: bytes) -> None"""
        self._chat_cb = cb

    def on_peer_discovered(self, cb: Callable[[str], None]):
        """cb(peer_id: str) -> None"""
        self._peer_cb = cb

    def on_raw_packet(self, cb: Callable[[bytes], None]):
        """cb(raw_bytes: bytes) -> None"""
        self._raw_cb = cb

    def on_log(self, cb: Callable[[str], None]):
        """cb(message: str) -> None"""
        self._log_cb = cb

    # ============================================================
    # Identity (Ed25519 keypair)
    # ============================================================
    def begin(self) -> bool:
        """Initialize Ed25519 keypair (auto-saved to disk for persistence)."""
        seed_path = self.storage_dir / "seed.bin"
        counter_path = self.storage_dir / "counter.json"

        if seed_path.exists():
            seed = seed_path.read_bytes()
            self._signing_key = SigningKey(seed)
        else:
            self._signing_key = SigningKey.generate()
            seed_path.write_bytes(self._signing_key.encode())
            self._log(f"Generated new keypair at {seed_path}")

        self._verify_key = self._signing_key.verify_key.encode()
        self._counter = 0
        if counter_path.exists():
            try:
                self._counter = json.loads(counter_path.read_text()).get("counter", 0)
            except Exception:
                pass

        node_id = id_from_pk(self._verify_key)
        self._log(f"identity ready: node_id={node_id} counter={self._counter}")
        return True

    def _save_counter(self):
        (self.storage_dir / "counter.json").write_text(
            json.dumps({"counter": self._counter})
        )

    # ============================================================
    # BLE connection
    # ============================================================
    async def connect_to_relay(self, timeout: float = 10.0) -> bool:
        """Scan for bitchat relay nodes and connect to the closest (strongest RSSI)."""
        if self.is_connected():
            self._log("already connected")
            return True

        self._log(f"scanning for bitchat relays ({timeout}s)...")
        devices = await BleakScanner.discover(timeout=timeout)

        best = None
        best_rssi = -200
        for d in devices:
            uuids = d.metadata.get("uuids", []) if hasattr(d, "metadata") else []
            if BCHAT_SERVICE_UUID in uuids:
                rssi = d.rssi or -100
                self._log(f"found relay: {d.address} rssi={rssi}")
                if rssi > best_rssi:
                    best_rssi = rssi
                    best = d

        if not best:
            self._log("no relay found")
            return False

        self._relay_address = best.address
        self._log(f"connecting to {best.address} (rssi={best_rssi})...")
        self._client = BleakClient(best.address)
        await self._client.connect()

        # Find service + characteristics
        for service in self._client.services:
            if str(service.uuid).lower() == BCHAT_SERVICE_UUID:
                for char in service.characteristics:
                    uuid = str(char.uuid).lower()
                    if uuid == BCHAT_TX_CHAR_UUID:
                        self._tx_char = char
                    elif uuid == BCHAT_RX_CHAR_UUID:
                        self._rx_char = char

        if not self._tx_char or not self._rx_char:
            self._log("ERROR: characteristics not found on relay")
            await self._client.disconnect()
            self._client = None
            return False

        # Subscribe to notifications
        await self._client.start_notify(self._tx_char, self._on_notify)
        self._log(f"connected to {self._relay_address}")
        return True

    async def disconnect(self):
        if self._client:
            await self._client.stop_notify(self._tx_char)
            await self._client.disconnect()
            self._client = None
            self._relay_address = None
            self._tx_char = None
            self._rx_char = None
            self._log("disconnected")

    def is_connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    # ============================================================
    # Sending
    # ============================================================
    async def _send_packet(self, packet_type: int, ttl: int,
                            payload: bytes = b"") -> bool:
        if not self.is_connected():
            self._log("not connected")
            return False
        if len(payload) > BITCHAT_MAX_PAYLOAD_LEN:
            self._log(f"payload too long: {len(payload)}")
            return False

        self._counter += 1
        self._save_counter()

        # Build packet (without signature first)
        unsigned = build_packet(
            packet_type, ttl,
            self._verify_key, self._counter,
            payload, signature=b"\x00" * BITCHAT_SIG_LEN,
        )

        # Compute signature over (header + payload)
        msg_to_sign = unsigned[:BITCHAT_HEADER_LEN + len(payload)]
        sig = self._signing_key.sign(msg_to_sign).signature
        # Verify: sig is exactly 64 bytes
        assert len(sig) == BITCHAT_SIG_LEN

        # Insert signature at end
        signed_packet = msg_to_sign + sig
        assert len(signed_packet) == len(unsigned)

        try:
            await self._client.write_gatt_char(self._rx_char, signed_packet, response=True)
            return True
        except Exception as e:
            self._log(f"BLE write failed: {e}")
            return False

    async def send_chat(self, text: str) -> bool:
        payload = text.encode("utf-8")
        self._log(f"send_chat: len={len(payload)}")
        return await self._send_packet(BITCHAT_TYPE_CHAT, BITCHAT_MAX_TTL, payload)

    async def send_ack(self, sender_pk: bytes, counter: int) -> bool:
        ack_payload = sender_pk + struct.pack(">Q", counter)
        return await self._send_packet(BITCHAT_TYPE_ACK, BITCHAT_MAX_TTL, ack_payload)

    async def send_ping(self) -> bool:
        return await self._send_packet(BITCHAT_TYPE_PING, BITCHAT_MAX_TTL, b"")

    async def send_peer_info(self, nickname: str) -> bool:
        return await self._send_packet(
            BITCHAT_TYPE_PEER_INFO, BITCHAT_MAX_TTL, nickname.encode("utf-8")
        )

    # ============================================================
    # Receive (notification handler)
    # ============================================================
    def _on_notify(self, char: BleakGATTCharacteristic, data: bytearray):
        if self._raw_cb:
            self._raw_cb(bytes(data))

        pkt = parse_packet(bytes(data))
        if not pkt:
            self._log("malformed packet received")
            return

        sender_id = id_from_pk(pkt["sender_pk"])

        # Verify signature
        msg = signing_message(pkt)
        try:
            VerifyKey(pkt["sender_pk"]).verify(msg, pkt["signature"])
        except BadSignatureError:
            self._log(f"signature verification FAILED from {sender_id}")
            return

        # Dispatch by type
        if pkt["type"] == BITCHAT_TYPE_CHAT:
            if self._chat_cb:
                self._chat_cb(sender_id, pkt["payload"])
            # Auto-ACK
            asyncio.create_task(self.send_ack(pkt["sender_pk"], pkt["counter"]))
        elif pkt["type"] == BITCHAT_TYPE_PEER_INFO:
            if self._peer_cb:
                self._peer_cb(sender_id)
        else:
            # ACK/PING/LEAVE/etc - just logged via raw_cb
            pass

    # ============================================================
    # Loop (async)
    # ============================================================
    async def loop_forever(self):
        """Run forever, sending keep-alive ping every 60s."""
        ping_interval = 60.0
        last_ping = 0
        while True:
            if self.is_connected():
                now = asyncio.get_event_loop().time()
                if now - last_ping > ping_interval:
                    await self.send_ping()
                    last_ping = now
            await asyncio.sleep(0.1)

    # ============================================================
    # Info
    # ============================================================
    def get_node_id(self) -> str:
        return id_from_pk(self._verify_key)

    def get_counter(self) -> int:
        return self._counter

    def get_connected_relay(self) -> Optional[str]:
        return self._relay_address


# ============================================================================
# CLI entry point for testing
# ============================================================================
async def _cli_main():
    import sys

    client = BitchatClient()
    client.on_log(lambda m: print(f"[LOG] {m}"))
    client.on_chat_message(
        lambda s, p: print(f"\n[CHAT] {s}: {p.decode('utf-8', errors='replace')}")
    )
    client.on_peer_discovered(lambda s: print(f"\n[PEER] {s} discovered"))

    if not client.begin():
        print("FATAL: begin() failed")
        return

    print(f"My node ID: {client.get_node_id()}")

    if not await client.connect_to_relay(timeout=10):
        print("No relay found. Exiting.")
        return

    await client.send_peer_info("python-cli")
    await client.send_chat("hello from python!")

    # Run forever
    try:
        await client.loop_forever()
    except KeyboardInterrupt:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(_cli_main())
