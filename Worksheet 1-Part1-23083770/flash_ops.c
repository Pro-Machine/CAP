/**
 * flash_ops.c
 * 
 * Flash memory operations for the Raspberry Pi Pico.
 * Implements safe read, write, and erase functions for the RP2040's
 * internal flash memory, along with structured data block support (Task 2).
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

#include "flash_ops.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

/* -----------------------------------------------------------------------
 * Constants
 * -----------------------------------------------------------------------
 * FLASH_TARGET_OFFSET: We reserve the first 1 MB of flash for the
 * program itself and use the upper portion for user data.
 * PICO_FLASH_SIZE_BYTES: Total flash on the RP2040 = 2 MB (2097152 bytes).
 * ----------------------------------------------------------------------- */
#define FLASH_TARGET_OFFSET (1024 * 1024)          /* 1 MB offset           */
#define FLASH_USABLE_SIZE   (PICO_FLASH_SIZE_BYTES - FLASH_TARGET_OFFSET)

/* -----------------------------------------------------------------------
 * Internal helper: align an offset down to the nearest sector boundary.
 * The RP2040 flash sector size is 4096 bytes (FLASH_SECTOR_SIZE).
 * ----------------------------------------------------------------------- */
static inline uint32_t sector_align(uint32_t offset) {
    return (offset / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
}

/* =======================================================================
 * TASK 1 – Basic Flash Memory Operations
 * ======================================================================= */

/**
 * flash_write_safe()
 *
 * Writes `data_len` bytes from `data` into flash at the given sector
 * `offset` (relative to FLASH_TARGET_OFFSET).
 *
 * Steps performed:
 *   1. Calculate absolute flash address.
 *   2. Bounds check – abort if write would exceed usable flash.
 *   3. Disable interrupts (required by the RP2040 flash API).
 *   4. Erase the target sector (flash can only be written after erasing).
 *   5. Write data using flash_range_program().
 *   6. Re-enable interrupts.
 *
 * @param offset    Sector-aligned byte offset from FLASH_TARGET_OFFSET.
 * @param data      Pointer to the data buffer to write.
 * @param data_len  Number of bytes to write (must be a multiple of 256).
 */
void flash_write_safe(uint32_t offset, const uint8_t *data, size_t data_len) {

    /* 1. Calculate absolute flash offset */
    uint32_t flash_offset = FLASH_TARGET_OFFSET + offset;

    /* 2. Bounds checking */
    if (flash_offset + data_len > PICO_FLASH_SIZE_BYTES) {
        printf("[ERROR] flash_write_safe: write out of bounds "
               "(offset=0x%08X, len=%u)\n", flash_offset, (unsigned)data_len);
        return;
    }

    /* 3. Disable interrupts – mandatory before any flash XIP operation */
    uint32_t ints = save_and_disable_interrupts();

    /* 4. Erase the flash sector(s) covered by this write.
     *    flash_range_erase() requires the offset to be sector-aligned
     *    and the size to be a multiple of FLASH_SECTOR_SIZE. */
    uint32_t erase_offset = sector_align(flash_offset);
    uint32_t erase_size   = FLASH_SECTOR_SIZE;   /* erase at least one sector */
    flash_range_erase(erase_offset, erase_size);

    /* 5. Program the data.
     *    flash_range_program() requires the offset to be 256-byte aligned
     *    and size to be a multiple of FLASH_PAGE_SIZE (256 bytes).
     *    Round up data_len and pad the remainder with 0xFF (erased state). */
    size_t padded_len = ((data_len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE)
                        * FLASH_PAGE_SIZE;
    uint8_t *page_buf = (uint8_t *)malloc(padded_len);
    if (page_buf == NULL) {
        restore_interrupts(ints);
        printf("[ERROR] flash_write_safe: malloc failed\n");
        return;
    }
    memset(page_buf, 0xFF, padded_len);   /* fill with erased-state value */
    memcpy(page_buf, data, data_len);     /* copy actual data into front  */
    flash_range_program(flash_offset, page_buf, padded_len);
    free(page_buf);

    /* 6. Re-enable interrupts */
    restore_interrupts(ints);

    printf("[INFO] flash_write_safe: wrote %u bytes at offset 0x%08X\n",
           (unsigned)data_len, flash_offset);
}

/**
 * flash_read_safe()
 *
 * Reads `buffer_len` bytes from flash at the given sector `offset`
 * (relative to FLASH_TARGET_OFFSET) into `buffer`.
 *
 * On the RP2040 the flash is memory-mapped starting at XIP_BASE, so
 * reads are simply a memcpy – no interrupts need to be disabled.
 *
 * @param offset      Byte offset from FLASH_TARGET_OFFSET.
 * @param buffer      Destination buffer (must be at least buffer_len bytes).
 * @param buffer_len  Number of bytes to read.
 */
void flash_read_safe(uint32_t offset, uint8_t *buffer, size_t buffer_len) {

    /* 1. Calculate absolute flash offset */
    uint32_t flash_offset = FLASH_TARGET_OFFSET + offset;

    /* 2. Bounds checking */
    if (flash_offset + buffer_len > PICO_FLASH_SIZE_BYTES) {
        printf("[ERROR] flash_read_safe: read out of bounds "
               "(offset=0x%08X, len=%u)\n", flash_offset, (unsigned)buffer_len);
        return;
    }

    /* 3. Read via the XIP memory-mapped window */
    memcpy(buffer, (void *)(XIP_BASE + flash_offset), buffer_len);

    printf("[INFO] flash_read_safe: read %u bytes from offset 0x%08X\n",
           (unsigned)buffer_len, flash_offset);
}

/**
 * flash_erase_safe()
 *
 * Erases the flash sector that contains the given `offset`
 * (relative to FLASH_TARGET_OFFSET).
 *
 * @param offset  Byte offset from FLASH_TARGET_OFFSET.
 */
void flash_erase_safe(uint32_t offset) {

    /* 1. Calculate absolute flash offset, aligned to sector boundary */
    uint32_t flash_offset = sector_align(FLASH_TARGET_OFFSET + offset);

    /* 2. Bounds checking */
    if (flash_offset + FLASH_SECTOR_SIZE > PICO_FLASH_SIZE_BYTES) {
        printf("[ERROR] flash_erase_safe: erase out of bounds "
               "(offset=0x%08X)\n", flash_offset);
        return;
    }

    /* 3. Disable interrupts */
    uint32_t ints = save_and_disable_interrupts();

    /* 4. Erase one sector (4096 bytes) */
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);

    /* 5. Re-enable interrupts */
    restore_interrupts(ints);

    printf("[INFO] flash_erase_safe: erased sector at offset 0x%08X\n",
           flash_offset);
}


/* =======================================================================
 * TASK 2 – Structured Flash Memory Write
 * =======================================================================
 *
 * Instead of writing raw bytes we wrap the payload in a flash_data
 * struct that carries metadata:
 *
 *   typedef struct {
 *       uint32_t write_count;   // how many times this sector has been written
 *       uint8_t *data_ptr;      // pointer to actual payload bytes
 *       size_t   data_len;      // length of payload in bytes
 *   } flash_data;
 *
 * When persisting to flash we serialise the struct into a flat buffer:
 *   [ write_count (4 bytes) | data_len (4 bytes) | payload (data_len bytes) ]
 *
 * The read function deserialises this back and prints the metadata.
 * ======================================================================= */

/**
 * flash_write_struct()
 *
 * Reads any existing write_count from flash, increments it, then
 * serialises the flash_data struct and writes it to the given sector.
 *
 * @param offset     Sector-aligned byte offset from FLASH_TARGET_OFFSET.
 * @param fdata      Pointer to a populated flash_data struct.
 */
void flash_write_struct(uint32_t offset, flash_data *fdata) {

    if (fdata == NULL || fdata->data_ptr == NULL) {
        printf("[ERROR] flash_write_struct: NULL pointer supplied\n");
        return;
    }

    /* --- Read back any previous write_count from flash --- */
    uint8_t prev_buf[8];   /* write_count (4) + data_len (4) */
    flash_read_safe(offset, prev_buf, sizeof(prev_buf));

    uint32_t prev_count;
    memcpy(&prev_count, prev_buf, sizeof(uint32_t));

    /* If the sector was never written (all 0xFF after erase) treat as 0 */
    if (prev_count == 0xFFFFFFFF) {
        prev_count = 0;
    }

    /* Increment write counter */
    fdata->write_count = prev_count + 1;

    /* --- Serialise into a page-aligned buffer ---
     * Layout: | write_count (4B) | data_len (4B) | payload |
     * Total must be a multiple of FLASH_PAGE_SIZE (256 bytes).          */
    size_t payload_len  = fdata->data_len;
    size_t header_len   = sizeof(uint32_t) + sizeof(uint32_t);  /* 8 bytes */
    size_t raw_total    = header_len + payload_len;

    /* Round up to nearest FLASH_PAGE_SIZE */
    size_t buf_len = ((raw_total + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE)
                     * FLASH_PAGE_SIZE;

    uint8_t *buf = (uint8_t *)malloc(buf_len);
    if (buf == NULL) {
        printf("[ERROR] flash_write_struct: malloc failed\n");
        return;
    }
    memset(buf, 0xFF, buf_len);   /* fill with 0xFF (erased state) */

    /* Copy header fields */
    memcpy(buf,                        &fdata->write_count, sizeof(uint32_t));
    memcpy(buf + sizeof(uint32_t),     &payload_len,        sizeof(uint32_t));
    /* Copy payload */
    memcpy(buf + header_len,           fdata->data_ptr,     payload_len);

    /* Write to flash */
    flash_write_safe(offset, buf, buf_len);

    free(buf);

    printf("[INFO] flash_write_struct: write_count=%u, payload_len=%u\n",
           (unsigned)fdata->write_count, (unsigned)payload_len);
}

/**
 * flash_read_struct()
 *
 * Reads a serialised flash_data record from flash, prints its metadata,
 * and copies the payload into the buffer pointed to by fdata->data_ptr.
 *
 * The caller must:
 *   1. Provide a valid flash_data pointer.
 *   2. Set fdata->data_ptr to a pre-allocated buffer of at least
 *      fdata->data_len bytes, OR pass data_len = 0 to auto-detect.
 *
 * @param offset  Sector-aligned byte offset from FLASH_TARGET_OFFSET.
 * @param fdata   Output struct – populated by this function.
 */
void flash_read_struct(uint32_t offset, flash_data *fdata) {

    if (fdata == NULL) {
        printf("[ERROR] flash_read_struct: NULL fdata pointer\n");
        return;
    }

    /* Read the 8-byte header first */
    uint8_t header[8];
    flash_read_safe(offset, header, sizeof(header));

    uint32_t write_count;
    uint32_t data_len;
    memcpy(&write_count, header,                    sizeof(uint32_t));
    memcpy(&data_len,    header + sizeof(uint32_t), sizeof(uint32_t));

    /* Sanity check – if the sector was never written */
    if (write_count == 0xFFFFFFFF || data_len == 0xFFFFFFFF) {
        printf("[WARN] flash_read_struct: sector appears blank (never written)\n");
        fdata->write_count = 0;
        fdata->data_len    = 0;
        return;
    }

    /* Print metadata */
    printf("--- Flash Struct Metadata ---\n");
    printf("  Write count : %u\n",   (unsigned)write_count);
    printf("  Payload len : %u bytes\n", (unsigned)data_len);

    fdata->write_count = write_count;
    fdata->data_len    = data_len;

    /* Read payload if caller provided a buffer */
    if (fdata->data_ptr != NULL && data_len > 0) {
        uint32_t payload_offset = offset + sizeof(uint32_t) + sizeof(uint32_t);
        flash_read_safe(payload_offset, fdata->data_ptr, data_len);

        /* Print payload as a null-terminated string (best-effort) */
        ((char *)fdata->data_ptr)[data_len] = '\0';
        printf("  Payload     : \"%s\"\n", (char *)fdata->data_ptr);
    }
    printf("-----------------------------\n");
}
