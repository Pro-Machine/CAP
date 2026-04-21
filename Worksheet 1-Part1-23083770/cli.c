#include "cli.h"
#include "flash_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "custom_fgets.h"

// Function: execute_command
// Parses and executes commands related to flash memory operations.
//
// Parameters:
// - command: A string containing the command and its arguments.
//
// Supported commands:
// - FLASH_WRITE  <sector> "<data>"  : Raw write (Task 1)
// - FLASH_READ   <sector> <length>  : Raw read  (Task 1)
// - FLASH_ERASE  <sector>           : Erase sector (Task 1)
// - FLASH_WRITE_STRUCT <sector> "<data>" : Structured write with metadata (Task 2)
// - FLASH_READ_STRUCT  <sector>          : Structured read, prints metadata (Task 2)

void execute_command(char *command) {
    // Split the command string into tokens
    char *token = strtok(command, " ");

    // Check for an empty or invalid command
    if (token == NULL) {
        printf("\nInvalid command\n");
        return;
    }

    // -------------------------------------------------------------------------
    // TASK 1: FLASH_WRITE
    // Usage: FLASH_WRITE <sector> "<data>"
    // -------------------------------------------------------------------------
    if (strcmp(token, "FLASH_WRITE") == 0) {
        // Parse the sector number
        token = strtok(NULL, " ");
        if (token == NULL) {
            printf("\nFLASH_WRITE requires an address and data\n");
            return;
        }
        uint32_t address = atoi(token);

        // Parse the data (enclosed in quotes)
        token = strtok(NULL, "\"");
        if (token == NULL) {
            printf("\nInvalid data format for FLASH_WRITE\n");
            return;
        }

        // Execute raw write
        flash_write_safe(address, (uint8_t *)token, strlen(token));
    }

    // -------------------------------------------------------------------------
    // TASK 1: FLASH_READ
    // Usage: FLASH_READ <sector> <length>
    // -------------------------------------------------------------------------
    else if (strcmp(token, "FLASH_READ") == 0) {
        // Parse the sector number
        token = strtok(NULL, " ");
        if (token == NULL) {
            printf("\nFLASH_READ requires an address and length\n");
            return;
        }
        uint32_t address = atoi(token);

        // Parse the length
        token = strtok(NULL, " ");
        if (token == NULL) {
            printf("\nInvalid length for FLASH_READ\n");
            return;
        }
        size_t length = atoi(token);

        // Read and print raw data
        uint8_t buffer[length + 1];
        flash_read_safe(address, buffer, length);
        buffer[length] = '\0';
        printf("\nData: %s\n", buffer);
    }

    // -------------------------------------------------------------------------
    // TASK 1: FLASH_ERASE
    // Usage: FLASH_ERASE <sector>
    // -------------------------------------------------------------------------
    else if (strcmp(token, "FLASH_ERASE") == 0) {
        // Parse the sector number
        token = strtok(NULL, " ");
        if (token == NULL) {
            printf("FLASH_ERASE requires an address\n");
            return;
        }
        uint32_t address = atoi(token);

        // Execute erase
        flash_erase_safe(address);
    }

    // -------------------------------------------------------------------------
    // TASK 2: FLASH_WRITE_STRUCT
    // Usage: FLASH_WRITE_STRUCT <sector> "<data>"
    // Writes data wrapped in a flash_data struct with write_count metadata.
    // The write_count auto-increments each time you write to the same sector.
    // -------------------------------------------------------------------------
    else if (strcmp(token, "FLASH_WRITE_STRUCT") == 0) {
        // Parse the sector number
        token = strtok(NULL, " ");
        if (token == NULL) {
            printf("\nFLASH_WRITE_STRUCT requires an address and data\n");
            return;
        }
        uint32_t address = atoi(token);

        // Parse the data (enclosed in quotes)
        token = strtok(NULL, "\"");
        if (token == NULL) {
            printf("\nInvalid data format for FLASH_WRITE_STRUCT\n");
            return;
        }

        // Build the flash_data struct and write it
        flash_data fd;
        fd.data_ptr   = (uint8_t *)token;
        fd.data_len   = strlen(token);
        fd.write_count = 0;  // auto-incremented inside flash_write_struct()

        flash_write_struct(address, &fd);
        printf("\nStruct written. Sector write count is now: %u\n",
               (unsigned)fd.write_count);
    }

    // -------------------------------------------------------------------------
    // TASK 2: FLASH_READ_STRUCT
    // Usage: FLASH_READ_STRUCT <sector>
    // Reads a flash_data struct and prints metadata + payload.
    // -------------------------------------------------------------------------
    else if (strcmp(token, "FLASH_READ_STRUCT") == 0) {
        // Parse the sector number
        token = strtok(NULL, " ");
        if (token == NULL) {
            printf("\nFLASH_READ_STRUCT requires an address\n");
            return;
        }
        uint32_t address = atoi(token);

        // Prepare output struct with a payload buffer
        uint8_t buf[256];
        flash_data fd;
        fd.data_ptr = buf;
        fd.data_len = sizeof(buf) - 1;

        flash_read_struct(address, &fd);
    }

    // -------------------------------------------------------------------------
    // Unknown command
    // -------------------------------------------------------------------------
    else {
        printf("\nUnknown command. Available commands:\n");
        printf("  FLASH_WRITE <sector> \"<data>\"\n");
        printf("  FLASH_READ  <sector> <length>\n");
        printf("  FLASH_ERASE <sector>\n");
        printf("  FLASH_WRITE_STRUCT <sector> \"<data>\"\n");
        printf("  FLASH_READ_STRUCT  <sector>\n");
    }
}
