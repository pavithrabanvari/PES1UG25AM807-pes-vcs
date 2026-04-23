#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include "pes.h"
#include "index.h"
#include "object.h"

/* ── PROVIDED: find entry by path ──────────────────────────────── */
IndexEntry *index_find(Index *idx, const char *path) {
    for (int i = 0; i < idx->count; i++)
        if (strcmp(idx->entries[i].path, path) == 0)
            return &idx->entries[i];
    return NULL;
}

/* ── PROVIDED: remove entry by path ────────────────────────────── */
void index_remove(Index *idx, const char *path) {
    for (int i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].path, path) == 0) {
            memmove(&idx->entries[i], &idx->entries[i+1],
                    (idx->count - i - 1) * sizeof(IndexEntry));
            idx->count--;
            return;
        }
    }
}

/* ── PROVIDED: collect untracked files recursively ─────────────── */
static void collect_untracked(const char *base, const char *prefix,
                               const Index *idx,
                               char untracked[][512], int *ucount, int umax) {
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && *ucount < umax) {
        if (e->d_name[0] == '.') continue;
        char rel[512], full[512];
        if (prefix[0])
            snprintf(rel,  sizeof(rel),  "%s/%s", prefix, e->d_name);
        else
            snprintf(rel,  sizeof(rel),  "%s", e->d_name);
        snprintf(full, sizeof(full), "%s/%s", base, e->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect_untracked(full, rel, idx, untracked, ucount, umax);
        } else {
            /* check if it's tracked */
            int found = 0;
            for (int i = 0; i < idx->count; i++)
                if (strcmp(idx->entries[i].path, rel) == 0) { found = 1; break; }
            if (!found) {
                strncpy(untracked[*ucount], rel, 511);
                (*ucount)++;
            }
        }
    }
    closedir(d);
}

/* ── PROVIDED: status display ──────────────────────────────────── */
void index_status(const Index *idx) {
    /* staged changes: files in index */
    printf("Staged changes:\n");
    int any_staged = 0;
    for (int i = 0; i < idx->count; i++) {
        printf("  staged:     %s\n", idx->entries[i].path);
        any_staged = 1;
    }
    if (!any_staged) printf("  (nothing to show)\n");

    /* unstaged: check if any indexed file changed on disk */
    printf("\nUnstaged changes:\n");
    int any_unstaged = 0;
    for (int i = 0; i < idx->count; i++) {
        struct stat st;
        if (stat(idx->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", idx->entries[i].path);
            any_unstaged = 1;
        } else if ((size_t)st.st_size != idx->entries[i].file_size ||
                   (time_t)st.st_mtime != idx->entries[i].mtime) {
            printf("  modified:   %s\n", idx->entries[i].path);
            any_unstaged = 1;
        }
    }
    if (!any_unstaged) printf("  (nothing to show)\n");

    /* untracked */
    printf("\nUntracked files:\n");
    char untracked[1024][512];
    int ucount = 0;
    collect_untracked(".", "", idx, untracked, &ucount, 1024);
    if (ucount == 0) printf("  (nothing to show)\n");
    for (int i = 0; i < ucount; i++)
        printf("  untracked:  %s\n", untracked[i]);
}

/* ── index_load ─────────────────────────────────────────────────
 * Reads .pes/index into *idx.
 * Format per line:  <mode> <hash> <mtime> <size> <path>
 * If file doesn't exist → empty index (not an error).
 */
int index_load(Index *idx) {
    idx->count = 0;
    FILE *f = fopen(PES_INDEX, "r");
    if (!f) {
        if (errno == ENOENT) return 0;
        return -1;
    }
    char line[700];
    while (fgets(line, sizeof(line), f) && idx->count < INDEX_MAX_ENTRIES) {
        /* strip newline */
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        IndexEntry *e = &idx->entries[idx->count];
        long long mtime;
        unsigned long long fsize;
        if (sscanf(line, "%7s %64s %lld %llu %511s",
                   e->mode, e->hash, &mtime, &fsize, e->path) == 5) {
            e->mtime     = (time_t)mtime;
            e->file_size = (size_t)fsize;
            idx->count++;
        }
    }
    fclose(f);
    return 0;
}

/* ── index_save ─────────────────────────────────────────────────
 * Writes *idx to .pes/index atomically (sort by path, temp+rename).
 */
static int entry_cmp(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

int index_save(const Index *idx) {
    /* work on a copy so we can sort without mutating caller's data */
    Index sorted = *idx;
    qsort(sorted.entries, sorted.count, sizeof(IndexEntry), entry_cmp);

    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp_XXXXXX", PES_INDEX);
    int fd = mkstemp(tmp);
    if (fd < 0) return -1;

    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(tmp); return -1; }

    for (int i = 0; i < sorted.count; i++) {
        IndexEntry *e = &sorted.entries[i];
        fprintf(f, "%s %s %lld %llu %s\n",
                e->mode, e->hash,
                (long long)e->mtime,
                (unsigned long long)e->file_size,
                e->path);
    }
    fflush(f);
    fsync(fd);
    fclose(f);

    if (rename(tmp, PES_INDEX) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ── index_add ──────────────────────────────────────────────────
 * Stages one file:
 *   1. stat() the file → get mtime, size, permissions
 *   2. read file contents
 *   3. object_write(OBJ_BLOB, ...) → get hash
 *   4. update (or insert) the IndexEntry
 */
int index_add(Index *idx, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "pes: cannot stat '%s': %s\n", path, strerror(errno));
        return -1;
    }

    /* read file */
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "pes: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }
    uint8_t *data = malloc(st.st_size + 1);
    if (!data) { fclose(f); return -1; }
    size_t nread = fread(data, 1, st.st_size, f);
    fclose(f);

    /* write blob */
    char hex[SHA256_HEX_SIZE];
    if (object_write(OBJ_BLOB, data, nread, hex) != 0) {
        free(data);
        return -1;
    }
    free(data);

    /* determine mode */
    char mode[8];
    if (st.st_mode & S_IXUSR)
        strncpy(mode, MODE_EXEC, sizeof(mode));
    else
        strncpy(mode, MODE_FILE, sizeof(mode));

    /* update or insert */
    IndexEntry *e = index_find(idx, path);
    if (!e) {
        if (idx->count >= INDEX_MAX_ENTRIES) return -1;
        e = &idx->entries[idx->count++];
    }
    strncpy(e->mode,  mode, sizeof(e->mode)-1);
    strncpy(e->hash,  hex,  SHA256_HEX_SIZE);
    strncpy(e->path,  path, sizeof(e->path)-1);
    e->mtime     = st.st_mtime;
    e->file_size = st.st_size;
    return 0;
}
