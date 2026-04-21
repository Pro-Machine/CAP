#include "flash_ops.h"
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define FLASH_TARGET_OFFSET (256 * 1024) // Offset where user data starts (256KB into flash)
#define FLASH_SIZE PICO_FLASH_SIZE_BYTES // Total flash size available

// Function: flash_write_safe
// Writes data to flash memory at a specified offset, ensuring safety checks.
//
// Parameters:
// - offset: The offset from FLASH_TARGET_OFFSET where data is to be written.
// - data: Pointer to the data to be written.
// - data_len: Length of the data to be written.
//
// Note: This function erases the flash sector before writing new data.
void flash_write_safe(uint32_t offset, const uint8_t *data, size_t data_len) {

    // Calculate absolute flash offset
    uint32_t flash_offset = FLASH_TARGET_OFFSET + offset;

    // Check if the write operation is within bounds
    if (flash_offset + data_len > FLASH_TARGET_OFFSET + FLASH_SIZE || flash_offset < FLASH_TARGET_OFFSET) {
        printf("\nError: Write out of bounds\n");
        return;
    }

    // Disable interrupts for a safe flash operation
    uint32_t ints = save_and_disable_interrupts();

    // Erase the flash sector before writing
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);

    // Write data to flash
    flash_range_program(flash_offset, data, data_len);

    // Restore interrupts
    restore_interrupts(ints);
}

// Function: flash_read_safe
// Reads data from flash memory into a buffer.
//
// Parameters:
// - offset: The offset from FLASH_TARGET_OFFSET where data is to be read.
// - buffer: Pointer to the buffer where read data will be stored.
// - buffer_len: Number of bytes to read.
//
// Note: The function performs bounds checking to ensure safe access.
void flash_read_safe(uint32_t offset, uint8_t *buffer, size_t buffer_len) {

    // Calculate absolute flash offset
    uint32_t flash_offset = FLASH_TARGET_OFFSET + offset;

    // Check if the read operation is within bounds
    if (flash_offset + buffer_len > FLASH_TARGET_OFFSET + FLASH_SIZE || flash_offset < FLASH_TARGET_OFFSET) {
        printf("\nError: Read out of bounds\n");
        return;
    }

    // Perform the memory copy from flash to buffer
    memcpy(buffer, (void *)(XIP_BASE + flash_offset), buffer_len);
}

// Function: flash_erase_safe
// Erases a sector of the flash memory.
//
// Parameters:
// - offset: The offset from FLASH_TARGET_OFFSET where the erase starts.
//
// Note: This function checks that the operation stays within valid bounds.
void flash_erase_safe(uint32_t offset) {

    // Calculate absolute flash offset
    uint32_t flash_offset = FLASH_TARGET_OFFSET + offset;

    // Check if the erase operation is within bounds
    if (flash_offset >= FLASH_TARGET_OFFSET + FLASH_SIZE || flash_offset < FLASH_TARGET_OFFSET) {
        printf("Error: Erase out of bounds\n");
        return;
    }

    // Disable interrupts for a safe flash operation
    uint32_t ints = save_and_disable_interrupts();

    // Erase the flash sector
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);

    // Restore interrupts
    restore_interrupts(ints);
}
