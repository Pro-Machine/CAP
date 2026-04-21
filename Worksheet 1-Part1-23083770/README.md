# Worksheet 1 – Part 1: Flash Storage on Raspberry Pi Pico

> **Module**: Communications and Protocols (UFCFVR-15-3)  
> **Deadline**: Before 14:00 on 24th April 2026  
> **Author**: Kaan Karadag — Student ID: 23083770  

---

## Table of Contents

1. [Overview](#overview)
2. [Background: Flash vs RAM](#background-flash-vs-ram)
3. [Flash Memory Layout on the Pico](#flash-memory-layout-on-the-pico)
4. [Getting Started](#getting-started)
5. [Task 1 – Flash Memory Operations](#task-1--flash-memory-operations)
   - [Implementation Details](#implementation-details)
   - [flash_write_safe](#flash_write_safe)
   - [flash_read_safe](#flash_read_safe)
   - [flash_erase_safe](#flash_erase_safe)
   - [CLI Commands](#cli-commands)
6. [Task 2 – Structured Flash Memory Write](#task-2--structured-flash-memory-write)
   - [Struct Design](#struct-design)
   - [Modified Write Function](#modified-write-function)
   - [Modified Read Function](#modified-read-function)
7. [Safety Considerations](#safety-considerations)
8. [Testing](#testing)
9. [Code Quality & Documentation Standards](#code-quality--documentation-standards)
10. [Resources](#resources)

---

## Overview

This worksheet explores **persistent data storage** on the Raspberry Pi Pico using its built-in 2MB flash memory. Unlike RAM, which is volatile (data lost on power-off), flash memory retains data across reboots and full power cycles — making it essential for embedded systems that need to store configuration, state, or user data.

The goal is to implement safe, interrupt-aware read, write, and erase operations on the Pico's flash memory, and extend these operations to handle **structured data blocks** including metadata such as write counters.

---

## Background: Flash vs RAM

| Feature        | RAM                                    | Flash Memory                        |
|----------------|----------------------------------------|-------------------------------------|
| Volatility     | Volatile (data lost on power-off)      | Non-volatile (data persists)        |
| Access Speed   | Faster (direct CPU access)             | Slower (XIP or programmatic)        |
| Write Method   | Direct byte write                      | Sector erase + page program         |
| Use Case       | Runtime variables, stack, heap         | Firmware, persistent storage        |
| Size on Pico   | 264 KB                                 | 2 MB                                |

The Pico's RP2040 also has registers capable of surviving a soft reboot, but these do not persist through a full power-down. True persistent storage requires flash.

---

## Flash Memory Layout on the Pico

```
┌─────────────────────────────────────┐  0x10000000 (XIP_BASE)
│         Bootloader (Stage 2)        │  First 256 bytes
├─────────────────────────────────────┤
│        Application / Firmware       │  Variable size
├─────────────────────────────────────┤  FLASH_TARGET_OFFSET (1MB)
│     ← Available for User Data →     │  Unused flash region
│                                     │  (sector-aligned, 4KB each)
└─────────────────────────────────────┘  0x10200000 (2MB end)
```

`FLASH_TARGET_OFFSET` is set to 1MB — the boundary above the application code where safe user data storage begins. All offset calculations in the implementation **add `FLASH_TARGET_OFFSET`** to a logical offset to compute the physical flash address.

Flash is accessed for reading via **XIP (Execute In Place)** — you can read flash like regular memory starting at `XIP_BASE`. Writing and erasing require dedicated SDK functions and **interrupts must be disabled** during these operations.

---

## Getting Started

Clone the template repository:

```bash
git clone https://gitlab.uwe.ac.uk/f4-barnes/cap_template
cd cap_template
mkdir build && cd build
export PICO_SDK_PATH=~/pico/pico-sdk
cmake -DPICO_BOARD=pico_w ..
make -j$(nproc)
```

Flash the resulting `.uf2` to the Pico by holding BOOTSEL and dragging the file onto the mounted RPI-RP2 drive.

Connect via a serial terminal (e.g., PuTTY) at **115200 baud** with **local echo forced on** to interact with the CLI.

---

## Task 1 – Flash Memory Operations

All three functions are implemented inside `flash_ops.c`. They form a safe abstraction over the low-level Pico SDK flash API.

### Implementation Details

Every function follows the same three-step safety pattern:

1. **Calculate the physical address** — add `FLASH_TARGET_OFFSET` to the logical byte offset.
2. **Bounds check** — verify the operation does not exceed the allocated flash region to prevent overwriting application code.
3. **Disable/restore interrupts** — flash write/erase operations must run with interrupts disabled to prevent DMA or IRQ conflicts mid-operation.

---

### `flash_write_safe`

```c
void flash_write_safe(uint32_t offset, const uint8_t *data, size_t data_len);
```

**Steps:**

1. Compute `flash_offset = FLASH_TARGET_OFFSET + offset`
2. Bounds check: if `flash_offset + data_len > PICO_FLASH_SIZE_BYTES`, print error and return
3. Disable interrupts: `uint32_t ints = save_and_disable_interrupts();`
4. Erase the target sector: `flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);`
5. Program the data: `flash_range_program(flash_offset, data, data_len);`
6. Restore interrupts: `restore_interrupts(ints);`

> **Why erase before write?** Flash memory can only transition bits from `1` → `0` during a write. Erasing resets all bits in a sector back to `1`, making it possible to write any new value. Skipping the erase step leads to corrupted data.

---

### `flash_read_safe`

```c
void flash_read_safe(uint32_t offset, uint8_t *buffer, size_t buffer_len);
```

**Steps:**

1. Compute `flash_offset = FLASH_TARGET_OFFSET + offset`
2. Bounds check: if `flash_offset + buffer_len > PICO_FLASH_SIZE_BYTES`, print error and return
3. Copy data from flash to buffer using XIP:
   ```c
   memcpy(buffer, (void *)(XIP_BASE + flash_offset), buffer_len);
   ```

> **No interrupt disable needed for reads** — reading flash via XIP is a standard memory-mapped operation and does not conflict with normal program execution.

---

### `flash_erase_safe`

```c
void flash_erase_safe(uint32_t offset);
```

**Steps:**

1. Compute `flash_offset` aligned to the nearest sector boundary
2. Bounds check: if `flash_offset + FLASH_SECTOR_SIZE > PICO_FLASH_SIZE_BYTES`, print error and return
3. Disable interrupts
4. Erase sector: `flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);`
5. Restore interrupts

---

### CLI Commands

The template provides a serial CLI with five commands (Task 1 basic + Task 2 structured):

| Command | Syntax | Example |
|--------|--------|---------|
| Write (raw) | `FLASH_WRITE <offset> "<data>"` | `FLASH_WRITE 0 "Hello World"` |
| Read (raw) | `FLASH_READ <offset> <length>` | `FLASH_READ 0 11` |
| Erase | `FLASH_ERASE <offset>` | `FLASH_ERASE 0` |
| Write (struct) | `FLASH_WRITE_STRUCT <offset> "<data>"` | `FLASH_WRITE_STRUCT 0 "Hello"` |
| Read (struct) | `FLASH_READ_STRUCT <offset>` | `FLASH_READ_STRUCT 0` |

Where `<offset>` is a byte offset from `FLASH_TARGET_OFFSET`.

---

## Task 2 – Structured Flash Memory Write

### Struct Design

Rather than writing raw bytes, Task 2 stores a structured block containing metadata alongside the actual data payload:

```c
typedef struct {
    uint32_t write_count;   // Number of times this sector has been written to
    uint8_t  *data_ptr;     // Pointer to the actual data to be stored
    size_t    data_len;     // Length of the data in bytes
} flash_data;
```

**Design Rationale:**
- `write_count` acts as a wear-levelling indicator and tracks sector usage over time.
- `data_ptr` + `data_len` provide a flexible payload mechanism.
- The struct is serialised into a contiguous byte buffer before calling `flash_range_program`, since pointers are not meaningful across power cycles.

### Modified Write Function

Before writing:
1. **Read existing metadata** from the target sector to retrieve the current `write_count`.
2. **Increment `write_count`** (initialise to `1` if the sector was blank — detected by `0xFFFFFFFF` erased state).
3. **Serialise the struct** — copy `write_count`, `data_len`, and raw data bytes into a flat `uint8_t` buffer padded to `FLASH_PAGE_SIZE`.
4. Call `flash_write_safe` with the serialised buffer.

```c
// Serialised layout in flash:
// [4 bytes: write_count] [4 bytes: data_len] [data_len bytes: data]
```

### Modified Read Function

After reading raw bytes from flash:
1. **Deserialise** — extract `write_count` and `data_len` from the first 8 bytes.
2. **Display metadata** — print `write_count` to show how many times this sector has been written.
3. **Display data** — print the payload as a null-terminated string.

---

## Safety Considerations

| Risk | Mitigation |
|------|-----------|
| Writing into application code region | `FLASH_TARGET_OFFSET` (1MB) enforced in every bounds check |
| Flash corruption during IRQ | `save_and_disable_interrupts()` wraps all erase/write calls |
| Write without prior erase | `flash_range_erase` always called before `flash_range_program` |
| Buffer overflow | Bounds checking compares `offset + length` against flash limit |
| Pointer serialisation | `data_ptr` is dereferenced before storage; raw bytes stored in flash, not pointer values |

---

## Testing

All tests were run on real hardware via PuTTY serial terminal (115200 baud, local echo on).

| Test Case | Command | Result |
|-----------|---------|--------|
| Write and read back a short string | `FLASH_WRITE 0 "Hello World"` → `FLASH_READ 0 11` | `Data: Hello World` ✅ |
| Erase then read confirms erased state | `FLASH_ERASE 0` → `FLASH_READ 0 11` | Returns `▒▒▒▒` (0xFF — correct erased state) ✅ |
| Structured write — first write | `FLASH_ERASE 0` → `FLASH_WRITE_STRUCT 0 "Hello"` | `write_count=1, payload_len=5` ✅ |
| Structured read | `FLASH_READ_STRUCT 0` | `Write count: 1, Payload: "Hello"` ✅ |
| Structured write — second write, count increments | `FLASH_WRITE_STRUCT 0 "Hello"` → `FLASH_READ_STRUCT 0` | `write_count=2` ✅ |
| Structured write — third write, count increments | `FLASH_WRITE_STRUCT 0 "Hello"` → `FLASH_READ_STRUCT 0` | `write_count=3` ✅ |

---

## Code Quality & Documentation Standards

- Every function has a **header comment** describing parameters, return values, and behaviour.
- Constants use `#define` (`FLASH_TARGET_OFFSET`, `FLASH_SECTOR_SIZE`) rather than magic numbers.
- Code is organised into `flash_ops.c` / `flash_ops.h`.
- Consistent 4-space indentation throughout.
- All `printf` debug messages are prefixed with `[INFO]` or `[ERROR]` for easy filtering.

---

## Resources

- [Pico SDK Flash API Reference](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Getting Started with Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)
- Template repository: https://gitlab.uwe.ac.uk/f4-barnes/cap_template
