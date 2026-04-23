/*
 * commit.c — Commit creation and history walking
 *
 * Text format stored inside the object:
 *
 *   tree <hash>\n
 *   [parent <hash>\n]          ← omitted for root commit
 *   author <name> <timestamp>\n
 *
 *   <message>\n
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pes.h"
#include "object.h"
#include "index.h"
#include "tree.h"
#include "commit.h"

/* ── PROVIDED: commit_serialize ────────────────────────────────── */
uint8_t *commit_serialize(const Commit *c, size_t *out_size) {
    char buf[4096];
    int pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "tree %s\n", c->tree);
    if (c->parent[0])
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "parent %s\n", c->parent);
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "author %s %ld\n\n%s\n",
                    c->author, (long)c->timestamp, c->message);

    *out_size = pos;
    uint8_t *out = malloc(pos + 1);
    if (!out) return NULL;
    memcpy(out, buf, pos + 1);
    return out;
}

/* ── PROVIDED: commit_parse ─────────────────────────────────────── */
int commit_parse(const uint8_t *data, size_t size, Commit *out) {
    memset(out, 0, sizeof(*out));
    char *text = strndup((const char*)data, size);
    if (!text) return -1;

    char *line = strtok(text, "\n");
    int in_body = 0;
    char body[1024] = {0};

    while (line) {
        if (!in_body) {
            if (strncmp(line, "tree ", 5) == 0)
                strncpy(out->tree, line + 5, SHA256_HEX_SIZE - 1);
            else if (strncmp(line, "parent ", 7) == 0)
                strncpy(out->parent, line + 7, SHA256_HEX_SIZE - 1);
            else if (strncmp(line, "author ", 7) == 0) {
                /* last token is timestamp */
                char tmp[128];
                strncpy(tmp, line + 7, sizeof(tmp) - 1);
                char *last_space = strrchr(tmp, ' ');
                if (last_space) {
                    out->timestamp = (time_t)atol(last_space + 1);
                    *last_space = '\0';
                    strncpy(out->author, tmp, sizeof(out->author) - 1);
                }
            } else if (line[0] == '\0') {
                in_body = 1;
            }
        } else {
            if (body[0]) strncat(body, "\n", sizeof(body)-strlen(body)-1);
            strncat(body, line, sizeof(body)-strlen(body)-1);
        }
        line = strtok(NULL, "\n");
    }
    strncpy(out->message, body, sizeof(out->message) - 1);
    free(text);
    return 0;
}

/* ── PROVIDED: head_read ────────────────────────────────────────── */
int head_read(char *out_hex) {
    /* HEAD contains: "ref: refs/heads/main" */
    FILE *f = fopen(PES_HEAD, "r");
    if (!f) return 1;

    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 1; }
    fclose(f);

    line[strcspn(line, "\n")] = '\0';
    if (strncmp(line, "ref: ", 5) != 0) return 1;

    /* read the branch file */
    char branch_path[256];
    snprintf(branch_path, sizeof(branch_path), ".pes/%s", line + 5);

    FILE *bf = fopen(branch_path, "r");
    if (!bf) return 1;  /* no commits yet */

    if (!fgets(out_hex, SHA256_HEX_SIZE, bf)) { fclose(bf); return 1; }
    fclose(bf);
    out_hex[strcspn(out_hex, "\n")] = '\0';
    return (out_hex[0] == '\0') ? 1 : 0;
}

/* ── PROVIDED: head_update ──────────────────────────────────────── */
int head_update(const char *hex) {
    /* read HEAD to find branch path */
    FILE *hf = fopen(PES_HEAD, "r");
    if (!hf) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), hf)) { fclose(hf); return -1; }
    fclose(hf);
    line[strcspn(line, "\n")] = '\0';

    char branch_path[256];
    snprintf(branch_path, sizeof(branch_path), ".pes/%s", line + 5);

    /* atomic write */
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", branch_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", hex);
    fflush(f);
    fclose(f);
    return rename(tmp, branch_path);
}

/* ── PROVIDED: commit_walk ──────────────────────────────────────── */
void commit_walk(void) {
    char current[SHA256_HEX_SIZE];
    if (head_read(current) != 0) {
        printf("No commits yet.\n");
        return;
    }

    while (current[0]) {
        char type[16];
        size_t sz;
        uint8_t *data = object_read(current, type, &sz);
        if (!data) break;

        Commit c;
        commit_parse(data, sz, &c);
        free(data);

        /* short hash display (first 7 chars) */
        char short_hash[8];
        strncpy(short_hash, current, 7);
        short_hash[7] = '\0';

        printf("commit %s\n", current);
        printf("Author: %s\n", c.author);
        printf("Date:   %ld\n", (long)c.timestamp);
        printf("\n    %s\n\n", c.message);

        if (c.parent[0])
            strncpy(current, c.parent, SHA256_HEX_SIZE);
        else
            break;
    }
}

/*
 * commit_create
 * -------------
 * 1. Load the index.
 * 2. Build tree from index → root tree hash.
 * 3. Read current HEAD (may be empty for first commit).
 * 4. Build and serialize the Commit struct.
 * 5. Write commit object.
 * 6. Update HEAD (branch pointer).
 */
int commit_create(const char *message, char *out_hex) {
    /* Step 1: load index */
    Index idx;
    if (index_load(&idx) != 0) return -1;
    if (idx.count == 0) {
        fprintf(stderr, "pes: nothing to commit (index is empty)\n");
        return -1;
    }

    /* Step 2: build tree */
    Commit c;
    memset(&c, 0, sizeof(c));
    if (tree_from_index(&idx, c.tree) != 0) return -1;

    /* Step 3: parent */
    if (head_read(c.parent) != 0)
        c.parent[0] = '\0';  /* first commit: no parent */

    /* Step 4: metadata */
    strncpy(c.author, pes_author(), sizeof(c.author) - 1);
    c.timestamp = time(NULL);
    strncpy(c.message, message, sizeof(c.message) - 1);

    /* Step 5: serialize + write */
    size_t sz;
    uint8_t *data = commit_serialize(&c, &sz);
    if (!data) return -1;

    char hex[SHA256_HEX_SIZE];
    int r = object_write(OBJ_COMMIT, data, sz, hex);
    free(data);
    if (r != 0) return -1;

    /* Step 6: update HEAD */
    if (head_update(hex) != 0) return -1;
    if (out_hex) strncpy(out_hex, hex, SHA256_HEX_SIZE);

    /* print short confirmation like git does */
    char short_hash[8];
    strncpy(short_hash, hex, 7); short_hash[7] = '\0';

    /* read branch name for display */
    FILE *hf = fopen(PES_HEAD, "r");
    char branch[64] = "main";
    if (hf) {
        char line[256];
        if (fgets(line, sizeof(line), hf)) {
            line[strcspn(line, "\n")] = '\0';
            /* "ref: refs/heads/main" → "main" */
            char *last = strrchr(line, '/');
            if (last) strncpy(branch, last + 1, sizeof(branch) - 1);
        }
        fclose(hf);
    }
    printf("[%s %s] %s\n", branch, short_hash, message);
    return 0;
}
