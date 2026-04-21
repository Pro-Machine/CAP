# Worksheet 2: Custom Communication Protocol — PicoComm

> **Module**: Communications and Protocols (UFCFVR-15-3)  
> **Deadline**: Before 14:00 on 24th April 2026  
> **Author**: Kaan Karadag — Student ID: 23083770  

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Team](#team)
3. [Protocol Design](#protocol-design)
   - [Goals and Rationale](#goals-and-rationale)
   - [Packet Structure](#packet-structure)
   - [Packet Types](#packet-types)
   - [Transmission Method](#transmission-method)
4. [Implementation](#implementation)
   - [Pico Side (C)](#pico-side-c)
   - [PC Side (Python)](#pc-side-python)
   - [API Reference – C](#api-reference--c)
   - [API Reference – Python](#api-reference--python)
5. [How to Build and Run](#how-to-build-and-run)
6. [Testing](#testing)
7. [Challenges and Solutions](#challenges-and-solutions)
8. [Reflection](#reflection)
9. [Video Presentation: Questions and Answers](#video-presentation-questions-and-answers)
10. [Resources](#resources)

---

## Project Overview

This project designs and implements **PicoComm** — a custom binary communication protocol for the Raspberry Pi Pico W. The protocol defines a structured packet format with a 2-byte magic header, packet type, sequence number, variable-length payload, and an XOR checksum. Communication is transmitted over **USB Serial (CDC)** between the Pico and a host PC.

On the Pico side, the protocol is implemented in C using the Pico SDK. On the PC side, a Python class (`CustomProtocol`) using **PySerial** handles encoding, sending, receiving, and decoding packets. The protocol supports stop-and-wait reliability with ACK, automatic retransmission, file transfer, and session statistics.

---

## Team

| Name | Student ID |
|------|------------|
| Kaan Karadag | 23083770 |

> This was an **individual project**.

---

## Protocol Design

### Goals and Rationale

| Requirement | Design Response |
|-------------|----------------|
| Binary (not text) | All fields encoded as fixed-size integers |
| Error detection | XOR checksum over TYPE + SEQ + LENGTH + PAYLOAD |
| Variable payload size | 2-byte little-endian `LENGTH` field |
| Frame synchronisation | 2-byte magic header `0xCA 0xFE` |
| Reliable delivery | ACK per packet + automatic retransmission (3 retries) |
| Minimal overhead | 7 bytes total overhead per packet |
| Extensible | TYPE field allows new message types without changing structure |
| File transfer | Chunked PKT_FILE packets with FILE_ACK |

---

### Packet Structure

```
+--------+--------+--------+--------+---------+---------+----------+----------+
| MAGIC0 | MAGIC1 |  TYPE  |  SEQ   |  LEN_LO |  LEN_HI | PAYLOAD  | CHECKSUM |
| 0xCA   | 0xFE   | 1 byte | 1 byte |  1 byte |  1 byte | 0-256 B  | 1 byte   |
+--------+--------+--------+--------+---------+---------+----------+----------+
```

| Field    | Size    | Description |
|----------|---------|-------------|
| `MAGIC`  | 2 bytes | `0xCA 0xFE` — identifies start of packet, enables re-sync |
| `TYPE`   | 1 byte  | Packet type (see table below) |
| `SEQ`    | 1 byte  | Sequence number 0–255, wraps around |
| `LENGTH` | 2 bytes | Payload length in bytes (little-endian) |
| `PAYLOAD`| 0–256 B | Variable-length data |
| `CHECKSUM`| 1 byte | XOR of TYPE + SEQ + LENGTH bytes + PAYLOAD |

**Total overhead:** 7 bytes per packet  
**Maximum payload:** 256 bytes

---

### Packet Types

| Type | Value | Description |
|------|-------|-------------|
| `PKT_DATA` | `0x01` | Carries a data payload |
| `PKT_ACK` | `0x02` | Acknowledges receipt of a DATA or FILE packet |
| `PKT_PING` | `0x03` | Connection check / handshake initiation |
| `PKT_PONG` | `0x04` | Response to PING |
| `PKT_ERROR` | `0x05` | Error signal or disconnect notification |
| `PKT_FILE` | `0x06` | File transfer chunk |
| `PKT_FILE_ACK` | `0x07` | File chunk acknowledgement |

---

### Transmission Method

**Chosen method: USB Serial (CDC — Communications Device Class)**

**Justification:**
- USB Serial is natively supported by the Pico SDK via `stdio_usb` with no additional hardware required.
- Provides reliable byte-level ordering — no signal integrity concerns compared to raw GPIO pulsing.
- PySerial on the PC side provides a mature, well-documented interface.
- Baud rate is effectively handled by the USB stack, removing the need to manually configure UART timing.
- The same connection used for `printf` debug output can carry the protocol packets.

**Alternatives considered:**

| Method | Pros | Cons | Reason Not Chosen |
|--------|------|------|------------------|
| GPIO Pulsing | Full bit-level control | Requires precise timing, susceptible to noise | Harder to implement reliably |
| Hardware UART | Standard serial protocol | Requires additional TX/RX wiring | USB Serial is simpler for PC communication |
| **USB Serial** ✅ | No extra hardware, mature Python support | Limited to PC communication | **Chosen** |

---

## Implementation

### Pico Side (C)

The implementation follows a **socket-like API** in `protocol.c` / `protocol.h`.

#### In-Memory Packet Structure

```c
typedef struct {
    uint8_t  type;                       // PKT_DATA, PKT_ACK, etc.
    uint8_t  seq;                        // Sequence number 0-255
    uint16_t length;                     // Payload length
    uint8_t  payload[PROTO_MAX_PAYLOAD]; // Up to 256 bytes
    uint8_t  checksum;                   // XOR checksum
} Packet;
```

#### XOR Checksum

```c
uint8_t proto_checksum(const Packet *pkt) {
    uint8_t cs = 0;
    cs ^= pkt->type;
    cs ^= pkt->seq;
    cs ^= (uint8_t)(pkt->length & 0xFF);
    cs ^= (uint8_t)(pkt->length >> 8);
    for (uint16_t i = 0; i < pkt->length; i++) cs ^= pkt->payload[i];
    return cs;
}
```

#### Wire Encoding

```
[0xCA][0xFE][type][seq][len_lo][len_hi][payload...][checksum]
```

Packets are decoded by scanning for magic bytes `0xCA 0xFE`, then reading the header and payload.

#### Reliability: Stop-and-Wait with Retransmission

Every DATA and FILE packet requires an ACK before the next is sent. If no ACK arrives within `PROTO_TIMEOUT_MS` (3 seconds), the packet is retransmitted up to `PROTO_MAX_RETRIES` (3) times. Session statistics (sent, received, retransmissions, timeouts, checksum errors) are tracked and printed on disconnect.

#### File Transfer

Files are split into 232-byte chunks, each wrapped in a PKT_FILE packet with a header containing: chunk index (2B), total chunks (2B), and filename (20B). The Pico acknowledges each chunk with PKT_FILE_ACK before the next is sent, then writes the reassembled file to flash.

---

### PC Side (Python)

```python
class CustomProtocol:
    def __init__(self, port: str, baudrate: int = 115200): ...
    def connect(self) -> bool: ...       # PING/PONG handshake with retry
    def send(self, data: bytes) -> int: ...     # Send DATA, wait for ACK (auto-retry)
    def receive(self, buffer_size=256) -> bytes: ...  # Receive DATA, send ACK
    def send_file(self, filepath: str) -> int: ...    # Send file in chunks
    def ping(self) -> bool: ...          # Check connection alive
    def disconnect(self): ...            # Send PKT_ERROR
    def cleanup(self): ...               # Close serial port
    def print_stats(self): ...           # Print session statistics
```

---

### API Reference – C

```c
void protocol_init(void);                            // Initialise USB serial
int  protocol_connect(void);                         // PING/PONG handshake
int  protocol_send(const void *data, int data_len);  // Send DATA, wait for ACK
int  protocol_receive(void *buffer, int buffer_len); // Receive DATA, send ACK
int  protocol_ping(void);                            // Check connection alive
void protocol_disconnect(void);                      // Send PKT_ERROR
void protocol_cleanup(void);                         // Reset state
void protocol_print_stats(void);                     // Print session statistics
```

---

### API Reference – Python

```python
proto = CustomProtocol(port="COM8", baudrate=115200)
proto.connect()

# Send data
proto.send(b"Hello Pico")

# Receive echo
reply = proto.receive()
print(reply.decode())   # "ECHO: Hello Pico"

# Send a file
proto.send_file("myfile.txt")

proto.disconnect()
proto.cleanup()
```

---

## How to Build and Run

### Pico (C)

```bash
cd cap_protocol
mkdir build && cd build
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -DPICO_BOARD=pico_w ..
make -j$(nproc)
```

Hold BOOTSEL, connect USB, release. Drag `picocomm.uf2` onto the RPI-RP2 drive.

### Python Host

```bash
pip install pyserial
py HostMachine.py --port COM8
```

The Pico's COM port can be found in Windows Device Manager under **Ports (COM & LPT)**. Adjust the port as needed.

**Available modes:**

| Command | Description |
|---------|-------------|
| `py HostMachine.py --port COM8` | Interactive echo demo |
| `py HostMachine.py --port COM8 --test` | Automated test suite |
| `py HostMachine.py --port COM8 --file myfile.txt` | Send a file to the Pico |

**Interactive commands:** type a message to echo it, or type `ping`, `stats`, `quit`.

---

## Testing

All tests were run on real hardware using `HostMachine.py` on Windows (COM8, 115200 baud) communicating with the Pico over USB Serial.

### Automated Test Suite — 21 / 21 passed

```
py HostMachine.py --port COM8 --test
```

| Test | Result |
|------|--------|
| PING/PONG handshake | ✅ PASS |
| Send short message (`Hello`) | ✅ PASS |
| Receive echo reply | ✅ PASS |
| Send long message (43 bytes) | ✅ PASS |
| Receive long echo | ✅ PASS |
| Multi-send packet 0 | ✅ PASS |
| Multi-receive packet 0 | ✅ PASS |
| Multi-send packet 1 | ✅ PASS |
| Multi-receive packet 1 | ✅ PASS |
| Multi-send packet 2 | ✅ PASS |
| Multi-receive packet 2 | ✅ PASS |
| Checksum non-zero | ✅ PASS |
| Checksum deterministic | ✅ PASS |
| Encode/decode roundtrip | ✅ PASS |
| Roundtrip type | ✅ PASS |
| Roundtrip seq | ✅ PASS |
| Roundtrip payload | ✅ PASS |
| Corrupted checksum rejected | ✅ PASS |
| Retransmission counter starts at 0 | ✅ PASS |
| File transfer succeeds | ✅ PASS |
| Mid-session ping | ✅ PASS |

### Interactive Mode

| Action | Output | Result |
|--------|--------|--------|
| Connect | `PONG received — connected!` | ✅ |
| `ping` | `PING/PONG OK (seq=1)` | ✅ |
| `Hello from Kaan` | `Pico: ECHO: Hello from Kaan` | ✅ |
| `stats` | `sent: 1, received: 1, retransmissions: 0, timeouts: 0` | ✅ |
| `quit` | `Disconnect sent, Cleaned up` | ✅ |

### File Transfer

```
py HostMachine.py --port COM8 --file HostMachine.py
```

| Metric | Value |
|--------|-------|
| File transferred | `HostMachine.py` |
| File size | 14,529 bytes |
| Total chunks | 63 |
| Chunk size | 232 bytes (last chunk: 145 bytes) |
| FILE_ACK received | 63 / 63 |
| Retransmissions | 0 |
| Result | ✅ File transfer complete |

---

## Challenges and Solutions

| Challenge | Solution |
|----------|----------|
| USB Serial initialisation delay | Wait for `stdio_usb_connected()` before entering main loop |
| Re-synchronising after garbage bytes | Scan for `0xCA 0xFE` magic bytes before reading packet header |
| Pico echo loop out of sync after PING | Return `0` (not `-1`) from `protocol_receive` when PING handled internally; `main.c` continues loop |
| Retransmission needed | Auto-retry up to 3 times with timeout before giving up |
| Python connect timing | Retry PING up to 15 times with 0.5s delay between attempts |
| File transfer: Pico didn't handle PKT_FILE | Added inline FILE packet handling inside `protocol_receive` |

---

## Reflection

### What I learned

- How binary packet framing works in practice — magic bytes allow the receiver to re-sync without resetting the connection, mirroring techniques used in HDLC, Modbus, and CAN bus.
- The importance of sequence numbers for detecting out-of-order delivery and enabling retransmission logic.
- How to design a protocol API that is agnostic to the underlying transport — making it possible to switch from USB Serial to UART by swapping only the internals of `protocol.c`.
- How to structure a Python class that mirrors a C API, keeping both sides conceptually consistent.

### What I would do differently

1. **Upgrade to CRC-16** — XOR checksums can miss multi-bit errors. CRC-16 reduces false-positive rate to 1-in-65536 with only 1 extra byte of overhead.
2. **Sliding window protocol** — stop-and-wait waits for one ACK before sending the next packet. A sliding window would allow multiple packets in flight, improving throughput significantly.
3. **Formal protocol specification** — a versioned `PROTOCOL_SPEC.md` similar to an RFC would make the protocol easier to maintain and extend.
4. **Encryption** — for real-world use, payloads should be encrypted (e.g. AES-128) to prevent eavesdropping on the serial link.

---

## Video Presentation: Questions and Answers

### Introduction

**Q: What was your project's goal?**

The goal was to individually design and implement a **custom binary communication protocol** (PicoComm) running on the Raspberry Pi Pico W and communicating with a host PC over USB Serial. The protocol defines a structured binary packet format with typed messages, sequence numbers, variable-length payloads, XOR error checking, stop-and-wait reliability with automatic retransmission, and file transfer support.

**Q: Who worked on it?**

Individual project — Kaan Karadag (Student ID: 23083770).

---

### Protocol Design

**Q: What kind of data does your protocol handle?**

PicoComm is a **general-purpose binary messaging protocol**. It handles typed messages (data, control, ping/pong, file transfer), variable-length binary payloads up to 256 bytes, and file transfer of arbitrary size via chunking. It is data-agnostic at the transport layer — the TYPE field allows the application to define payload meaning.

**Q: What packet structure did you choose and why?**

```
[0xCA][0xFE][TYPE][SEQ][LEN_LO][LEN_HI][PAYLOAD...][CHECKSUM]
```

- **2-byte magic `0xCA 0xFE`**: allows receiver to re-synchronise if bytes are lost, without resetting the connection.
- **TYPE field**: makes the protocol extensible — new message types (e.g. PKT_FILE) can be added without changing the structure.
- **SEQ field**: enables detection of out-of-order packets and supports the retransmission logic.
- **Variable-length payload with 2-byte LENGTH**: avoids wasting bytes on short messages while supporting up to 256 bytes.
- **XOR checksum**: simple and fast — sufficient for a short-range USB serial link with minimal overhead.

**Q: Did you implement any error checking or reliability features?**

Yes:
- **XOR checksum** — computed over TYPE, SEQ, LENGTH, and PAYLOAD. Mismatch causes the packet to be rejected.
- **Magic byte framing** — `0xCA 0xFE` allows the receiver to discard garbage bytes and re-sync to a valid packet boundary.
- **Stop-and-wait ACK** — every DATA and FILE packet requires an ACK before the next is sent.
- **Automatic retransmission** — up to 3 retries if no ACK received within 3 seconds.
- **Session statistics** — track retransmissions, timeouts, and checksum errors per session.

---

### Implementation

**Q: What transmission method did you use?**

**USB Serial (CDC)**. The Pico presents itself as a virtual serial port. On the PC side, Python's `pyserial` library opens the port and sends/receives raw bytes. No additional hardware or wiring required.

**Q: How did you structure your code on the Pico side?**

```
cap_protocol/
├── CMakeLists.txt
├── main.c          ← Echo server; calls protocol_init/connect, loops on protocol_receive
├── protocol.c      ← All protocol logic: encode, decode, checksum, send, receive, file transfer
├── protocol.h      ← Public API, Packet struct, PacketType enum, constants
├── flash_ops.c/h   ← Flash read/write helpers (used for file storage)
└── README.md       ← This file
```

`main.c` is kept minimal — it calls `protocol_init()`, `protocol_connect()`, then loops on `protocol_receive()` and echoes DATA packets back. All protocol logic is isolated in `protocol.c`.

**Q: How did you structure your Python code?**

```
Python/
├── HostMachine.py     ← CustomProtocol class + interactive demo + test suite
```

The `CustomProtocol` class mirrors the socket-like C API exactly, with `connect`, `send`, `receive`, `send_file`, `ping`, `disconnect`, `cleanup`.

---

### Demonstration

**Q: What does the demo show?**

1. Pico connected over USB; Python host connects with PING/PONG handshake.
2. Interactive mode: messages typed by the user are echoed back with `ECHO:` prefix.
3. `ping` command: mid-session PING/PONG confirms link alive.
4. `stats` command: session statistics showing packets sent/received with 0 retransmissions.
5. File transfer: `HostMachine.py` (14,529 bytes, 63 chunks) sent to Pico, all 63 FILE_ACKs received.
6. Automated test suite: `21/21 tests passing`.

---

### Testing and Challenges

**Q: How did you test your protocol?**

Two levels:
1. **Automated test suite** — `py HostMachine.py --port COM8 --test` runs 21 end-to-end tests covering handshake, echo, multi-packet, checksum validation, encode/decode roundtrip, file transfer, and mid-session ping.
2. **Interactive testing** — manual sessions testing normal operation, ping, stats, and file transfer.

**Q: What challenges did you encounter?**

The most significant challenge was the **Pico echo loop going out of sync after a PING** — if the Python side sent a PING mid-session, `protocol_receive` would return an error instead of handling the PING transparently. Fixed by handling PKT_PING inside `protocol_receive` and returning `0` instead of `-1`, so `main.c` continues the echo loop normally.

---

## Resources

- [Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
- [PySerial Documentation](https://pyserial.readthedocs.io/en/latest/shortintro.html)
- [UDP Packet Structure (inspiration)](https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Getting Started with Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)
