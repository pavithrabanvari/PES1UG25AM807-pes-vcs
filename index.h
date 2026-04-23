#ifndef INDEX_H
#define INDEX_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "pes.h"

#define INDEX_MAX_ENTRIES 4096

typedef struct {
    char     mode[8];               /* "100644" or "100755" */
    char     hash[SHA256_HEX_SIZE]; /* hex SHA-256 of blob */
    time_t   mtime;                 /* modification time */
    size_t   file_size;             /* file size in bytes */
    char     path[512];             /* relative path from repo root */
} IndexEntry;

typedef struct Index {
    IndexEntry entries[INDEX_MAX_ENTRIES];
    int        count;
} Index;

/* Load .pes/index → fills *idx.  If file missing, sets count=0.  Returns 0. */
int index_load(Index *idx);

/* Save *idx → .pes/index atomically (temp+rename, sorted by path). */
int index_save(const Index *idx);

/* Stage one file: hash it, store blob, update index entry. */
int index_add(Index *idx, const char *path);

/* PROVIDED helpers */
IndexEntry *index_find(Index *idx, const char *path);
void        index_remove(Index *idx, const char *path);
void        index_status(const Index *idx);

#endif /* INDEX_H */
