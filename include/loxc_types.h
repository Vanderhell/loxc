#ifndef LOXC_TYPES_H
#define LOXC_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  LOXC_OK = 0,
  LOXC_ERR_NULL = 1,
  LOXC_ERR_TRUNCATED = 2,
  LOXC_ERR_OVERFLOW = 3,
  LOXC_ERR_INVALID_MAGIC = 4,
  LOXC_ERR_SYMBOL_NOT_FOUND = 5,
  LOXC_ERR_INVALID_FORMAT = 6,
  LOXC_ERR_MODULE_NOT_FOUND = 7,
  LOXC_ERR_REGISTRY_FULL = 8,
  LOXC_ERR_DUPLICATE_MODULE = 9,
  LOXC_ERR_INVALID_MODULE = 10,
  LOXC_ERR_BUSY = 11
} loxc_err_t;

/*
 * Serialized .loxc files begin with the three-byte ASCII prefix "LXC".
 * The fourth serialized byte is the module id, not part of the magic prefix.
 */
static const uint8_t LOXC_MAGIC[3] = { 'L', 'X', 'C' };

#endif /* LOXC_TYPES_H */
