/**
 * filesystem.h
 *
 * Simple Filesystem API for the Raspberry Pi Pico.
 * Each file occupies one dedicated flash sector (4096 bytes).
 * A file table is persisted to flash sector 0 so files survive reboots.
 *
 * Author: Kaan Karadag-23083770
 * Date:   April 2026
 * Module: Communications and Protocols (UFCFVR-15-3)
 */

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_FILES 10  /* Maximum number of files in the filesystem */

/* File entry – stored in the flash file table */
typedef struct {
    char     filename[256]; /* Path/name of the file          */
    uint32_t size;          /* Current size of the file       */
    bool     in_use;        /* Whether this slot is occupied  */
} FileEntry;

/* File handle – returned by fs_open(), used for all operations */
typedef struct {
    FileEntry *entry;    /* Pointer to the file's table entry */
    uint32_t   position; /* Current read/write position       */
} FS_FILE;

/* -----------------------------------------------------------------------
 * Core file operations (Task requirement)
 * ----------------------------------------------------------------------- */
FS_FILE* fs_open  (const char* path, const char* mode);
void     fs_close (FS_FILE* file);
int      fs_read  (FS_FILE* file, void* buffer, int size);
int      fs_write (FS_FILE* file, const void* buffer, int size);
int      fs_seek  (FS_FILE* file, long offset, int whence);

/* -----------------------------------------------------------------------
 * Extended operations (bonus functionality)
 * ----------------------------------------------------------------------- */
int  fs_delete (const char* path);
void fs_list   (void);
int  fs_rename (const char* old_path, const char* new_path);

/* -----------------------------------------------------------------------
 * Testing
 * ----------------------------------------------------------------------- */
void fs_run_tests(void);

#endif /* FILESYSTEM_H */
