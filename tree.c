#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pes.h"
#include "object.h"
#include "tree.h"
#include "index.h"

/* ── PROVIDED: entry comparator (sort by name) ─────────────────── */
int tree_entry_cmp(const void *a, const void *b) {
    return strcmp(((TreeEntry*)a)->name, ((TreeEntry*)b)->name);
}

/*
 * tree_serialize
 * --------------
 * Binary format per entry (same as git):
 *   "<mode> <name>\0<binary-hash-32-bytes>"
 *
 * Returns malloc'd buffer; caller must free. Sets *out_size.
 */
uint8_t *tree_serialize(const Tree *t, size_t *out_size) {
    /* Sort a copy */
    Tree sorted = *t;
    qsort(sorted.entries, sorted.count, sizeof(TreeEntry), tree_entry_cmp);

    /* First pass: calculate total size */
    size_t total = 0;
    for (int i = 0; i < sorted.count; i++) {
        total += strlen(sorted.entries[i].mode) + 1   /* "mode " */
               + strlen(sorted.entries[i].name) + 1   /* "name\0" */
               + SHA256_BIN_SIZE;                      /* 32 raw bytes */
    }

    uint8_t *buf = malloc(total);
    if (!buf) return NULL;
    size_t pos = 0;

    for (int i = 0; i < sorted.count; i++) {
        TreeEntry *e = &sorted.entries[i];

        /* "<mode> <name>\0" */
        int hlen = sprintf((char*)buf + pos, "%s %s", e->mode, e->name);
        pos += hlen;
        buf[pos++] = '\0';

        /* binary hash (32 bytes decoded from hex) */
        for (int j = 0; j < SHA256_BIN_SIZE; j++) {
            unsigned int byte;
            sscanf(e->hash + j * 2, "%02x", &byte);
            buf[pos++] = (uint8_t)byte;
        }
    }

    *out_size = pos;
    return buf;
}

/*
 * tree_parse
 * ----------
 * Parses binary tree data back into a Tree struct.
 * Returns 0 on success, -1 on error.
 */
int tree_parse(const uint8_t *data, size_t size, Tree *out) {
    out->count = 0;
    size_t pos = 0;

    while (pos < size && out->count < TREE_MAX_ENTRIES) {
        TreeEntry *e = &out->entries[out->count];

        /* read "mode name\0" */
        const char *header = (const char*)data + pos;
        size_t header_len = strnlen(header, size - pos);
        if (pos + header_len >= size) break;

        /* parse mode and name */
        if (sscanf(header, "%7s %255s", e->mode, e->name) != 2) break;
        pos += header_len + 1; /* skip NUL */

        /* read 32 binary hash bytes → convert to hex */
        if (pos + SHA256_BIN_SIZE > size) break;
        for (int j = 0; j < SHA256_BIN_SIZE; j++)
            sprintf(e->hash + j * 2, "%02x", data[pos + j]);
        e->hash[64] = '\0';
        pos += SHA256_BIN_SIZE;

        out->count++;
    }
    return 0;
}

/*
 * tree_from_index
 * ---------------
 * Builds the tree hierarchy from the index and writes all tree objects.
 *
 * Strategy (single-level simplification that also handles nesting):
 *   - Group index entries by their top-level component.
 *   - Entries with no '/' go directly into root as blob entries.
 *   - Entries with '/' are grouped by their first path component;
 *     for each such group we recurse (create a sub-index and call again).
 *   - Write the root tree object and return its hash.
 */

/* Helper: write a subtree for entries sharing prefix `dir` */
static int build_subtree(IndexEntry *entries, int count,
                          const char *dir, int depth, char *out_hex);

/* strip leading `dir/` from path */
static void strip_prefix(const char *path, const char *dir, char *out) {
    size_t dlen = strlen(dir);
    if (strncmp(path, dir, dlen) == 0 && path[dlen] == '/')
        strcpy(out, path + dlen + 1);
    else
        strcpy(out, path);
}

int tree_from_index(Index *idx, char *out_hex) {
    /* Build a synthetic entries array with relative paths == original paths */
    return build_subtree(idx->entries, idx->count, "", 0, out_hex);
}

static int build_subtree(IndexEntry *entries, int count,
                          const char *dir, int depth, char *out_hex) {
    Tree root;
    root.count = 0;

    /* collect unique first components */
    char seen[TREE_MAX_ENTRIES][256];
    int  nseen = 0;

    for (int i = 0; i < count; i++) {
        const char *rel = entries[i].path;
        /* Strip the leading dir prefix if we're in a subdirectory */
        char stripped[512];
        if (dir[0]) {
            strip_prefix(rel, dir, stripped);
            rel = stripped;
        }

        /* find the first path component */
        const char *slash = strchr(rel, '/');
        char component[256];
        if (!slash) {
            strncpy(component, rel, sizeof(component)-1);
        } else {
            size_t len = slash - rel;
            strncpy(component, rel, len);
            component[len] = '\0';
        }

        /* check if already seen */
        int found = 0;
        for (int j = 0; j < nseen; j++)
            if (strcmp(seen[j], component) == 0) { found = 1; break; }
        if (!found && nseen < TREE_MAX_ENTRIES)
            strncpy(seen[nseen++], component, 255);
    }

    /* for each unique component, create a blob or subtree entry */
    for (int s = 0; s < nseen; s++) {
        const char *comp = seen[s];

        /* is it a directory (does any entry have comp/ prefix)? */
        int is_dir = 0;
        char full_prefix[512];
        if (dir[0])
            snprintf(full_prefix, sizeof(full_prefix), "%s/%s", dir, comp);
        else
            snprintf(full_prefix, sizeof(full_prefix), "%s", comp);

        for (int i = 0; i < count; i++) {
            size_t plen = strlen(full_prefix);
            if (strncmp(entries[i].path, full_prefix, plen) == 0
                && entries[i].path[plen] == '/') {
                is_dir = 1;
                break;
            }
        }

        TreeEntry *te = &root.entries[root.count++];
        strncpy(te->name, comp, sizeof(te->name)-1);

        if (is_dir) {
            /* recurse */
            strncpy(te->mode, MODE_DIR, sizeof(te->mode)-1);
            if (build_subtree(entries, count, full_prefix, depth+1, te->hash) != 0)
                return -1;
        } else {
            /* find the exact index entry */
            for (int i = 0; i < count; i++) {
                if (strcmp(entries[i].path, full_prefix) == 0) {
                    strncpy(te->mode, entries[i].mode, sizeof(te->mode)-1);
                    strncpy(te->hash, entries[i].hash, SHA256_HEX_SIZE);
                    break;
                }
            }
        }
    }

    /* serialize and write this tree */
    size_t sz;
    uint8_t *buf = tree_serialize(&root, &sz);
    if (!buf) return -1;
    int r = object_write(OBJ_TREE, buf, sz, out_hex);
    free(buf);
    return r;
}
