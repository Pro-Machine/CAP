/**
 * flash_ops.h
 *
 * Public API for flash memory operations on the Raspberry Pi Pico.
 * Include this header in main.c (or wherever your CLI logic lives).
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

#ifndef FLASH_OPS_H
#define FLASH_OPS_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

/* -----------------------------------------------------------------------
 * Task 2 – Structured data type
 * ----------------------------------------------------------------------- */

/**
 * flash_data
 *
 * Metadata wrapper stored alongside user data in flash.
 *
 * @field write_count  Number of times this sector has been written to.
 *                     Automatically incremented by flash_write_struct().
 * @field data_ptr     Pointer to the payload buffer (in RAM).
 * @field data_len     Length of the payload in bytes.
 */
typedef struct {
    uint32_t write_count;   /* Counter for the number of writes to this sector */
    uint8_t *data_ptr;      /* Pointer to the actual data to be stored          */
    size_t   data_len;      /* Length of the data in bytes                      */
} flash_data;

/* -----------------------------------------------------------------------
 * Task 1 – Basic flash operations
 * ----------------------------------------------------------------------- */

/**
 * Write raw bytes to a flash sector.
 * @param offset    Byte offset from FLASH_TARGET_OFFSET (sector-aligned).
 * @param data      Source data buffer.
 * @param data_len  Number of bytes to write (multiple of 256).
 */
void flash_write_safe(uint32_t offset, const uint8_t *data, size_t data_len);

/**
 * Read bytes from flash into a buffer.
 * @param offset      Byte offset from FLASH_TARGET_OFFSET.
 * @param buffer      Destination buffer.
 * @param buffer_len  Number of bytes to read.
 */
void flash_read_safe(uint32_t offset, uint8_t *buffer, size_t buffer_len);

/**
 * Erase the flash sector at the given offset.
 * @param offset  Byte offset from FLASH_TARGET_OFFSET.
 */
void flash_erase_safe(uint32_t offset);

/* -----------------------------------------------------------------------
 * Task 2 – Structured flash operations
 * ----------------------------------------------------------------------- */

/**
 * Write a flash_data struct (metadata + payload) to flash.
 * Automatically reads and increments the existing write_count.
 */
void flash_write_struct(uint32_t offset, flash_data *fdata);

/**
 * Read a flash_data struct from flash and print its metadata.
 * Caller must set fdata->data_ptr to a buffer large enough for the payload.
 */
void flash_read_struct(uint32_t offset, flash_data *fdata);

#endif /* FLASH_OPS_H */
