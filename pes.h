#ifndef PES_H
#define PES_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* ── constants ─────────────────────────────────────────────────── */
#define PES_DIR        ".pes"
#define PES_OBJECTS    ".pes/objects"
#define PES_REFS       ".pes/refs/heads"
#define PES_HEAD       ".pes/HEAD"
#define PES_INDEX      ".pes/index"
#define PES_BRANCH     "main"

#define SHA256_HEX_SIZE 65   /* 64 hex chars + NUL */
#define SHA256_BIN_SIZE 32

/* object type tags */
#define OBJ_BLOB   "blob"
#define OBJ_TREE   "tree"
#define OBJ_COMMIT "commit"

/* file-mode constants (same as git) */
#define MODE_FILE  "100644"
#define MODE_EXEC  "100755"
#define MODE_DIR   "040000"

/* ── author ─────────────────────────────────────────────────────── */
static inline const char *pes_author(void) {
    const char *a = getenv("PES_AUTHOR");
    return a ? a : "PES User <pes@localhost>";
}

#endif /* PES_H */
