#ifndef COMMIT_H
#define COMMIT_H

#include <time.h>
#include "pes.h"

typedef struct {
    char tree[SHA256_HEX_SIZE];
    char parent[SHA256_HEX_SIZE];  /* empty string if root commit */
    char author[128];
    time_t timestamp;
    char message[1024];
} Commit;

/* Serialize a Commit struct to text */
uint8_t *commit_serialize(const Commit *c, size_t *out_size);

/* Parse text into a Commit struct */
int commit_parse(const uint8_t *data, size_t size, Commit *out);

/* Create a new commit from the current index */
int commit_create(const char *message, char *out_hex);

/* Walk and display commit history from HEAD */
void commit_walk(void);

/* Read/update the HEAD reference */
int head_read(char *out_hex);   /* returns 0 if found, 1 if none */
int head_update(const char *hex);

#endif /* COMMIT_H */
