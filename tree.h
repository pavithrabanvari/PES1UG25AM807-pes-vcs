#ifndef TREE_H
#define TREE_H

#include <stdint.h>
#include <stddef.h>
#include "pes.h"

#define TREE_MAX_ENTRIES 1024

typedef struct {
    char mode[8];                  /* "100644", "100755", "040000" */
    char name[256];                /* filename or dirname */
    char hash[SHA256_HEX_SIZE];    /* hex SHA-256 */
} TreeEntry;

typedef struct {
    TreeEntry entries[TREE_MAX_ENTRIES];
    int count;
} Tree;

/* Serialize tree to bytes; caller must free returned buffer */
uint8_t *tree_serialize(const Tree *t, size_t *out_size);

/* Parse raw bytes back into a Tree; returns 0 on success */
int tree_parse(const uint8_t *data, size_t size, Tree *out);

/* Build tree hierarchy from index and write all tree objects.
   Returns root tree hash in out_hex. */
int tree_from_index(struct Index *idx, char *out_hex);

/* Compare helper for sorting entries */
int tree_entry_cmp(const void *a, const void *b);

#endif /* TREE_H */
