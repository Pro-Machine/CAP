/**
 * main.c
 *
 * CLI for demonstrating the Simple Filesystem on the Raspberry Pi Pico W.
 *
 * Commands:
 *   FS_WRITE   <file> "<data>"  - Write data to a file (overwrites)
 *   FS_READ    <file> <bytes>   - Read bytes from a file
 *   FS_APPEND  <file> "<data>"  - Append data to a file
 *   FS_SEEK    <file> <offset>  - Seek to offset and read remaining
 *   FS_DELETE  <file>           - Delete a file
 *   FS_LIST                     - List all files
 *   FS_RENAME  <old> <new>      - Rename a file
 *   FS_TEST                     - Run the full test suite
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "flash_ops.h"
#include "filesystem.h"

/* Read a line from USB serial, stripping \r and \n */
static void read_line(char *buf, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        int c = getchar();
        if (c == '\n' || c == '\r') break;
        if (c >= 32 && c < 127) buf[i++] = (char)c;
    }
    buf[i] = '\0';
    /* Flush any trailing \r\n */
    int c;
    while ((c = getchar_timeout_us(1000)) != PICO_ERROR_TIMEOUT) {
        if (c != '\r' && c != '\n') break;
    }
}

int main() {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(100);

    printf("\n=== Pico Simple Filesystem Demo ===\n");
    printf("Commands:\n");
    printf("  FS_WRITE  <file> \"<data>\"\n");
    printf("  FS_READ   <file> <bytes>\n");
    printf("  FS_APPEND <file> \"<data>\"\n");
    printf("  FS_SEEK   <file> <offset>\n");
    printf("  FS_DELETE <file>\n");
    printf("  FS_LIST\n");
    printf("  FS_RENAME <old> <new>\n");
    printf("  FS_TEST\n\n");

    char line[256];

    while (1) {
        printf("Enter command: ");
        read_line(line, sizeof(line));

        char *token = strtok(line, " ");
        if (token == NULL) continue;

        /* FS_WRITE <file> "<data>" */
        if (strcmp(token, "FS_WRITE") == 0) {
            char *fname = strtok(NULL, " ");
            char *data  = strtok(NULL, "\"");
            if (!fname || !data) { printf("Usage: FS_WRITE <file> \"<data>\"\n"); continue; }
            FS_FILE *f = fs_open(fname, "w");
            if (!f) continue;
            fs_write(f, data, strlen(data));
            fs_close(f);

        /* FS_READ <file> <bytes> */
        } else if (strcmp(token, "FS_READ") == 0) {
            char *fname = strtok(NULL, " ");
            char *slen  = strtok(NULL, " ");
            if (!fname || !slen) { printf("Usage: FS_READ <file> <bytes>\n"); continue; }
            FS_FILE *f = fs_open(fname, "r");
            if (!f) continue;
            char buf[256] = {0};
            int n = fs_read(f, buf, atoi(slen));
            if (n > 0) printf("Data: %.*s\n", n, buf);
            fs_close(f);

        /* FS_APPEND <file> "<data>" */
        } else if (strcmp(token, "FS_APPEND") == 0) {
            char *fname = strtok(NULL, " ");
            char *data  = strtok(NULL, "\"");
            if (!fname || !data) { printf("Usage: FS_APPEND <file> \"<data>\"\n"); continue; }
            FS_FILE *f = fs_open(fname, "a");
            if (!f) continue;
            fs_write(f, data, strlen(data));
            fs_close(f);

        /* FS_SEEK <file> <offset> */
        } else if (strcmp(token, "FS_SEEK") == 0) {
            char *fname   = strtok(NULL, " ");
            char *soffset = strtok(NULL, " ");
            if (!fname || !soffset) { printf("Usage: FS_SEEK <file> <offset>\n"); continue; }
            FS_FILE *f = fs_open(fname, "r");
            if (!f) continue;
            fs_seek(f, atoi(soffset), 0);
            char buf[256] = {0};
            int n = fs_read(f, buf, sizeof(buf) - 1);
            if (n > 0) printf("Data from offset: %.*s\n", n, buf);
            fs_close(f);

        /* FS_DELETE <file> */
        } else if (strcmp(token, "FS_DELETE") == 0) {
            char *fname = strtok(NULL, " ");
            if (!fname) { printf("Usage: FS_DELETE <file>\n"); continue; }
            fs_delete(fname);

        /* FS_LIST */
        } else if (strcmp(token, "FS_LIST") == 0) {
            fs_list();

        /* FS_RENAME <old> <new> */
        } else if (strcmp(token, "FS_RENAME") == 0) {
            char *old_name = strtok(NULL, " ");
            char *new_name = strtok(NULL, " ");
            if (!old_name || !new_name) { printf("Usage: FS_RENAME <old> <new>\n"); continue; }
            fs_rename(old_name, new_name);

        /* FS_TEST - run the full test suite */
        } else if (strcmp(token, "FS_TEST") == 0) {
            fs_run_tests();

        } else {
            printf("Unknown command: %s\n", token);
            printf("Type FS_LIST to see files or FS_TEST to run tests.\n");
        }
    }

    return 0;
}
