"""
protocol.py

PicoComm Protocol - Python host implementation.

Features:
  - Binary packet protocol over USB Serial
  - PING/PONG handshake
  - Stop-and-wait with ACK
  - Automatic retransmission (up to MAX_RETRIES)
  - File transfer to Pico flash
  - Session statistics

Usage:
  py protocol.py --port COM8           # Interactive echo demo
  py protocol.py --port COM8 --test    # Full automated test suite
  py protocol.py --port COM8 --file myfile.txt  # Send a file to Pico

Author: [Your Name]
Date:   April 2026
Module: Communications and Protocols (UFCFVR-15-3)
"""

import serial
import struct
import time
import argparse
import os
import math

# -------------------------------------------------------------------------
# Constants
# -------------------------------------------------------------------------
MAGIC_0        = 0xCA
MAGIC_1        = 0xFE
MAX_PAYLOAD    = 256
HEADER_SIZE    = 6
TIMEOUT_S      = 1.0
MAX_RETRIES    = 3
FILE_HDR_SIZE  = 24   # chunk_index(2) + total(2) + filename(20)
FILE_CHUNK_SIZE = MAX_PAYLOAD - FILE_HDR_SIZE

PKT_DATA     = 0x01
PKT_ACK      = 0x02
PKT_PING     = 0x03
PKT_PONG     = 0x04
PKT_ERROR    = 0x05
PKT_FILE     = 0x06
PKT_FILE_ACK = 0x07

PKT_NAMES = {
    PKT_DATA: "DATA", PKT_ACK: "ACK", PKT_PING: "PING",
    PKT_PONG: "PONG", PKT_ERROR: "ERROR",
    PKT_FILE: "FILE", PKT_FILE_ACK: "FILE_ACK",
}

# -------------------------------------------------------------------------
# Low-level helpers
# -------------------------------------------------------------------------

def _checksum(pkt_type, seq, length, payload):
    cs = pkt_type ^ seq ^ (length & 0xFF) ^ (length >> 8)
    for b in payload:
        cs ^= b
    return cs & 0xFF

def _encode(pkt_type, seq, payload):
    length = len(payload)
    cs = _checksum(pkt_type, seq, length, payload)
    header = struct.pack("<BBBBH", MAGIC_0, MAGIC_1, pkt_type, seq, length)
    return header + payload + bytes([cs])

def _decode(data):
    if len(data) < HEADER_SIZE + 1:
        return None
    if data[0] != MAGIC_0 or data[1] != MAGIC_1:
        return None
    pkt_type = data[2]
    seq      = data[3]
    length   = struct.unpack_from("<H", data, 4)[0]
    if length > MAX_PAYLOAD:
        return None
    if len(data) < HEADER_SIZE + length + 1:
        return None
    payload     = data[HEADER_SIZE:HEADER_SIZE + length]
    received_cs = data[HEADER_SIZE + length]
    expected_cs = _checksum(pkt_type, seq, length, payload)
    if received_cs != expected_cs:
        print(f"[PROTO] Checksum mismatch: expected 0x{expected_cs:02X} "
              f"got 0x{received_cs:02X}")
        return None
    return {"type": pkt_type, "seq": seq, "length": length, "payload": payload}

# -------------------------------------------------------------------------
# CustomProtocol class
# -------------------------------------------------------------------------

class CustomProtocol:
    """Socket-like interface to the PicoComm binary protocol."""

    def __init__(self, port, baudrate=115200):
        self.port     = port
        self.baudrate = baudrate
        self.ser      = None
        self.tx_seq   = 0
        # Stats
        self.stats = {
            "sent": 0, "received": 0,
            "retransmissions": 0, "timeouts": 0
        }

    def connect(self):
        """Open port and perform PING/PONG handshake. Retries up to 15 times."""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=TIMEOUT_S)
            time.sleep(0.5)
        except serial.SerialException as e:
            print(f"[PROTO] Failed to open {self.port}: {e}")
            return False

        print(f"[PROTO] Opened {self.port} at {self.baudrate} baud")

        for attempt in range(15):
            ping = _encode(PKT_PING, self.tx_seq, b"")
            self.ser.write(ping)
            print(f"[PROTO] PING sent (attempt {attempt+1}, seq={self.tx_seq})")
            pkt = self._recv_packet()
            if pkt and pkt["type"] == PKT_PONG:
                self.tx_seq += 1
                print(f"[PROTO] PONG received – connected!")
                return True
            print(f"[PROTO] No PONG, retrying...")
            time.sleep(0.5)

        print("[PROTO] Handshake failed")
        return False

    def send(self, data):
        """
        Send a DATA packet with automatic retransmission.
        Retries up to MAX_RETRIES times if no ACK received.
        """
        if self.ser is None:
            return -1
        if len(data) > MAX_PAYLOAD:
            print(f"[PROTO] Payload too large")
            return -1

        seq = self.tx_seq

        for attempt in range(MAX_RETRIES + 1):
            if attempt > 0:
                print(f"[PROTO] Retransmitting seq={seq} (attempt {attempt+1})")
                self.stats["retransmissions"] += 1

            pkt = _encode(PKT_DATA, seq, data)
            self.ser.write(pkt)
            self.stats["sent"] += 1
            print(f"[PROTO] Sent {len(data)} bytes (seq={seq}): {data!r}")

            ack = self._recv_packet()
            if ack and ack["type"] == PKT_ACK and ack["seq"] == seq:
                self.tx_seq = (self.tx_seq + 1) & 0xFF
                print(f"[PROTO] ACK received (attempt {attempt+1})")
                return len(data)

            print(f"[PROTO] No ACK for seq={seq}")
            self.stats["timeouts"] += 1

        print(f"[PROTO] Send failed after {MAX_RETRIES} retries")
        return -1

    def receive(self, buffer_size=256):
        """Receive a DATA packet and send ACK."""
        if self.ser is None:
            return b""
        pkt = self._recv_packet()
        if pkt is None:
            return b""
        if pkt["type"] != PKT_DATA:
            return b""
        payload = pkt["payload"][:buffer_size]
        ack = _encode(PKT_ACK, pkt["seq"], b"")
        self.ser.write(ack)
        self.stats["received"] += 1
        print(f"[PROTO] Received {len(payload)} bytes (seq={pkt['seq']}), ACK sent")
        return payload

    def send_file(self, filepath):
        """
        Send a file to the Pico in chunks using PKT_FILE packets.
        Each chunk gets a FILE_ACK before the next is sent.
        """
        if not os.path.exists(filepath):
            print(f"[PROTO] File not found: {filepath}")
            return -1

        with open(filepath, "rb") as f:
            file_data = f.read()

        filename = os.path.basename(filepath)[:19].encode().ljust(20, b'\x00')
        total_chunks = max(1, math.ceil(len(file_data) / FILE_CHUNK_SIZE))

        print(f"[PROTO] Sending '{filepath}' "
              f"({len(file_data)} bytes, {total_chunks} chunks)")

        for i in range(total_chunks):
            chunk_data = file_data[i*FILE_CHUNK_SIZE:(i+1)*FILE_CHUNK_SIZE]

            # Build FileChunk payload: chunk_index(2) + total(2) + filename(20) + data
            payload = struct.pack("<HH", i, total_chunks) + filename + chunk_data
            pkt = _encode(PKT_FILE, self.tx_seq, payload)

            # Send with retransmission
            for attempt in range(MAX_RETRIES + 1):
                self.ser.write(pkt)
                print(f"[PROTO] Sent chunk {i+1}/{total_chunks} "
                      f"({len(chunk_data)} bytes)")

                ack = self._recv_packet()
                if ack and ack["type"] == PKT_FILE_ACK:
                    self.tx_seq = (self.tx_seq + 1) & 0xFF
                    break
                print(f"[PROTO] No FILE_ACK, retrying chunk {i+1}...")
                if attempt == MAX_RETRIES:
                    print("[PROTO] File transfer failed")
                    return -1

        print(f"[PROTO] File transfer complete!")
        return len(file_data)

    def ping(self):
        """Send PING and check for PONG."""
        if self.ser is None:
            return False
        seq = self.tx_seq
        self.ser.write(_encode(PKT_PING, seq, b""))
        pkt = self._recv_packet()
        if pkt and pkt["type"] == PKT_PONG:
            self.tx_seq = (self.tx_seq + 1) & 0xFF
            print(f"[PROTO] PING/PONG OK (seq={seq})")
            return True
        print("[PROTO] PING timeout")
        return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.write(_encode(PKT_ERROR, self.tx_seq, b""))
            print("[PROTO] Disconnect sent")

    def cleanup(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser    = None
        self.tx_seq = 0
        print("[PROTO] Cleaned up")

    def print_stats(self):
        print("\n--- Session Stats ---")
        for k, v in self.stats.items():
            print(f"  {k:20s}: {v}")
        print("---------------------\n")

    def _recv_packet(self):
        if self.ser is None:
            return None
        deadline = time.time() + TIMEOUT_S
        while time.time() < deadline:
            b = self.ser.read(1)
            if not b:
                continue
            if b[0] == MAGIC_0:
                b2 = self.ser.read(1)
                if b2 and b2[0] == MAGIC_1:
                    header_rest = self.ser.read(4)
                    if len(header_rest) < 4:
                        continue
                    length = struct.unpack_from("<H", header_rest, 2)[0]
                    if length > MAX_PAYLOAD:
                        continue
                    rest = self.ser.read(length + 1)
                    if len(rest) < length + 1:
                        continue
                    full = bytes([MAGIC_0, MAGIC_1]) + header_rest + rest
                    pkt = _decode(full)
                    if pkt:
                        ptype = PKT_NAMES.get(pkt["type"], f"0x{pkt['type']:02X}")
                        print(f"[PROTO] Received {ptype} seq={pkt['seq']} "
                              f"len={pkt['length']}")
                        return pkt
        return None


# -------------------------------------------------------------------------
# Test suite
# -------------------------------------------------------------------------

def run_tests(port):
    print("\n========== PICOCOMM TEST SUITE ==========")
    passed = 0
    total  = 0

    def check(name, condition):
        nonlocal passed, total
        total += 1
        result = "PASS" if condition else "FAIL"
        print(f"  [{result}] {name}")
        if condition:
            passed += 1

    proto = CustomProtocol(port)

    # 1: Handshake
    ok = proto.connect()
    check("PING/PONG handshake", ok)
    if not ok:
        print("Cannot proceed.")
        return

    # 2-3: Short message echo
    n = proto.send(b"Hello")
    check("Send short message", n == 5)
    reply = proto.receive()
    check("Receive echo reply", reply == b"ECHO: Hello")

    # 4-5: Long message
    msg = b"The quick brown fox jumps over the lazy dog"
    n = proto.send(msg)
    check("Send long message", n == len(msg))
    reply = proto.receive()
    check("Receive long echo", reply == b"ECHO: " + msg)

    # 6-11: Multi-packet
    for i in range(3):
        n = proto.send(f"Packet {i}".encode())
        check(f"Multi-send packet {i}", n > 0)
        reply = proto.receive()
        check(f"Multi-receive packet {i}", reply == f"ECHO: Packet {i}".encode())

    # 12-13: Checksum
    cs = _checksum(PKT_DATA, 1, 5, b"Hello")
    check("Checksum non-zero", cs != 0)
    check("Checksum deterministic", cs == _checksum(PKT_DATA, 1, 5, b"Hello"))

    # 14-18: Encode/decode roundtrip
    encoded = _encode(PKT_DATA, 42, b"Test")
    decoded = _decode(encoded)
    check("Encode/decode roundtrip", decoded is not None)
    check("Roundtrip type", decoded["type"]    == PKT_DATA)
    check("Roundtrip seq",  decoded["seq"]     == 42)
    check("Roundtrip payload", decoded["payload"] == b"Test")

    # 19: Corrupted checksum rejected
    bad = bytearray(encoded)
    bad[-1] ^= 0xFF
    check("Corrupted checksum rejected", _decode(bytes(bad)) is None)

    # 20: Retransmission counter starts at 0
    check("Retransmission counter starts at 0",
          proto.stats["retransmissions"] == 0)

    # 21: File transfer
    test_file = "test_transfer.txt"
    with open(test_file, "w") as f:
        f.write("PicoComm file transfer test!\nHello from Python!\n")
    n = proto.send_file(test_file)
    check("File transfer succeeds", n > 0)
    os.remove(test_file)

    # 22: Mid-session ping
    time.sleep(0.5)
    check("Mid-session ping", proto.ping())

    proto.disconnect()
    proto.print_stats()
    proto.cleanup()

    print(f"\nResults: {passed} / {total} tests passed")
    print("==========================================\n")


# -------------------------------------------------------------------------
# Interactive demo
# -------------------------------------------------------------------------

def interactive_demo(port):
    proto = CustomProtocol(port)
    if not proto.connect():
        print("Failed to connect.")
        return

    print("\nConnected! Commands: type a message, or 'quit', 'stats', 'ping'")

    try:
        while True:
            msg = input("You: ").strip()
            if msg.lower() == "quit":
                break
            elif msg.lower() == "stats":
                proto.print_stats()
                continue
            elif msg.lower() == "ping":
                proto.ping()
                continue
            elif not msg:
                continue

            n = proto.send(msg.encode())
            if n < 0:
                print("Send failed.")
                continue
            reply = proto.receive()
            if reply:
                print(f"Pico: {reply.decode(errors='replace')}")

    except KeyboardInterrupt:
        print("\nInterrupted.")

    proto.disconnect()
    proto.print_stats()
    proto.cleanup()


# -------------------------------------------------------------------------
# Entry point
# -------------------------------------------------------------------------

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PicoComm Protocol Host")
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--file", help="Send a file to the Pico")
    args = parser.parse_args()

    if args.test:
        run_tests(args.port)
    elif args.file:
        proto = CustomProtocol(args.port)
        if proto.connect():
            proto.send_file(args.file)
            proto.disconnect()
            proto.cleanup()
    else:
        interactive_demo(args.port)
