# Worksheet 1 – Part 2: Filesystem on Raspberry Pi Pico

> **Module**: Communications and Protocols (UFCFVR-15-3)  
> **Deadline**: Before 14:00 on 24th April 2026  
> **Author**: Kaan Karadag — Student ID: 23083770  

---

## Table of Contents

1. [Overview](#overview)
2. [Implementation Choice](#implementation-choice)
3. [Architecture](#architecture)
4. [Flash Memory Layout](#flash-memory-layout)
5. [API Reference](#api-reference)
6. [Design Decisions](#design-decisions)
7. [Error Handling](#error-handling)
8. [Testing](#testing)
9. [Challenges and Solutions](#challenges-and-solutions)
10. [Resources](#resources)

---

## Overview

This worksheet builds on the raw flash operations from Part 1 to implement a **filesystem abstraction layer** on top of the Raspberry Pi Pico's flash memory. A filesystem provides a structured, file-centric interface to flash storage — allowing code to open, read, write, seek, and close named files without needing to know about sector offsets or erase boundaries.

Two implementation paths were available:

| Path | Complexity | Files per Block | Recommended For |
|------|-----------|-----------------|-----------------|
| Simple Filesystem | Lower | Exactly 1 | Understanding the fundamentals |
| Advanced FAT Filesystem | Higher | Multiple | Demonstrating advanced data structures |

---

## Implementation Choice

### ✅ Chosen: Simple Filesystem

The Simple Filesystem was selected as the implementation path. Each flash sector maps 1:1 to a single file, eliminating the need for a FAT table or block chain management. A persistent file table is stored in sector 0 with a magic number (`0xCAFEF00D`) to detect first-boot vs. already-formatted flash.

This approach is easier to reason about, less prone to corruption, and well-suited to the fixed-size data payloads typical in embedded systems. It was extended with bonus operations (`fs_delete`, `fs_list`, `fs_rename`) and a full 21-test automated test suite.

---

## Architecture

```
Flash Memory (user region, starting at FLASH_TARGET_OFFSET)
┌─────────────────────────┬────────────────┬────────────────┬─────┐
│  Sector 0               │  Sector 1      │  Sector 2      │ ... │
│  File Table             │  File 0 data   │  File 1 data   │     │
│  (magic + FileEntry[10])│  4KB           │  4KB           │     │
└─────────────────────────┴────────────────┴────────────────┴─────┘
```

Sector 0 stores the persistent file table: a `0xCAFEF00D` magic header followed by an array of up to 10 `FileEntry` structs (filename, size, in_use flag). Sectors 1–10 each hold one file's raw data. On first boot, the magic number is absent so the filesystem initialises a blank table and writes it to flash.

---

## Flash Memory Layout

| Region | Offset | Description |
|--------|--------|-------------|
| File Table | `FLASH_TARGET_OFFSET + 0` | Magic header + FileEntry[10] |
| File 0 data | `FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE` | Up to 4096 bytes |
| File 1 data | `FLASH_TARGET_OFFSET + 2 * FLASH_SECTOR_SIZE` | Up to 4096 bytes |
| ... | ... | ... |
| File 9 data | `FLASH_TARGET_OFFSET + 10 * FLASH_SECTOR_SIZE` | Up to 4096 bytes |

Each sector is `FLASH_SECTOR_SIZE` bytes (4096 bytes / 4 KB). Maximum 10 files, maximum 4096 bytes per file.

---

## Data Structures

### FileEntry (stored in flash table)

```c
typedef struct {
    char     filename[256]; // Path/name of the file
    uint32_t size;          // Current file size in bytes
    bool     in_use;        // Whether this slot is occupied
} FileEntry;
```

### FS_FILE (in-RAM file handle)

```c
typedef struct {
    FileEntry *entry;    // Pointer to the file's table entry (in RAM)
    uint32_t   position; // Current read/write cursor position
} FS_FILE;
```

---

## API Reference

### Core Functions (Required)

#### `fs_open`

```c
FS_FILE *fs_open(const char *path, const char *mode);
```

| Parameter | Description |
|-----------|-------------|
| `path` | Filename string |
| `mode` | `"r"` = read only, `"w"` = write/create (erases existing), `"a"` = append |

Returns a pointer to an `FS_FILE` handle, or `NULL` on failure (e.g. file not found in `"r"` mode, or filesystem full).

---

#### `fs_close`

```c
void fs_close(FS_FILE *file);
```

Persists the updated file table to flash sector 0 and releases the handle. After calling `fs_close`, the pointer must not be used.

---

#### `fs_read`

```c
int fs_read(FS_FILE *file, void *buffer, int size);
```

Reads up to `size` bytes from the current file position into `buffer`. Advances the position by bytes actually read. Returns bytes read, or `-1` on error.

---

#### `fs_write`

```c
int fs_write(FS_FILE *file, const void *buffer, int size);
```

Writes `size` bytes from `buffer` into the file at the current position. Uses a full sector read-modify-write cycle (required by flash hardware). Returns bytes written, or `-1` on error (e.g. write would exceed sector size).

---

#### `fs_seek`

```c
int fs_seek(FS_FILE *file, long offset, int whence);
```

| `whence` value | Behaviour |
|---------------|-----------|
| `SEEK_SET` (0) | Set position to `offset` bytes from start |
| `SEEK_CUR` (1) | Move position by `offset` bytes from current |
| `SEEK_END` (2) | Set position to end of file plus `offset` |

Returns `0` on success, `-1` on error (e.g. out-of-bounds position).

---

### Extended Functions (Bonus)

```c
int  fs_delete(const char *path);                        // Delete file, free sector
void fs_list(void);                                      // Print all files and sizes
int  fs_rename(const char *old_path, const char *new);  // Rename a file
void fs_run_tests(void);                                 // Run automated test suite
```

---

### CLI Commands

| Command | Syntax | Example |
|---------|--------|---------|
| Write | `FS_WRITE <file> "<data>"` | `FS_WRITE hello.txt "Hello World"` |
| Read | `FS_READ <file> <bytes>` | `FS_READ hello.txt 11` |
| Append | `FS_APPEND <file> "<data>"` | `FS_APPEND hello.txt " More"` |
| Seek | `FS_SEEK <file> <offset>` | `FS_SEEK hello.txt 6` |
| Delete | `FS_DELETE <file>` | `FS_DELETE hello.txt` |
| List | `FS_LIST` | `FS_LIST` |
| Rename | `FS_RENAME <old> <new>` | `FS_RENAME hello.txt bye.txt` |
| Test | `FS_TEST` | `FS_TEST` |

---

## Design Decisions

### Why Simple Filesystem?

- Eliminates the complexity of FAT management — each file's sector is computed directly from its table index with no chain traversal.
- The persistent file table in sector 0 with magic header `0xCAFEF00D` gives files named identity and survives reboots.
- Damage is isolated to individual sectors — a corrupted FAT can render an entire filesystem unreadable; here, only the affected file is impacted.
- The read-modify-write cycle (read sector into RAM → modify → erase → reprogram) is straightforward to implement and test.
- Well-suited to fixed-size, small payloads typical in embedded systems (configuration, sensor logs).

### Seek Strategy

`SEEK_END` uses the `size` field stored in the `FileEntry` struct, which is updated by `fs_write` and persisted by `fs_close`.

### Erase-on-Write

Since flash sectors must be erased before writing, `fs_write` performs a full **read-modify-write**: read the entire 4KB sector into a RAM buffer, patch in the new data at `position`, erase the sector, then reprogram it. This is standard practice for flash-backed filesystems.

### Persistence

The file table is persisted to flash on every `fs_close` call. On boot, `fs_init` reads sector 0 — if the magic number matches `0xCAFEF00D`, the file table is restored; otherwise a blank table is initialised. This means files survive power cycles.

---

## Error Handling

| Error Condition | Handling Strategy |
|----------------|------------------|
| `fs_open` on non-existent file in `"r"` mode | Return `NULL`, print `[FS] file not found` |
| `fs_write` would exceed sector size (4096 bytes) | Return `-1`, print warning |
| Filesystem full (10 files) | Return `NULL` from `fs_open`, print `[FS] filesystem full` |
| `fs_read` past end of file | Return `0` (EOF) |
| `fs_seek` to out-of-bounds position | Return `-1`, position unchanged |
| `NULL` file handle passed | Guard check at top of each function, return `-1` |
| `fs_rename` target name already exists | Return `-1`, print error |

---

## Testing

### Automated Test Suite (`FS_TEST`) — 21 / 21 passed

Run via: `FS_TEST` in the serial CLI.

| Test | Result |
|------|--------|
| `fs_open` write mode | ✅ PASS |
| `fs_write` returns correct byte count | ✅ PASS |
| `fs_open` read mode | ✅ PASS |
| `fs_read` returns correct byte count | ✅ PASS |
| `fs_read` data matches written data | ✅ PASS |
| `fs_open` append mode | ✅ PASS |
| Append extends file correctly | ✅ PASS |
| `fs_seek` SEEK_SET returns 0 | ✅ PASS |
| `fs_seek` SEEK_SET reads correct data | ✅ PASS |
| `fs_seek` SEEK_END reads last 4 bytes | ✅ PASS |
| Overwrite resets file size | ✅ PASS |
| Overwrite stores new data | ✅ PASS |
| `fs_rename` returns 0 | ✅ PASS |
| Renamed file is accessible | ✅ PASS |
| Original name no longer exists | ✅ PASS |
| `fs_delete` returns 0 | ✅ PASS |
| Deleted file not accessible | ✅ PASS |
| Open non-existent file returns NULL | ✅ PASS |
| Seek out of bounds returns -1 | ✅ PASS |
| Can create multiple files | ✅ PASS |
| Multiple files store independently | ✅ PASS |

### Manual Test Results

| Command | Output | Result |
|---------|--------|--------|
| `FS_LIST` (empty) | `(empty) — 0 / 10 slots used` | ✅ |
| `FS_WRITE hello.txt "Hello World"` | `Wrote 11 bytes to 'hello.txt'` | ✅ |
| `FS_READ hello.txt 11` | `Data: Hello World` | ✅ |
| `FS_APPEND hello.txt " More data"` | File grows to 21 bytes | ✅ |
| `FS_READ hello.txt 21` | `Data: Hello World More data` | ✅ |
| `FS_SEEK hello.txt 6` | `Data from offset: World More data` | ✅ |
| `FS_DELETE hello.txt` | `Deleted 'hello.txt'` | ✅ |
| `FS_RENAME test.txt renamed.txt` | `Renamed 'test.txt' -> 'renamed.txt'` | ✅ |

### Persistence Test

The Pico was fully unplugged and reconnected (cold boot). On reconnecting via PuTTY:

- `FS_LIST` correctly showed files written before the power cycle.
- `FS_READ` on those files returned correct data.
- This confirms both the file table and sector data are persisted non-volatilely to flash.

---

## Challenges and Solutions

| Challenge | Solution |
|----------|----------|
| Flash must be erased before write | Always call `flash_range_erase` before `flash_range_program` |
| Interrupts cannot fire during erase/write | Wrap operations with `save_and_disable_interrupts` / `restore_interrupts` |
| Partial updates require read-modify-write | Buffer entire sector in RAM, modify, erase, reprogram |
| Pointer values cannot be stored in flash | Struct fields serialised as raw bytes; pointers reconstructed from table index on read |
| File table must survive power cycles | File table written to sector 0 with magic header on every `fs_close` |

---

## Resources

- [Pico SDK Flash API](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- Starter repository: https://gitlab.uwe.ac.uk/f4-barnes/cap_fs
