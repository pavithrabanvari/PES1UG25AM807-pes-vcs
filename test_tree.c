/*
 * test_tree.c — Phase 2 test: tree objects
 * PROVIDED — do not modify
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pes.h"
#include "object.h"
#include "tree.h"
#include "index.h"

static int pass = 0, fail = 0;
#define CHECK(label, expr) do { \
    if (expr) { printf("PASS: %s\n", label); pass++; } \
    else { printf("FAIL: %s\n", label); fail++; } \
} while(0)

int main(void) {
    mkdir(PES_DIR, 0755);
    mkdir(PES_OBJECTS, 0755);

    /* Build a simple tree with two entries */
    Tree t;
    t.count = 2;
    strncpy(t.entries[0].mode, MODE_FILE,  7);
    strncpy(t.entries[0].name, "README.md", 255);
    memset(t.entries[0].hash, '0', 64); t.entries[0].hash[64] = '\0';

    strncpy(t.entries[1].mode, MODE_FILE,   7);
    strncpy(t.entries[1].name, "main.c",    255);
    memset(t.entries[1].hash, 'a', 64); t.entries[1].hash[64] = '\0';

    /* serialize */
    size_t sz;
    uint8_t *buf = tree_serialize(&t, &sz);
    CHECK("tree serializes", buf != NULL && sz > 0);
    printf("Serialized tree: %zu bytes\n", sz);

    /* parse roundtrip */
    Tree t2;
    int r = tree_parse(buf, sz, &t2);
    CHECK("tree parse succeeds", r == 0);
    CHECK("tree entry count matches", t2.count == t.count);

    /* entries should be sorted by name after serialize */
    CHECK("tree serialize/parse roundtrip", t2.count == 2
        && strcmp(t2.entries[0].name, "README.md") == 0
        && strcmp(t2.entries[1].name, "main.c")    == 0);

    /* deterministic serialization: reorder entries, should produce same bytes */
    Tree t3;
    t3.count = 2;
    t3.entries[0] = t.entries[1];  /* main.c first */
    t3.entries[1] = t.entries[0];  /* README.md second */
    size_t sz3;
    uint8_t *buf3 = tree_serialize(&t3, &sz3);
    CHECK("tree deterministic serialization", sz == sz3 && memcmp(buf, buf3, sz) == 0);

    free(buf);
    free(buf3);

    printf("\nAll Phase 2 tests %s.\n", fail == 0 ? "passed" : "had failures");
    return fail ? 1 : 0;
}
