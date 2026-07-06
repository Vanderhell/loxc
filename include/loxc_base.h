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

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *loxc_magic_bytes(void);

enum {
  LOXC_MAGIC_PREFIX_SIZE = LOXC_CONTAINER_MAGIC_PREFIX_SIZE,
  LOXC_HEADER_VERSION_V2 = 2u,
  LOXC_HEADER_VERSION_CURRENT = LOXC_HEADER_VERSION_V2,
  LOXC_HEADER_PAYLOAD_LEN_LEGACY_TO_EOF = 0xFFFFu,
  LOXC_HEADER_MAX_EXACT_PAYLOAD_LEN = 0xFFFEu,
  LOXC_HEADER_OFFSET_MODULE_ID = 3u,
  LOXC_HEADER_OFFSET_VERSION = 4u,
  LOXC_HEADER_OFFSET_FLAGS = 5u,
  LOXC_HEADER_OFFSET_STRATEGY_ID = 6u,
  LOXC_HEADER_OFFSET_PAYLOAD_LEN = 7u,
  LOXC_HEADER_OFFSET_LEVEL_COUNT = 9u,
  LOXC_HEADER_OFFSET_UNCOMPRESSED_LEN = 11u,
  LOXC_HEADER_OFFSET_TABLE_FINGERPRINT = 15u,
  LOXC_HEADER_SIZE_V2 = 19u,
  LOXC_FLAG_CRC = 1u << 7,   /* legacy/unsupported in v2: rejected */
  LOXC_FLAG_DICT = 1u << 6   /* legacy/unsupported in v2: rejected */
};

#define LOXC_FLAG_EMBEDDED_TABLE 0x04u
#define LOXC_CONTAINER_FLAG_EMBEDDED_TABLE LOXC_FLAG_EMBEDDED_TABLE

typedef uint8_t loxc_strategy_t;
enum {
  LOXC_STRATEGY_FLAT_FIXED_WIDTH = 0u,
  LOXC_STRATEGY_HIERARCHICAL_8 = 1u,
  LOXC_STRATEGY_HIERARCHICAL_4 = 2u
};

typedef struct {
  uint8_t  module_id;
  uint8_t  version;           /* supported on-wire version: 2 */
  uint8_t  flags;
  uint8_t  strategy_id;       /* loxc_strategy_t */
  uint16_t payload_len;       /* exact payload bytes; 0xFFFF is read-only legacy-to-EOF */
  uint16_t level_count;       /* FLAT=0, HIER*=positive */
  uint32_t uncompressed_len;  /* exact decoded output length */
  uint32_t table_fingerprint; /* exact table identity for payload binding */
  uint32_t crc32;             /* unsupported in v2; must not be serialized */
} loxc_header_t;

int loxc_header_write(loxc_writer_t *w, const loxc_header_t *h);
int loxc_header_read(loxc_reader_t *r, loxc_header_t *h);
int loxc_header_validate(const loxc_header_t *h);
size_t loxc_header_size(const loxc_header_t *h);
int loxc_header_resolve_payload_len(const loxc_header_t *h,
                                    size_t available_bytes,
                                    size_t *out_payload_len);
int loxc_reader_finish_zero_padding(loxc_reader_t *r);

uint32_t loxc_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOXC_BASE_H */
