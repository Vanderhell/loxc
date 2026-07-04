#ifndef LOXC_TAB_H
#define LOXC_TAB_H

#include "loxc.h"

#define LOXC_TAB_MAGIC "LOXT"  /* 4 bytes */
#define LOXC_TAB_VERSION 1
#define LOXC_TAB_HEADER_SIZE 24u
#define LOXC_TAB_TRAILER_SIZE 4u

enum {
  LOXC_TAB_MAX_FILE_SIZE = 16u * 1024u * 1024u,
  LOXC_TAB_MAX_DATA_SIZE = 16u * 1024u * 1024u,
  LOXC_TAB_MAX_SYMBOLS = 16384u,
  LOXC_TAB_MAX_DICT_ENTRIES = 16384u,
  LOXC_TAB_MAX_DICT_DATA_BYTES = 8u * 1024u * 1024u,
  LOXC_TAB_MAX_LEVEL_COUNT = 1024u,
  LOXC_TAB_MAX_ALLOC_TOTAL = 24u * 1024u * 1024u
};

/*
 * .loxctab on-disk layout.
 *
 * All multi-byte integer fields use little-endian serialization. The file
 * format is byte-packed and must not be read or written via C struct layout.
 *
 * Header (24 B):
 *   bytes 0-3:   magic "LOXT"
 *   byte 4:      version (=1)
 *   byte 5:      strategy_id
 *   byte 6:      base_size (0/4/8)
 *   byte 7:      bits_per_level (0/4/6)
 *   bytes 8-9:   level_count (u16 LE)
 *   bytes 10-11: reserved
 *   bytes 12-15: symbol_count (u32 LE)
 *   bytes 16-19: dict_count (u32 LE)
 *   bytes 20-23: data_size (u32 LE)
 *
 * Data section (data_size bytes):
 *   byte_to_symbol[256]: 256 x u32 LE = 1024 B
 *   symbols[N]:          N x (u8 type + u32 byte_or_idx) = N x 5 B
 *   dict_offsets[D+1]:   (D+1) x u32 LE
 *   dict_data_size:      u32 LE
 *   dict_data[...]       dict_data_size bytes
 *
 * Trailer (4 B):
 *   crc32 of header + data (u32 LE)
 */

typedef struct {
  int code;
  size_t offset;
  const char *message;
} loxc_tab_error_t;

loxc_module_t *loxc_module_load_from_file(const char *path);
loxc_module_t *loxc_module_load_from_memory(const uint8_t *buf, size_t buf_size,
                                            const char *name);
int loxc_module_load_from_file_ex(const char *path,
                                  loxc_module_t **out_module,
                                  loxc_tab_error_t *out_error);
int loxc_module_load_from_memory_ex(const uint8_t *buf, size_t buf_size,
                                    const char *name,
                                    loxc_module_t **out_module,
                                    loxc_tab_error_t *out_error);
/*
 * Unloads a runtime-loaded module after it has been unregistered.
 * Returns LOXC_ERR_BUSY for still-registered modules and
 * LOXC_ERR_INVALID_MODULE for static/generated modules.
 */
int loxc_module_unload(loxc_module_t *module);

#endif /* LOXC_TAB_H */
