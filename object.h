#ifndef OBJECT_H
#define OBJECT_H

#include <stdint.h>
#include <stddef.h>
#include "pes.h"

int     object_write(const char *type, const uint8_t *data, size_t size, char *out_hex);
uint8_t *object_read(const char *hex, char *out_type, size_t *out_size);

#endif /* OBJECT_H */
