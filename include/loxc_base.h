#ifndef LOXC_BASE_H
#define LOXC_BASE_H

#include "loxc_types.h"
#include "loxc_lower.h"
#include "loxc_upper.h"
#include "loxc_digits.h"
#include "loxc_punct.h"
#include "loxc_whitespace.h"
#include "loxc_plain.h"
#include "loxc_stream.h"
#include "loxc_matrix.h"
#include "loxc_dict.h"

const uint8_t *loxc_magic_bytes(void);

enum {
  LOXC_FLAG_CRC = 1u << 7,
  LOXC_FLAG_DICT = 1u << 6
};

#define LOXC_FLAG_EMBEDDED_TABLE 0x04u

typedef enum {
  LOXC_STRATEGY_FLAT_FIXED_WIDTH = 0,
  LOXC_STRATEGY_HIERARCHICAL_8   = 1,
  LOXC_STRATEGY_HIERARCHICAL_4   = 2,
} loxc_strategy_t;

typedef struct {
  uint8_t  module_id;
  uint8_t  version;       /* = 2 (v2 format) */
  uint8_t  flags;
  uint8_t  strategy_id;   /* loxc_strategy_t: encoding strategy */
  uint16_t data_len;      /* encoded data length in bytes */
  uint16_t level_count;   /* for hierarchical strategies: number of levels (0 for FLAT) */
  uint8_t  reserved[4];   /* module-defined extension bytes */
  uint32_t crc32;         /* valid iff (flags & LOXC_FLAG_CRC) */
} loxc_header_t;

int loxc_header_write(loxc_writer_t *w, const loxc_header_t *h);
int loxc_header_read(loxc_reader_t *r, loxc_header_t *h);
int loxc_header_validate(const loxc_header_t *h);

uint32_t loxc_crc32(const uint8_t *data, size_t len);

#endif /* LOXC_BASE_H */
