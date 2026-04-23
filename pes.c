/*
 * pes.c — CLI entry point and command dispatch
 * PROVIDED — do not modify
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "pes.h"
#include "object.h"
#include "index.h"
#include "tree.h"
#include "commit.h"

/* ── pes init ───────────────────────────────────────────────────── */
static int cmd_init(void) {
    const char *dirs[] = {
        PES_DIR,
        PES_OBJECTS,
        PES_REFS,
        NULL
    };
    for (int i = 0; dirs[i]; i++) {
        if (mkdir(dirs[i], 0755) != 0 && errno != EEXIST) {
            perror(dirs[i]);
            return 1;
        }
    }
    /* write HEAD */
    FILE *f = fopen(PES_HEAD, "w");
    if (!f) { perror(PES_HEAD); return 1; }
    fprintf(f, "ref: refs/heads/%s\n", PES_BRANCH);
    fclose(f);

    printf("Initialized empty PES repository in %s/\n", PES_DIR);
    return 0;
}

/* ── pes add ────────────────────────────────────────────────────── */
static int cmd_add(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: pes add <file>...\n"); return 1; }
    Index idx;
    if (index_load(&idx) != 0) { fprintf(stderr, "pes: cannot load index\n"); return 1; }
    for (int i = 2; i < argc; i++)
        if (index_add(&idx, argv[i]) != 0)
            fprintf(stderr, "pes: failed to add '%s'\n", argv[i]);
    return index_save(&idx);
}

/* ── pes status ─────────────────────────────────────────────────── */
static int cmd_status(void) {
    Index idx;
    if (index_load(&idx) != 0) { fprintf(stderr, "pes: cannot load index\n"); return 1; }
    index_status(&idx);
    return 0;
}

/* ── pes commit ─────────────────────────────────────────────────── */
static int cmd_commit(int argc, char **argv) {
    if (argc < 4 || strcmp(argv[2], "-m") != 0) {
        fprintf(stderr, "usage: pes commit -m <message>\n");
        return 1;
    }
    char hex[SHA256_HEX_SIZE];
    if (commit_create(argv[3], hex) != 0) return 1;
    printf("Committed: %s...\n", hex);  /* already printed short form inside */
    return 0;
}

/* ── pes log ────────────────────────────────────────────────────── */
static int cmd_log(void) {
    commit_walk();
    return 0;
}

/* ── main ───────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: pes <command> [args]\n"
                        "commands: init add status commit log\n");
        return 1;
    }

    if (strcmp(argv[1], "init")   == 0) return cmd_init();
    if (strcmp(argv[1], "add")    == 0) return cmd_add(argc, argv);
    if (strcmp(argv[1], "status") == 0) return cmd_status();
    if (strcmp(argv[1], "commit") == 0) return cmd_commit(argc, argv);
    if (strcmp(argv[1], "log")    == 0) return cmd_log();

    fprintf(stderr, "pes: unknown command '%s'\n", argv[1]);
    return 1;
}
