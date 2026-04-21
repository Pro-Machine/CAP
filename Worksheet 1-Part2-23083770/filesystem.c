/**
 * filesystem.c
 *
 * Simple Filesystem implementation for the Raspberry Pi Pico.
 *
 * Design: each file maps to one dedicated flash sector (4096 bytes).
 * A file table (array of FileEntry) is kept in RAM and persisted to
 * flash sector 0 so files survive reboots.
 *
 * Flash layout:
 *   Sector 0        : file table (magic header + FileEntry array)
 *   Sector 1..10    : file data (one sector per file, index 0..9)
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

#include "filesystem.h"
#include "flash_ops.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hardware/flash.h"

/* -----------------------------------------------------------------------
 * Internal state
 * ----------------------------------------------------------------------- */
static FileEntry file_table[MAX_FILES];
static FS_FILE   file_handles[MAX_FILES];
static bool      fs_initialised = false;

/* -----------------------------------------------------------------------
 * Flash sector layout
 * ----------------------------------------------------------------------- */
#define TABLE_SECTOR_OFFSET    0
#define DATA_SECTOR_OFFSET(i)  (((i) + 1) * FLASH_SECTOR_SIZE)

/* Magic number to detect a previously formatted filesystem */
#define FS_MAGIC  0xCAFEF00D

/* On-flash table structure */
typedef struct {
    uint32_t  magic;
    FileEntry entries[MAX_FILES];
} FlashTable;

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Load the file table from flash into RAM */
static void fs_load_table(void) {
    FlashTable ft;
    flash_read_safe(TABLE_SECTOR_OFFSET, (uint8_t *)&ft, sizeof(FlashTable));
    if (ft.magic == FS_MAGIC) {
        memcpy(file_table, ft.entries, sizeof(file_table));
        printf("[FS] File table loaded from flash.\n");
    } else {
        memset(file_table, 0, sizeof(file_table));
        printf("[FS] No existing table found - starting fresh.\n");
    }
}

/* Persist the in-RAM file table to flash sector 0 */
static void fs_save_table(void) {
    FlashTable ft;
    ft.magic = FS_MAGIC;
    memcpy(ft.entries, file_table, sizeof(file_table));

    uint8_t buf[FLASH_SECTOR_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &ft, sizeof(FlashTable));
    flash_write_safe(TABLE_SECTOR_OFFSET, buf, FLASH_SECTOR_SIZE);
}

/* Lazy init - called on first filesystem operation */
static void fs_init(void) {
    if (!fs_initialised) {
        memset(file_handles, 0, sizeof(file_handles));
        fs_load_table();
        fs_initialised = true;
    }
}

/* Find a file entry by name; returns index or -1 */
static int find_entry(const char *path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].in_use &&
            strncmp(file_table[i].filename, path,
                    sizeof(file_table[i].filename)) == 0)
            return i;
    }
    return -1;
}

/* Allocate a free entry slot; returns index or -1 if full */
static int alloc_entry(const char *path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].in_use) {
            memset(&file_table[i], 0, sizeof(FileEntry));
            strncpy(file_table[i].filename, path,
                    sizeof(file_table[i].filename) - 1);
            file_table[i].in_use = true;
            file_table[i].size   = 0;
            return i;
        }
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * Core API
 * ----------------------------------------------------------------------- */

/**
 * fs_open() - Open a file for reading, writing, or appending.
 *
 * Mode "w": create or overwrite. Erases the file's flash sector.
 * Mode "r": read-only. Returns NULL if the file does not exist.
 * Mode "a": append. Creates the file if it does not exist.
 *
 * @param path  Filename string.
 * @param mode  "r", "w", or "a".
 * @return      Pointer to an FS_FILE handle, or NULL on error.
 */
FS_FILE* fs_open(const char* path, const char* mode) {
    fs_init();

    if (path == NULL || mode == NULL) {
        printf("[FS] fs_open: NULL argument\n");
        return NULL;
    }

    int idx = find_entry(path);

    if (strcmp(mode, "w") == 0) {
        if (idx == -1) {
            idx = alloc_entry(path);
            if (idx == -1) { printf("[FS] fs_open: filesystem full\n"); return NULL; }
        } else {
            file_table[idx].size = 0;
        }
        flash_erase_safe(DATA_SECTOR_OFFSET(idx));

    } else if (strcmp(mode, "r") == 0) {
        if (idx == -1) {
            printf("[FS] fs_open: file '%s' not found\n", path);
            return NULL;
        }

    } else if (strcmp(mode, "a") == 0) {
        if (idx == -1) {
            idx = alloc_entry(path);
            if (idx == -1) { printf("[FS] fs_open: filesystem full\n"); return NULL; }
            flash_erase_safe(DATA_SECTOR_OFFSET(idx));
        }

    } else {
        printf("[FS] fs_open: unknown mode '%s'\n", mode);
        return NULL;
    }

    file_handles[idx].entry    = &file_table[idx];
    file_handles[idx].position = (strcmp(mode, "a") == 0)
                                 ? file_table[idx].size : 0;

    printf("[FS] Opened '%s' mode='%s' size=%u sector=%d\n",
           path, mode, (unsigned)file_table[idx].size, idx + 1);
    return &file_handles[idx];
}

/**
 * fs_close() - Close a file and persist the file table to flash.
 *
 * @param file  Pointer to an open FS_FILE handle.
 */
void fs_close(FS_FILE* file) {
    if (file == NULL || file->entry == NULL) {
        printf("[FS] fs_close: invalid handle\n");
        return;
    }
    printf("[FS] Closing '%s' final size=%u\n",
           file->entry->filename, (unsigned)file->entry->size);
    fs_save_table();
    file->position = 0;
    file->entry    = NULL;
}

/**
 * fs_read() - Read up to `size` bytes from the current position.
 *
 * @param file    Open file handle.
 * @param buffer  Destination buffer.
 * @param size    Maximum bytes to read.
 * @return        Bytes read, or -1 on error.
 */
int fs_read(FS_FILE* file, void* buffer, int size) {
    if (file == NULL || file->entry == NULL || buffer == NULL) {
        printf("[FS] fs_read: invalid arguments\n");
        return -1;
    }

    uint32_t available = file->entry->size - file->position;
    if (available == 0) return 0;

    int to_read = (size < (int)available) ? size : (int)available;
    int idx     = (int)(file->entry - file_table);

    flash_read_safe(DATA_SECTOR_OFFSET(idx) + file->position,
                    (uint8_t *)buffer, to_read);

    file->position += to_read;
    printf("[FS] Read %d bytes from '%s' (pos=%u)\n",
           to_read, file->entry->filename, (unsigned)file->position);
    return to_read;
}

/**
 * fs_write() - Write `size` bytes to the file at the current position.
 *
 * Uses read-modify-write on the whole sector (required by flash hardware).
 * Maximum file size is one sector (4096 bytes).
 *
 * @param file    Open file handle.
 * @param buffer  Source data.
 * @param size    Bytes to write.
 * @return        Bytes written, or -1 on error.
 */
int fs_write(FS_FILE* file, const void* buffer, int size) {
    if (file == NULL || file->entry == NULL || buffer == NULL || size <= 0) {
        printf("[FS] fs_write: invalid arguments\n");
        return -1;
    }

    int idx = (int)(file->entry - file_table);

    if ((int)file->position + size > FLASH_SECTOR_SIZE) {
        printf("[FS] fs_write: write exceeds max file size (%d bytes)\n",
               FLASH_SECTOR_SIZE);
        return -1;
    }

    /* Read-modify-write the entire sector */
    uint8_t sector_buf[FLASH_SECTOR_SIZE];
    flash_read_safe(DATA_SECTOR_OFFSET(idx), sector_buf, FLASH_SECTOR_SIZE);
    memcpy(sector_buf + file->position, buffer, size);
    flash_erase_safe(DATA_SECTOR_OFFSET(idx));
    flash_write_safe(DATA_SECTOR_OFFSET(idx), sector_buf, FLASH_SECTOR_SIZE);

    file->position += size;
    if (file->position > file->entry->size)
        file->entry->size = file->position;

    printf("[FS] Wrote %d bytes to '%s' (pos=%u size=%u)\n",
           size, file->entry->filename,
           (unsigned)file->position, (unsigned)file->entry->size);
    return size;
}

/**
 * fs_seek() - Move the file position indicator.
 *
 * @param file    Open file handle.
 * @param offset  Byte offset.
 * @param whence  0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END.
 * @return        0 on success, -1 on error.
 */
int fs_seek(FS_FILE* file, long offset, int whence) {
    if (file == NULL || file->entry == NULL) {
        printf("[FS] fs_seek: invalid handle\n");
        return -1;
    }

    long new_pos;
    switch (whence) {
        case 0: new_pos = offset; break;
        case 1: new_pos = (long)file->position + offset; break;
        case 2: new_pos = (long)file->entry->size + offset; break;
        default:
            printf("[FS] fs_seek: invalid whence %d\n", whence);
            return -1;
    }

    if (new_pos < 0 || new_pos > (long)file->entry->size) {
        printf("[FS] fs_seek: position %ld out of bounds (size=%u)\n",
               new_pos, (unsigned)file->entry->size);
        return -1;
    }

    file->position = (uint32_t)new_pos;
    printf("[FS] Seeked '%s' to position %u\n",
           file->entry->filename, (unsigned)file->position);
    return 0;
}

/* -----------------------------------------------------------------------
 * Extended operations
 * ----------------------------------------------------------------------- */

/**
 * fs_delete() - Delete a file from the filesystem.
 *
 * Marks the file table entry as free and erases the data sector.
 *
 * @param path  Filename to delete.
 * @return      0 on success, -1 if file not found.
 */
int fs_delete(const char* path) {
    fs_init();

    int idx = find_entry(path);
    if (idx == -1) {
        printf("[FS] fs_delete: file '%s' not found\n", path);
        return -1;
    }

    /* Erase the data sector */
    flash_erase_safe(DATA_SECTOR_OFFSET(idx));

    /* Free the table entry */
    memset(&file_table[idx], 0, sizeof(FileEntry));
    fs_save_table();

    printf("[FS] Deleted '%s'\n", path);
    return 0;
}

/**
 * fs_list() - Print all files currently in the filesystem.
 */
void fs_list(void) {
    fs_init();

    printf("\n--- Filesystem Contents ---\n");
    int count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].in_use) {
            printf("  [%d] %-30s %u bytes\n",
                   i + 1,
                   file_table[i].filename,
                   (unsigned)file_table[i].size);
            count++;
        }
    }
    if (count == 0) printf("  (empty)\n");
    printf("  %d / %d slots used\n", count, MAX_FILES);
    printf("---------------------------\n\n");
}

/**
 * fs_rename() - Rename a file.
 *
 * @param old_path  Current filename.
 * @param new_path  New filename.
 * @return          0 on success, -1 on error.
 */
int fs_rename(const char* old_path, const char* new_path) {
    fs_init();

    if (old_path == NULL || new_path == NULL) {
        printf("[FS] fs_rename: NULL argument\n");
        return -1;
    }

    /* Check new name doesn't already exist */
    if (find_entry(new_path) != -1) {
        printf("[FS] fs_rename: '%s' already exists\n", new_path);
        return -1;
    }

    int idx = find_entry(old_path);
    if (idx == -1) {
        printf("[FS] fs_rename: file '%s' not found\n", old_path);
        return -1;
    }

    strncpy(file_table[idx].filename, new_path,
            sizeof(file_table[idx].filename) - 1);
    fs_save_table();

    printf("[FS] Renamed '%s' -> '%s'\n", old_path, new_path);
    return 0;
}

/* -----------------------------------------------------------------------
 * Self-test suite
 * ----------------------------------------------------------------------- */

/* Helper: print pass/fail */
static void test_result(const char *name, bool passed) {
    printf("  [%s] %s\n", passed ? "PASS" : "FAIL", name);
}

/**
 * fs_run_tests() - Run a comprehensive test suite and print results.
 *
 * Tests cover: write, read, append, seek, delete, rename, persistence,
 * and error/edge cases.
 */
void fs_run_tests(void) {
    printf("\n========== FILESYSTEM TEST SUITE ==========\n");
    int passed = 0, total = 0;

#define CHECK(name, cond) do { \
    total++; \
    bool _r = (cond); \
    test_result(name, _r); \
    if (_r) passed++; \
} while(0)

    /* --- Test 1: Write and read back --- */
    {
        FS_FILE *f = fs_open("test1.txt", "w");
        CHECK("fs_open write mode", f != NULL);
        if (f) {
            int n = fs_write(f, "TestData", 8);
            CHECK("fs_write returns correct byte count", n == 8);
            fs_close(f);
        }

        char buf[16] = {0};
        f = fs_open("test1.txt", "r");
        CHECK("fs_open read mode", f != NULL);
        if (f) {
            int n = fs_read(f, buf, 8);
            CHECK("fs_read returns correct byte count", n == 8);
            CHECK("fs_read data matches written data",
                  memcmp(buf, "TestData", 8) == 0);
            fs_close(f);
        }
    }

    /* --- Test 2: Append --- */
    {
        FS_FILE *f = fs_open("test1.txt", "a");
        CHECK("fs_open append mode", f != NULL);
        if (f) {
            fs_write(f, "More", 4);
            fs_close(f);
        }

        char buf[16] = {0};
        f = fs_open("test1.txt", "r");
        if (f) {
            fs_read(f, buf, 12);
            CHECK("append extends file correctly",
                  memcmp(buf, "TestDataMore", 12) == 0);
            fs_close(f);
        }
    }

    /* --- Test 3: Seek SEEK_SET --- */
    {
        char buf[8] = {0};
        FS_FILE *f = fs_open("test1.txt", "r");
        if (f) {
            int r = fs_seek(f, 4, 0);   /* SEEK_SET to byte 4 */
            CHECK("fs_seek SEEK_SET returns 0", r == 0);
            fs_read(f, buf, 4);
            CHECK("fs_seek SEEK_SET reads correct data",
                  memcmp(buf, "Data", 4) == 0);
            fs_close(f);
        }
    }

    /* --- Test 4: Seek SEEK_END --- */
    {
        char buf[8] = {0};
        FS_FILE *f = fs_open("test1.txt", "r");
        if (f) {
            fs_seek(f, -4, 2);  /* SEEK_END -4 */
            fs_read(f, buf, 4);
            CHECK("fs_seek SEEK_END reads last 4 bytes",
                  memcmp(buf, "More", 4) == 0);
            fs_close(f);
        }
    }

    /* --- Test 5: Overwrite --- */
    {
        FS_FILE *f = fs_open("test1.txt", "w");
        if (f) {
            fs_write(f, "Fresh", 5);
            fs_close(f);
        }
        char buf[8] = {0};
        f = fs_open("test1.txt", "r");
        if (f) {
            int n = fs_read(f, buf, 8);
            CHECK("overwrite resets file size", n == 5);
            CHECK("overwrite stores new data",
                  memcmp(buf, "Fresh", 5) == 0);
            fs_close(f);
        }
    }

    /* --- Test 6: Rename --- */
    {
        int r = fs_rename("test1.txt", "renamed.txt");
        CHECK("fs_rename returns 0", r == 0);
        FS_FILE *f = fs_open("renamed.txt", "r");
        CHECK("renamed file is accessible", f != NULL);
        if (f) fs_close(f);
        f = fs_open("test1.txt", "r");
        CHECK("original name no longer exists", f == NULL);
    }

    /* --- Test 7: Delete --- */
    {
        int r = fs_delete("renamed.txt");
        CHECK("fs_delete returns 0", r == 0);
        FS_FILE *f = fs_open("renamed.txt", "r");
        CHECK("deleted file not accessible", f == NULL);
    }

    /* --- Test 8: Error handling – open non-existent file for read --- */
    {
        FS_FILE *f = fs_open("doesnotexist.txt", "r");
        CHECK("open non-existent file returns NULL", f == NULL);
    }

    /* --- Test 9: Error handling – seek out of bounds --- */
    {
        FS_FILE *f = fs_open("bounds.txt", "w");
        if (f) { fs_write(f, "AB", 2); fs_close(f); }
        f = fs_open("bounds.txt", "r");
        if (f) {
            int r = fs_seek(f, 999, 0);
            CHECK("seek out of bounds returns -1", r == -1);
            fs_close(f);
        }
        fs_delete("bounds.txt");
    }

    /* --- Test 10: Multiple files --- */
    {
        FS_FILE *f1 = fs_open("file1.txt", "w");
        FS_FILE *f2 = fs_open("file2.txt", "w");
        CHECK("can create multiple files", f1 != NULL && f2 != NULL);
        if (f1) { fs_write(f1, "AAA", 3); fs_close(f1); }
        if (f2) { fs_write(f2, "BBB", 3); fs_close(f2); }

        char b1[4]={0}, b2[4]={0};
        f1 = fs_open("file1.txt", "r");
        f2 = fs_open("file2.txt", "r");
        if (f1) { fs_read(f1, b1, 3); fs_close(f1); }
        if (f2) { fs_read(f2, b2, 3); fs_close(f2); }
        CHECK("multiple files store independently",
              memcmp(b1,"AAA",3)==0 && memcmp(b2,"BBB",3)==0);

        fs_delete("file1.txt");
        fs_delete("file2.txt");
    }

#undef CHECK

    printf("\n===========================================\n");
    printf("Results: %d / %d tests passed\n", passed, total);
    printf("===========================================\n\n");
}
