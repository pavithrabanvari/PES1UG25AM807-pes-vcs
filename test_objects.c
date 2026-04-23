/*
 * test_objects.c — Phase 1 test: object store
 * PROVIDED — do not modify
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>

#include "pes.h"
#include "object.h"

static int pass = 0, fail = 0;

#define CHECK(label, expr) do { \
    if (expr) { printf("PASS: %s\n", label); pass++; } \
    else { printf("FAIL: %s\n", label); fail++; } \
} while(0)

int main(void) {
    /* init repo */
    mkdir(PES_DIR, 0755);
    mkdir(PES_OBJECTS, 0755);

    const char *content = "Hello, World!\n";
    size_t clen = strlen(content);
    char hex1[SHA256_HEX_SIZE], hex2[SHA256_HEX_SIZE];

    /* write blob */
    int r = object_write(OBJ_BLOB, (const uint8_t*)content, clen, hex1);
    CHECK("blob storage", r == 0 && hex1[0] != '\0');
    printf("Stored blob with hash: %s\n", hex1);

    /* verify file exists */
    char path[256];
    snprintf(path, sizeof(path), "%s/%.2s/%s", PES_OBJECTS, hex1, hex1+2);
    struct stat st;
    CHECK("Object stored at correct path", stat(path, &st) == 0);
    printf("Object stored at: %s\n", path);

    /* deduplication: writing same content returns same hash */
    r = object_write(OBJ_BLOB, (const uint8_t*)content, clen, hex2);
    CHECK("deduplication", r == 0 && strcmp(hex1, hex2) == 0);

    /* read back and compare */
    char type[16];
    size_t sz;
    uint8_t *data = object_read(hex1, type, &sz);
    CHECK("blob read back", data != NULL);
    CHECK("blob content matches", data && sz == clen && memcmp(data, content, clen) == 0);
    CHECK("blob type correct", strcmp(type, OBJ_BLOB) == 0);
    free(data);

    /* integrity check: corrupt the file and try to read */
    FILE *f = fopen(path, "r+b");
    if (f) { fseek(f, 5, SEEK_SET); fputc('X', f); fclose(f); }
    data = object_read(hex1, NULL, NULL);
    CHECK("integrity check", data == NULL);
    /* restore (re-write) */
    object_write(OBJ_BLOB, (const uint8_t*)content, clen, NULL);

    printf("\nAll Phase 1 tests %s.\n", fail == 0 ? "passed" : "had failures");
    return fail ? 1 : 0;
}
