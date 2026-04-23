#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/sha.h>

#include "pes.h"

/* ── PROVIDED: sha256 hex helper ───────────────────────────────── */
static void sha256_to_hex(const unsigned char *hash, char *hex) {
    for (int i = 0; i < SHA256_BIN_SIZE; i++)
        sprintf(hex + i * 2, "%02x", hash[i]);
    hex[64] = '\0';
}

/* ── PROVIDED: ensure a directory exists ───────────────────────── */
static int mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

/*
 * object_write
 * ------------
 * Stores `data` (length `size`) as an object of `type` ("blob", "tree",
 * or "commit") in the PES object store.
 *
 * Steps:
 *  1. Build the object content:  "<type> <size>\0<data>"
 *  2. Compute SHA-256 of the full object content.
 *  3. Convert hash to 64-char hex string → that is the object's name.
 *  4. Derive the shard path:  .pes/objects/<first-2-hex>/<remaining-62-hex>
 *  5. If the file already exists, we're done (deduplication).
 *  6. Write atomically: write to a temp file, fsync, then rename.
 *  7. Copy hex string into `out_hex` (must be SHA256_HEX_SIZE bytes).
 *
 * Returns 0 on success, -1 on error.
 */
int object_write(const char *type, const uint8_t *data, size_t size,
                 char *out_hex) {
    /* Step 1: build header  "<type> <size>\0" */
    char header[64];
    int hlen = snprintf(header, sizeof(header), "%s %zu", type, size) + 1;
    /* +1 to include the NUL byte that snprintf does NOT count */

    size_t total = (size_t)hlen + size;
    uint8_t *obj = malloc(total);
    if (!obj) return -1;
    memcpy(obj, header, hlen);
    memcpy(obj + hlen, data, size);

    /* Step 2: SHA-256 */
    unsigned char hash[SHA256_BIN_SIZE];
    SHA256(obj, total, hash);

    /* Step 3: hex */
    char hex[SHA256_HEX_SIZE];
    sha256_to_hex(hash, hex);
    if (out_hex) memcpy(out_hex, hex, SHA256_HEX_SIZE);

    /* Step 4: shard path */
    char shard_dir[256], obj_path[256];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", PES_OBJECTS, hex);
    snprintf(obj_path,  sizeof(obj_path),  "%s/%s",   shard_dir, hex + 2);

    /* Step 5: deduplication */
    if (access(obj_path, F_OK) == 0) { free(obj); return 0; }

    /* Step 6: atomic write */
    mkdir_p(shard_dir);

    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp_XXXXXX", shard_dir);
    int fd = mkstemp(tmp_path);
    if (fd < 0) { free(obj); return -1; }

    size_t written = 0;
    while (written < total) {
        ssize_t r = write(fd, obj + written, total - written);
        if (r < 0) { close(fd); unlink(tmp_path); free(obj); return -1; }
        written += r;
    }
    fsync(fd);
    close(fd);
    free(obj);

    if (rename(tmp_path, obj_path) != 0) { unlink(tmp_path); return -1; }
    return 0;
}

/*
 * object_read
 * -----------
 * Reads and verifies the object identified by `hex` from the store.
 *
 * Steps:
 *  1. Derive the shard path from hex.
 *  2. Read the entire file into a buffer.
 *  3. Recompute SHA-256 and verify it matches `hex` (integrity check).
 *  4. Parse the header: find the '\0' separator.
 *  5. Extract type and size from the header.
 *  6. Allocate a buffer for the data, copy data portion, set *out_size.
 *  7. If `out_type` is non-NULL, copy type string into it (≤15 chars + NUL).
 *
 * Returns a malloc'd buffer containing the object data (caller must free),
 * or NULL on error.
 */
uint8_t *object_read(const char *hex, char *out_type, size_t *out_size) {
    /* Step 1: path */
    char obj_path[256];
    snprintf(obj_path, sizeof(obj_path), "%s/%.2s/%s", PES_OBJECTS, hex, hex + 2);

    /* Step 2: read file */
    FILE *f = fopen(obj_path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    uint8_t *buf = malloc(fsize);
    if (!buf) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, fsize, f) != fsize) { free(buf); fclose(f); return NULL; }
    fclose(f);

    /* Step 3: integrity check */
    unsigned char hash[SHA256_BIN_SIZE];
    SHA256(buf, fsize, hash);
    char computed[SHA256_HEX_SIZE];
    sha256_to_hex(hash, computed);
    if (strcmp(computed, hex) != 0) { free(buf); return NULL; }

    /* Step 4: find NUL separator */
    size_t nul_pos = 0;
    for (long i = 0; i < fsize; i++) {
        if (buf[i] == '\0') { nul_pos = i; break; }
    }
    if (nul_pos == 0) { free(buf); return NULL; }

    /* Step 5: parse header */
    char header[64];
    memcpy(header, buf, nul_pos < sizeof(header) ? nul_pos : sizeof(header)-1);
    header[nul_pos] = '\0';

    char type[16];
    size_t data_size;
    if (sscanf(header, "%15s %zu", type, &data_size) != 2) { free(buf); return NULL; }

    /* Step 6: copy data */
    uint8_t *data = malloc(data_size + 1);
    if (!data) { free(buf); return NULL; }
    memcpy(data, buf + nul_pos + 1, data_size);
    data[data_size] = '\0';
    free(buf);

    if (out_size) *out_size = data_size;
    if (out_type) { strncpy(out_type, type, 15); out_type[15] = '\0'; }
    return data;
}
