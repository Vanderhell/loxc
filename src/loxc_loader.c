#include "loxc_tab.h"

#include "loxc_base.h"
#include "loxc_stream.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint8_t type;
  uint32_t byte_or_idx;
} loxc_sym_t;

typedef struct {
  uint32_t magic;
  char *table_name;
  uint8_t module_id;
  uint8_t format_version;
  uint8_t strategy_id;
  uint8_t base_size;
  uint8_t bits_per_level;
  uint8_t direct_slots;
  uint8_t raw_pos;
  uint8_t continue_pos;
  uint16_t level_count;
  uint32_t table_fingerprint;
  uint32_t symbol_count;
  uint32_t dict_count;
  uint32_t *byte_to_symbol;
  loxc_sym_t *symbols;
  uint32_t *dict_offsets;
  uint8_t *dict_data;
  uint32_t dict_data_size;
  uint8_t *raw_loxctab;
  size_t raw_loxctab_size;
} loxc_loaded_module_ctx_t;

enum {
  LOXC_LOADED_MODULE_MAGIC = 0x4c4f5844u
};

static uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8u) |
         ((uint32_t)p[2] << 16u) |
         ((uint32_t)p[3] << 24u);
}

typedef struct {
  uint8_t format_version;
  uint8_t module_id;
  uint8_t strategy_id;
  uint8_t base_size;
  uint8_t bits_per_level;
  uint8_t direct_slots;
  uint8_t raw_pos;
  uint8_t continue_pos;
  uint16_t level_count;
  uint32_t table_fingerprint;
  uint32_t symbol_count;
  uint32_t dict_count;
  uint32_t data_size;
  uint32_t dict_data_size;
  char table_name[LOXC_TAB_MAX_NAME_LEN + 1u];
  const uint8_t *data;
  const uint8_t *symbols_data;
  const uint8_t *offsets_data;
  const uint8_t *dict_data;
} loxc_parsed_table_t;

static int loxc__tab_fail(loxc_tab_error_t *out_error,
                          int code,
                          size_t offset,
                          const char *message) {
  if (out_error != NULL) {
    out_error->code = code;
    out_error->offset = offset;
    out_error->message = message;
  }
  return code;
}

static int loxc__checked_add_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a > SIZE_MAX - b) return LOXC_ERR_OVERFLOW;
  *out = a + b;
  return LOXC_OK;
}

static int loxc__checked_mul_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a != 0u && b > SIZE_MAX / a) return LOXC_ERR_OVERFLOW;
  *out = a * b;
  return LOXC_OK;
}

static int loxc__validate_strategy_config(uint8_t strategy_id,
                                          uint8_t base_size,
                                          uint8_t bits_per_level,
                                          uint16_t level_count,
                                          uint8_t *out_direct_slots,
                                          uint8_t *out_raw_pos,
                                          uint8_t *out_continue_pos) {
  uint8_t direct_slots = 0u;
  uint8_t raw_pos = 0u;
  uint8_t continue_pos = 0u;

  switch (strategy_id) {
    case LOXC_STRATEGY_FLAT_FIXED_WIDTH:
      if (base_size != 0u || bits_per_level != 0u || level_count != 0u) {
        return LOXC_ERR_INVALID_FORMAT;
      }
      direct_slots = 0u;
      raw_pos = 0u;
      continue_pos = 0u;
      break;
    case LOXC_STRATEGY_HIERARCHICAL_4:
      if (base_size != 4u || bits_per_level != 4u || level_count == 0u ||
          level_count > LOXC_TAB_MAX_LEVEL_COUNT) {
        return LOXC_ERR_INVALID_FORMAT;
      }
      direct_slots = 14u;
      raw_pos = 14u;
      continue_pos = 15u;
      break;
    case LOXC_STRATEGY_HIERARCHICAL_8:
      if (base_size != 8u || bits_per_level != 6u || level_count == 0u ||
          level_count > LOXC_TAB_MAX_LEVEL_COUNT) {
        return LOXC_ERR_INVALID_FORMAT;
      }
      direct_slots = 55u;
      raw_pos = 55u;
      continue_pos = 56u;
      break;
    default:
      return LOXC_ERR_INVALID_FORMAT;
  }

  if (out_direct_slots != NULL) *out_direct_slots = direct_slots;
  if (out_raw_pos != NULL) *out_raw_pos = raw_pos;
  if (out_continue_pos != NULL) *out_continue_pos = continue_pos;
  return LOXC_OK;
}

static int loxc__parse_table(const uint8_t *buf,
                             size_t file_size,
                             loxc_parsed_table_t *out_parsed,
                             loxc_tab_error_t *out_error) {
  loxc_parsed_table_t parsed;
  size_t expected_size = 0u;
  size_t symbols_bytes = 0u;
  size_t offsets_bytes = 0u;
  size_t min_data_size = 0u;
  size_t after_symbols = 0u;
  size_t after_offsets = 0u;
  size_t dict_data_end = 0u;
  size_t i = 0u;
  uint32_t char_symbol_ids[256];

  if (out_parsed == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_NULL, 0u, "missing parsed output");
  }
  memset(&parsed, 0, sizeof(parsed));
  if (buf == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_NULL, 0u, "null table buffer");
  }

  if (file_size < LOXC_TAB_HEADER_SIZE + LOXC_TAB_TRAILER_SIZE) {
    return loxc__tab_fail(out_error, LOXC_ERR_TRUNCATED, 0u, "table shorter than minimum size");
  }
  if (file_size > LOXC_TAB_MAX_FILE_SIZE) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "table exceeds maximum supported size");
  }
  if (memcmp(buf, LOXC_TAB_MAGIC, 4u) != 0) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_MAGIC, 0u, "invalid loxctab magic");
  }
  if (buf[4] != (uint8_t)LOXC_TAB_VERSION) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 4u, "unsupported loxctab version");
  }
  if (buf[61] != 0u || buf[62] != 0u || buf[63] != 0u) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 61u, "reserved loxctab header bytes must be zero");
  }

  parsed.format_version = buf[5];
  parsed.module_id = buf[6];
  parsed.strategy_id = buf[7];
  parsed.base_size = buf[8];
  parsed.bits_per_level = buf[9];
  parsed.level_count = read_u16_le(buf + 10u);
  parsed.symbol_count = read_u32_le(buf + 12u);
  parsed.dict_count = read_u32_le(buf + 16u);
  parsed.data_size = read_u32_le(buf + 20u);
  parsed.table_fingerprint = read_u32_le(buf + 24u);
  if (buf[28] > LOXC_TAB_MAX_NAME_LEN) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 28u, "invalid loxctab table name length");
  }
  memcpy(parsed.table_name, buf + 29u, LOXC_TAB_MAX_NAME_LEN);
  parsed.table_name[buf[28]] = '\0';
  for (i = (size_t)buf[28]; i < LOXC_TAB_MAX_NAME_LEN; i++) {
    if (parsed.table_name[i] != '\0') {
      return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 29u + i,
                            "loxctab table name padding must be zero");
    }
  }
  if (parsed.format_version == 0u) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 5u, "invalid loxctab module format version");
  }
  if (parsed.table_fingerprint == 0u) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 24u, "missing loxctab table fingerprint");
  }
  if (parsed.table_name[0] == '\0') {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 28u, "missing loxctab table name");
  }

  if (parsed.symbol_count == 0u || parsed.symbol_count > LOXC_TAB_MAX_SYMBOLS) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 12u, "invalid loxctab symbol count");
  }
  if (parsed.dict_count > LOXC_TAB_MAX_DICT_ENTRIES) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 16u, "invalid loxctab dictionary count");
  }
  if (parsed.data_size > LOXC_TAB_MAX_DATA_SIZE) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 20u, "loxctab data section exceeds limit");
  }

  if (loxc__validate_strategy_config(parsed.strategy_id, parsed.base_size,
                                     parsed.bits_per_level, parsed.level_count,
                                     &parsed.direct_slots,
                                     &parsed.raw_pos,
                                     &parsed.continue_pos) != LOXC_OK) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 7u, "invalid loxctab strategy/base/bits/level configuration");
  }
  if (parsed.strategy_id != LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
    const uint32_t max_symbols =
        (uint32_t)parsed.level_count * (uint32_t)parsed.direct_slots;
    if (parsed.symbol_count > max_symbols) {
      return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 12u,
                            "hierarchical loxctab symbol count exceeds representable slots");
    }
  }

  if (loxc__checked_add_size((size_t)LOXC_TAB_HEADER_SIZE, (size_t)parsed.data_size,
                             &expected_size) != LOXC_OK ||
      loxc__checked_add_size(expected_size, (size_t)LOXC_TAB_TRAILER_SIZE,
                             &expected_size) != LOXC_OK) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 20u, "loxctab total size overflows");
  }
  if (file_size != expected_size) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 20u, "loxctab size does not match declared data size");
  }

  if (loxc_crc32(buf, (size_t)LOXC_TAB_HEADER_SIZE + (size_t)parsed.data_size) !=
      read_u32_le(buf + file_size - LOXC_TAB_TRAILER_SIZE)) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                          file_size - LOXC_TAB_TRAILER_SIZE,
                          "loxctab crc mismatch");
  }

  if (loxc__checked_mul_size((size_t)parsed.symbol_count, 5u, &symbols_bytes) != LOXC_OK ||
      loxc__checked_add_size((size_t)parsed.dict_count, 1u, &offsets_bytes) != LOXC_OK ||
      loxc__checked_mul_size(offsets_bytes, 4u, &offsets_bytes) != LOXC_OK) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 12u, "loxctab section size overflows");
  }

  min_data_size = 1024u;
  if (loxc__checked_add_size(min_data_size, symbols_bytes, &min_data_size) != LOXC_OK ||
      loxc__checked_add_size(min_data_size, offsets_bytes, &min_data_size) != LOXC_OK ||
      loxc__checked_add_size(min_data_size, 4u, &min_data_size) != LOXC_OK) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 20u, "loxctab minimum data size overflows");
  }
  if ((size_t)parsed.data_size < min_data_size) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 20u, "loxctab data section too small");
  }

  parsed.data = buf + LOXC_TAB_HEADER_SIZE;
  parsed.symbols_data = parsed.data + 1024u;
  if (loxc__checked_add_size(1024u, symbols_bytes, &after_symbols) != LOXC_OK ||
      loxc__checked_add_size(after_symbols, offsets_bytes, &after_offsets) != LOXC_OK) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 20u, "loxctab section offsets overflow");
  }
  parsed.offsets_data = parsed.data + after_symbols;
  parsed.dict_data_size = read_u32_le(parsed.data + after_offsets);
  if (parsed.dict_data_size > LOXC_TAB_MAX_DICT_DATA_BYTES) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                          LOXC_TAB_HEADER_SIZE + after_offsets,
                          "loxctab dictionary data exceeds limit");
  }
  parsed.dict_data = parsed.data + after_offsets + 4u;

  if (loxc__checked_add_size(after_offsets, 4u, &dict_data_end) != LOXC_OK ||
      loxc__checked_add_size(dict_data_end, (size_t)parsed.dict_data_size,
                             &dict_data_end) != LOXC_OK) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 20u, "loxctab dictionary data overflows");
  }
  if (dict_data_end != (size_t)parsed.data_size) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                          LOXC_TAB_HEADER_SIZE + after_offsets,
                          "loxctab dictionary data does not fill declared data section");
  }

  for (i = 0u; i < 256u; i++) char_symbol_ids[i] = 0xFFFFFFFFu;
  for (i = 0u; i < (size_t)parsed.symbol_count; i++) {
    const size_t symbol_off = (size_t)LOXC_TAB_HEADER_SIZE + 1024u + (i * 5u);
    const uint8_t type = parsed.symbols_data[i * 5u];
    const uint32_t value = read_u32_le(parsed.symbols_data + (i * 5u) + 1u);

    if (type == 0u) {
      if (value > 0xFFu) {
        return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, symbol_off,
                              "character symbol value exceeds byte range");
      }
      if (char_symbol_ids[value] != 0xFFFFFFFFu) {
        return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, symbol_off,
                              "duplicate character symbol");
      }
      char_symbol_ids[value] = (uint32_t)i;
    } else if (type == 1u) {
      if (value >= parsed.dict_count) {
        return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, symbol_off,
                              "dictionary symbol index out of range");
      }
    } else {
      return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, symbol_off,
                            "invalid symbol type");
    }
  }

  for (i = 0u; i < 256u; i++) {
    const uint32_t sym_id = read_u32_le(parsed.data + (i * 4u));
    if (sym_id == 0xFFFFFFFFu) {
      if (char_symbol_ids[i] != 0xFFFFFFFFu) {
        return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                              LOXC_TAB_HEADER_SIZE + (i * 4u),
                              "character symbol missing from byte-to-symbol table");
      }
      continue;
    }
    if (sym_id >= parsed.symbol_count) {
      return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                            LOXC_TAB_HEADER_SIZE + (i * 4u),
                            "byte-to-symbol entry out of range");
    }
    if (char_symbol_ids[i] != sym_id) {
      return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                            LOXC_TAB_HEADER_SIZE + (i * 4u),
                            "byte-to-symbol entry does not reference matching character symbol");
    }
  }

  if (parsed.dict_count == 0u && parsed.dict_data_size != 0u) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                          LOXC_TAB_HEADER_SIZE + after_offsets,
                          "dictionary data must be empty when dictionary count is zero");
  }

  for (i = 0u; i < (size_t)parsed.dict_count + 1u; i++) {
    const uint32_t off = read_u32_le(parsed.offsets_data + (i * 4u));
    if (i == 0u && off != 0u) {
      return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                            LOXC_TAB_HEADER_SIZE + after_symbols,
                            "dictionary offsets must start at zero");
    }
    if (off > parsed.dict_data_size) {
      return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                            LOXC_TAB_HEADER_SIZE + after_symbols + (i * 4u),
                            "dictionary offset exceeds dictionary data size");
    }
    if (i > 0u) {
      const uint32_t prev = read_u32_le(parsed.offsets_data + ((i - 1u) * 4u));
      if (off < prev) {
        return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                              LOXC_TAB_HEADER_SIZE + after_symbols + (i * 4u),
                              "dictionary offsets must be monotonic");
      }
    }
  }
  if (read_u32_le(parsed.offsets_data + ((size_t)parsed.dict_count * 4u)) !=
      parsed.dict_data_size) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT,
                          LOXC_TAB_HEADER_SIZE + after_symbols +
                              ((size_t)parsed.dict_count * 4u),
                          "dictionary offset sentinel must equal dictionary data size");
  }

  *out_parsed = parsed;
  return LOXC_OK;
}

static char *loxc_strdup_range(const char *start, size_t len) {
  char *out = (char *)malloc(len + 1u);
  if (out == NULL) return NULL;
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

/*
 * Runtime-loaded module names are derived from the table path basename.
 * The contract is:
 * - strip both '/' and '\' directory separators
 * - strip the trailing ".loxctab" extension
 * - if the resulting basename starts with the generated-table prefix
 *   "loxc_", strip that prefix so generated tables register under their
 *   logical module name
 */
static char *module_name_from_path(const char *path) {
  const char *base = strrchr(path, '/');
  const char *base_win = strrchr(path, '\\');
  if (base_win != NULL && (base == NULL || base_win > base)) {
    base = base_win;
  }
  base = (base == NULL) ? path : base + 1;

  const char *start = base;
  if (strncmp(start, "loxc_", 5) == 0) start += 5;

  size_t len = strlen(start);
  const char suffix[] = ".loxctab";
  const size_t suffix_len = sizeof(suffix) - 1u;
  if (len >= suffix_len &&
      strcmp(start + len - suffix_len, suffix) == 0) {
    len -= suffix_len;
  }

  return loxc_strdup_range(start, len);
}

static void free_loaded_ctx(loxc_loaded_module_ctx_t *ctx) {
  if (ctx == NULL) return;
  free(ctx->table_name);
  free(ctx->byte_to_symbol);
  free(ctx->symbols);
  free(ctx->dict_offsets);
  free(ctx->dict_data);
  free(ctx->raw_loxctab);
  free(ctx);
}

static int loaded_ctx_from_module(const loxc_module_t *module,
                                  const loxc_loaded_module_ctx_t **out_ctx) {
  const loxc_loaded_module_ctx_t *ctx = NULL;

  if (out_ctx == NULL) return LOXC_ERR_NULL;
  *out_ctx = NULL;
  if (module == NULL) return LOXC_ERR_NULL;

  ctx = (const loxc_loaded_module_ctx_t *)module->private_data;
  if (ctx == NULL || ctx->magic != LOXC_LOADED_MODULE_MAGIC) {
    return LOXC_ERR_INVALID_MODULE;
  }

  *out_ctx = ctx;
  return LOXC_OK;
}

int loxc_registry_module_status(const loxc_module_t *module,
                                int *out_registered,
                                int *out_busy);

static int loaded_ctx_from_module_mutable(loxc_module_t *module,
                                          loxc_loaded_module_ctx_t **out_ctx) {
  loxc_loaded_module_ctx_t *ctx = NULL;

  if (out_ctx == NULL) return LOXC_ERR_NULL;
  *out_ctx = NULL;
  if (module == NULL) return LOXC_ERR_NULL;

  ctx = (loxc_loaded_module_ctx_t *)module->private_data;
  if (ctx == NULL || ctx->magic != LOXC_LOADED_MODULE_MAGIC) {
    return LOXC_ERR_INVALID_MODULE;
  }

  *out_ctx = ctx;
  return LOXC_OK;
}

static uint8_t bits_needed_u32(uint32_t n) {
  if (n <= 1u) return 1u;
  uint8_t bits = 0;
  uint32_t v = n - 1u;
  while (v > 0) {
    bits++;
    v >>= 1u;
  }
  return bits;
}

static int loxc_generic_encode(const char *text, size_t text_len,
                               loxc_writer_t *w,
                               const loxc_loaded_module_ctx_t *ctx) {
  if (text == NULL || w == NULL || ctx == NULL) return LOXC_ERR_NULL;

  const uint8_t flat_bits = bits_needed_u32(ctx->symbol_count + 1u);
  size_t i = 0;
  while (i < text_len) {
    uint32_t sym_id = 0xFFFFFFFFu;
    size_t consumed = 1u;

    for (uint32_t d = 0; d < ctx->dict_count; d++) {
      const uint32_t off = ctx->dict_offsets[d];
      const uint32_t dlen = ctx->dict_offsets[d + 1u] - off;
      if (dlen == 0) continue;
      if (i + (size_t)dlen <= text_len &&
          memcmp((const uint8_t *)text + i, ctx->dict_data + off,
                 (size_t)dlen) == 0) {
        for (uint32_t s = 0; s < ctx->symbol_count; s++) {
          if (ctx->symbols[s].type == 1u &&
              ctx->symbols[s].byte_or_idx == d) {
            sym_id = s;
            break;
          }
        }
        consumed = (size_t)dlen;
        break;
      }
    }

    if (sym_id == 0xFFFFFFFFu) {
      sym_id = ctx->byte_to_symbol[(uint8_t)text[i]];
      if (sym_id == 0xFFFFFFFFu) {
        if (ctx->strategy_id == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
          int rc = loxc_write_bits(w, ctx->symbol_count, flat_bits);
          if (rc != LOXC_OK) return rc;
          rc = loxc_write_bits(w, (uint8_t)text[i], 8u);
          if (rc != LOXC_OK) return rc;
        } else {
          int rc = loxc_write_bits(w, ctx->raw_pos, ctx->bits_per_level);
          if (rc != LOXC_OK) return rc;
          rc = loxc_write_bits(w, (uint8_t)text[i], 8u);
          if (rc != LOXC_OK) return rc;
        }
        i += 1u;
        continue;
      }
      consumed = 1u;
    }

    if (ctx->strategy_id == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
      int rc = loxc_write_bits(w, sym_id, flat_bits);
      if (rc != LOXC_OK) return rc;
    } else {
      if (ctx->direct_slots == 0 || ctx->bits_per_level == 0) {
        return LOXC_ERR_INVALID_FORMAT;
      }
      const uint32_t level = sym_id / (uint32_t)ctx->direct_slots;
      const uint32_t pos = sym_id % (uint32_t)ctx->direct_slots;
      for (uint32_t l = 0; l < level; l++) {
        int rc = loxc_write_bits(w, (uint32_t)ctx->continue_pos,
                                 ctx->bits_per_level);
        if (rc != LOXC_OK) return rc;
      }
      int rc = loxc_write_bits(w, pos, ctx->bits_per_level);
      if (rc != LOXC_OK) return rc;
    }

    i += consumed;
  }
  return LOXC_OK;
}

static int loxc_generic_decode(loxc_reader_t *r,
                               char *out, size_t *inout_len,
                               const loxc_loaded_module_ctx_t *ctx) {
  if (r == NULL || out == NULL || inout_len == NULL || ctx == NULL) {
    return LOXC_ERR_NULL;
  }

  const size_t cap = *inout_len;
  size_t written = 0;
  const uint8_t flat_bits = bits_needed_u32(ctx->symbol_count + 1u);

  for (;;) {
    if (written == cap) goto done;

    uint32_t sym_id = 0;
    if (ctx->strategy_id == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
      int rc = loxc_read_bits(r, flat_bits, &sym_id);
      if (rc != LOXC_OK) return rc;
      if (sym_id == ctx->symbol_count) {
        uint32_t raw_byte = 0;
        rc = loxc_read_bits(r, 8u, &raw_byte);
        if (rc != LOXC_OK) return rc;
        if (written >= cap) {
          *inout_len = written;
          return LOXC_ERR_OVERFLOW;
        }
        out[written++] = (char)(uint8_t)raw_byte;
        continue;
      }
    } else {
      if (ctx->direct_slots == 0 || ctx->bits_per_level == 0) {
        return LOXC_ERR_INVALID_FORMAT;
      }

      uint32_t level = 0;
      uint32_t pos = 0;
      int found = 0;
      for (level = 0; level < (uint32_t)ctx->level_count; level++) {
        uint32_t val = 0;
        int rc = loxc_read_bits(r, ctx->bits_per_level, &val);
        if (rc != LOXC_OK) return rc;
        if (val < (uint32_t)ctx->direct_slots) {
          pos = val;
          found = 1;
          break;
        }
        if (val == (uint32_t)ctx->raw_pos) {
          uint32_t raw_byte = 0;
          rc = loxc_read_bits(r, 8u, &raw_byte);
          if (rc != LOXC_OK) return rc;
          if (written >= cap) {
            *inout_len = written;
            return LOXC_ERR_OVERFLOW;
          }
          out[written++] = (char)(uint8_t)raw_byte;
          found = 2;
          break;
        }
        if (val != (uint32_t)ctx->continue_pos) return LOXC_ERR_INVALID_FORMAT;
      }
      if (found == 2) continue;
      if (!found) return LOXC_ERR_INVALID_FORMAT;
      sym_id = level * (uint32_t)ctx->direct_slots + pos;
    }

    if (sym_id >= ctx->symbol_count) return LOXC_ERR_INVALID_FORMAT;
    const loxc_sym_t sym = ctx->symbols[sym_id];
    if (sym.type == 0u) {
      if (written >= cap) {
        *inout_len = written;
        return LOXC_ERR_OVERFLOW;
      }
      out[written++] = (char)(uint8_t)(sym.byte_or_idx & 0xFFu);
    } else if (sym.type == 1u) {
      if (sym.byte_or_idx >= ctx->dict_count) return LOXC_ERR_INVALID_FORMAT;
      const uint32_t off = ctx->dict_offsets[sym.byte_or_idx];
      const uint32_t dlen =
          ctx->dict_offsets[sym.byte_or_idx + 1u] - off;
      if (written + (size_t)dlen > cap) {
        *inout_len = written;
        return LOXC_ERR_OVERFLOW;
      }
      memcpy(out + written, ctx->dict_data + off, (size_t)dlen);
      written += (size_t)dlen;
    } else {
      return LOXC_ERR_INVALID_FORMAT;
    }
  }

done:
  *inout_len = written;
  return LOXC_OK;
}

static int loaded_encode_stub(const uint8_t *in, size_t in_len, uint8_t *out,
                              size_t out_cap, size_t *out_len) {
  (void)in;
  (void)in_len;
  (void)out;
  (void)out_cap;
  if (out_len != NULL) *out_len = 0;
  return LOXC_ERR_INVALID_MODULE;
}

static int loaded_decode_stub(const uint8_t *in, size_t in_len, uint8_t *out,
                              size_t out_cap, size_t *out_len) {
  (void)in;
  (void)in_len;
  (void)out;
  (void)out_cap;
  if (out_len != NULL) *out_len = 0;
  return LOXC_ERR_INVALID_MODULE;
}

int loxc_loaded_module_encode(const loxc_module_t *module,
                              const uint8_t *in, size_t in_len, uint8_t *out,
                              size_t out_cap, size_t *out_len) {
  const loxc_loaded_module_ctx_t *ctx = NULL;

  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;
  if (in_len > 0xFFFFFFFFu) return LOXC_ERR_OVERFLOW;

  int rc = loaded_ctx_from_module(module, &ctx);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  h.module_id = ctx->module_id;
  h.version = LOXC_HEADER_VERSION_V2;
  h.flags = 0u;
  h.strategy_id = ctx->strategy_id;
  h.payload_len = 0u;
  h.level_count = ctx->level_count;
  h.uncompressed_len = (uint32_t)in_len;
  h.table_fingerprint = ctx->table_fingerprint;
  h.crc32 = 0u;

  const size_t header_bytes = loxc_header_size(&h);
  if (out_cap < header_bytes) return LOXC_ERR_OVERFLOW;

  loxc_writer_t w;
  rc = loxc_writer_init(&w, out + header_bytes, out_cap - header_bytes);
  if (rc != LOXC_OK) return rc;

  rc = loxc_generic_encode((const char *)in, in_len, &w, ctx);
  if (rc != LOXC_OK) return rc;
  rc = loxc_writer_flush(&w);
  if (rc != LOXC_OK) return rc;

  const size_t data_bytes = loxc_writer_size(&w);
  if (data_bytes > LOXC_HEADER_MAX_EXACT_PAYLOAD_LEN) return LOXC_ERR_OVERFLOW;
  h.payload_len = (uint16_t)data_bytes;
  const size_t total = header_bytes + data_bytes;

  loxc_writer_t hw;
  rc = loxc_writer_init(&hw, out, header_bytes);
  if (rc != LOXC_OK) return rc;
  rc = loxc_header_write(&hw, &h);
  if (rc != LOXC_OK) return rc;
  if (loxc_writer_size(&hw) != header_bytes) return LOXC_ERR_INVALID_FORMAT;

  *out_len = total;
  return LOXC_OK;
}

int loxc_loaded_module_decode(const loxc_module_t *module,
                              const uint8_t *in, size_t in_len, uint8_t *out,
                              size_t out_cap, size_t *out_len) {
  const loxc_loaded_module_ctx_t *ctx = NULL;

  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;

  int rc = loaded_ctx_from_module(module, &ctx);
  if (rc != LOXC_OK) return rc;

  loxc_reader_t hr;
  rc = loxc_reader_init(&hr, in, in_len);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  rc = loxc_header_read(&hr, &h);
  if (rc != LOXC_OK) return rc;
  if (h.module_id != ctx->module_id) return LOXC_ERR_INVALID_MAGIC;
  if (h.version != LOXC_HEADER_VERSION_V2) return LOXC_ERR_INVALID_MAGIC;
  if (h.flags != 0u) return LOXC_ERR_INVALID_MAGIC;
  if (h.table_fingerprint != ctx->table_fingerprint) return LOXC_ERR_INVALID_MAGIC;
  if (h.strategy_id != ctx->strategy_id) return LOXC_ERR_INVALID_MAGIC;
  if (h.level_count != ctx->level_count) return LOXC_ERR_INVALID_MAGIC;

  const size_t header_bytes = loxc_header_size(&h);
  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;
  size_t data_len = 0;
  rc = loxc_header_resolve_payload_len(&h, in_len - header_bytes, &data_len);
  if (rc != LOXC_OK) return rc;
  if ((size_t)h.uncompressed_len > out_cap) return LOXC_ERR_OVERFLOW;

  loxc_reader_t r;
  rc = loxc_reader_init(&r, in + header_bytes, data_len);
  if (rc != LOXC_OK) return rc;

  size_t cap = (size_t)h.uncompressed_len;
  rc = loxc_generic_decode(&r, (char *)out, &cap, ctx);
  if (rc != LOXC_OK) return rc;
  if (cap != (size_t)h.uncompressed_len) return LOXC_ERR_TRUNCATED;
  rc = loxc_reader_finish_zero_padding(&r);
  if (rc != LOXC_OK) return rc;
  *out_len = cap;
  return LOXC_OK;
}

int loxc_module_load_from_memory_ex(const uint8_t *buf, size_t buf_size,
                                    const char *name,
                                    loxc_module_t **out_module,
                                    loxc_tab_error_t *out_error) {
  loxc_parsed_table_t parsed;
  loxc_loaded_module_ctx_t *ctx = NULL;
  loxc_module_t *module = NULL;
  char *module_name = NULL;
  size_t offsets_count = 0u;
  size_t alloc_total = 0u;
  size_t bytes = 0u;
  size_t i = 0u;

  if (out_module == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_NULL, 0u, "missing module output");
  }
  *out_module = NULL;
  if (name == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_NULL, 0u, "null module name");
  }

  if (loxc__parse_table(buf, buf_size, &parsed, out_error) != LOXC_OK) {
    return (out_error != NULL) ? out_error->code : LOXC_ERR_INVALID_FORMAT;
  }

  offsets_count = (size_t)parsed.dict_count + 1u;
  if (loxc__checked_mul_size(256u, sizeof(uint32_t), &alloc_total) != LOXC_OK ||
      loxc__checked_mul_size((size_t)parsed.symbol_count, sizeof(loxc_sym_t),
                             &bytes) != LOXC_OK ||
      loxc__checked_add_size(alloc_total, bytes, &alloc_total) != LOXC_OK ||
      loxc__checked_mul_size(offsets_count, sizeof(uint32_t), &bytes) != LOXC_OK ||
      loxc__checked_add_size(alloc_total, bytes, &alloc_total) != LOXC_OK ||
      loxc__checked_add_size(alloc_total, (size_t)parsed.dict_data_size,
                             &alloc_total) != LOXC_OK ||
      loxc__checked_add_size(alloc_total, buf_size, &alloc_total) != LOXC_OK) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "loxctab allocation size overflows");
  }
  if (alloc_total > LOXC_TAB_MAX_ALLOC_TOTAL) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "loxctab allocations exceed limit");
  }

  ctx = (loxc_loaded_module_ctx_t *)calloc(1u, sizeof(loxc_loaded_module_ctx_t));
  if (ctx == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to allocate module context");
  }

  ctx->byte_to_symbol = (uint32_t *)malloc(256u * sizeof(uint32_t));
  ctx->symbols = (loxc_sym_t *)malloc((size_t)parsed.symbol_count * sizeof(loxc_sym_t));
  ctx->dict_offsets = (uint32_t *)malloc(offsets_count * sizeof(uint32_t));
  if (ctx->byte_to_symbol == NULL || ctx->symbols == NULL ||
      ctx->dict_offsets == NULL) {
    free_loaded_ctx(ctx);
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to allocate loxctab tables");
  }
  if (parsed.dict_data_size > 0u) {
    ctx->dict_data = (uint8_t *)malloc((size_t)parsed.dict_data_size);
    if (ctx->dict_data == NULL) {
      free_loaded_ctx(ctx);
      return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to allocate loxctab dictionary data");
    }
  }
  ctx->raw_loxctab = (uint8_t *)malloc(buf_size);
  if (ctx->raw_loxctab == NULL) {
    free_loaded_ctx(ctx);
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to retain raw loxctab blob");
  }

  for (i = 0u; i < 256u; i++) {
    ctx->byte_to_symbol[i] = read_u32_le(parsed.data + (i * 4u));
  }
  for (i = 0u; i < (size_t)parsed.symbol_count; i++) {
    ctx->symbols[i].type = parsed.symbols_data[i * 5u];
    ctx->symbols[i].byte_or_idx =
        read_u32_le(parsed.symbols_data + (i * 5u) + 1u);
  }
  for (i = 0u; i < offsets_count; i++) {
    ctx->dict_offsets[i] = read_u32_le(parsed.offsets_data + (i * 4u));
  }
  if (parsed.dict_data_size > 0u) {
    memcpy(ctx->dict_data, parsed.dict_data, (size_t)parsed.dict_data_size);
  }
  memcpy(ctx->raw_loxctab, buf, buf_size);

  module = (loxc_module_t *)calloc(1u, sizeof(loxc_module_t));
  if (module == NULL) {
    free_loaded_ctx(ctx);
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to allocate module object");
  }

  module_name = loxc_strdup_range(name, strlen(name));
  if (module_name == NULL) {
    free(module);
    free_loaded_ctx(ctx);
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to allocate module name");
  }

  ctx->magic = LOXC_LOADED_MODULE_MAGIC;
  ctx->table_name = loxc_strdup_range(parsed.table_name, strlen(parsed.table_name));
  if (ctx->table_name == NULL) {
    free(module_name);
    free(module);
    free_loaded_ctx(ctx);
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to allocate table name");
  }
  ctx->module_id = parsed.module_id;
  ctx->format_version = parsed.format_version;
  ctx->strategy_id = parsed.strategy_id;
  ctx->base_size = parsed.base_size;
  ctx->bits_per_level = parsed.bits_per_level;
  ctx->direct_slots = parsed.direct_slots;
  ctx->raw_pos = parsed.raw_pos;
  ctx->continue_pos = parsed.continue_pos;
  ctx->level_count = parsed.level_count;
  ctx->table_fingerprint = parsed.table_fingerprint;
  ctx->symbol_count = parsed.symbol_count;
  ctx->dict_count = parsed.dict_count;
  ctx->dict_data_size = parsed.dict_data_size;
  ctx->raw_loxctab_size = buf_size;

  module->name = module_name;
  module->table_name = ctx->table_name;
  module->module_id = ctx->module_id;
  module->version = ctx->format_version;
  module->strategy_id = ctx->strategy_id;
  module->level_count = ctx->level_count;
  module->table_fingerprint = ctx->table_fingerprint;
  module->encode = loaded_encode_stub;
  module->decode = loaded_decode_stub;
  module->private_data = ctx;
  *out_module = module;
  return LOXC_OK;
}

loxc_module_t *loxc_module_load_from_memory(const uint8_t *buf, size_t buf_size,
                                            const char *name) {
  loxc_module_t *module = NULL;
  if (loxc_module_load_from_memory_ex(buf, buf_size, name, &module, NULL) !=
      LOXC_OK) {
    return NULL;
  }
  return module;
}

int loxc_module_load_from_file_ex(const char *path,
                                  loxc_module_t **out_module,
                                  loxc_tab_error_t *out_error) {
  FILE *f = NULL;
  long file_size_long = 0;
  size_t file_size = 0u;
  uint8_t *buf = NULL;
  size_t nread = 0u;
  char *name = NULL;
  int rc = LOXC_OK;

  if (out_module == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_NULL, 0u, "missing module output");
  }
  *out_module = NULL;
  if (path == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_NULL, 0u, "null table path");
  }

  f = fopen(path, "rb");
  if (f == NULL) {
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 0u, "failed to open loxctab file");
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 0u, "failed to seek loxctab file");
  }
  file_size_long = ftell(f);
  if (file_size_long < 0) {
    fclose(f);
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 0u, "failed to determine loxctab file size");
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return loxc__tab_fail(out_error, LOXC_ERR_INVALID_FORMAT, 0u, "failed to rewind loxctab file");
  }

  file_size = (size_t)file_size_long;
  if (file_size > 0u) {
    buf = (uint8_t *)malloc(file_size);
    if (buf == NULL) {
      fclose(f);
      return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to allocate loxctab file buffer");
    }
    nread = fread(buf, 1, file_size, f);
  }
  fclose(f);
  if (nread != file_size) {
    free(buf);
    return loxc__tab_fail(out_error, LOXC_ERR_TRUNCATED, 0u, "failed to read complete loxctab file");
  }

  name = module_name_from_path(path);
  if (name == NULL) {
    free(buf);
    return loxc__tab_fail(out_error, LOXC_ERR_OVERFLOW, 0u, "failed to derive module name from path");
  }
  rc = loxc_module_load_from_memory_ex(buf, file_size, name, out_module, out_error);
  free(name);
  free(buf);
  return rc;
}

loxc_module_t *loxc_module_load_from_file(const char *path) {
  loxc_module_t *module = NULL;
  if (loxc_module_load_from_file_ex(path, &module, NULL) != LOXC_OK) return NULL;
  return module;
}

int loxc_module_unload(loxc_module_t *module) {
  loxc_loaded_module_ctx_t *ctx = NULL;
  int is_registered = 0;
  int is_busy = 0;

  if (module == NULL) return LOXC_ERR_NULL;
  if (loaded_ctx_from_module_mutable(module, &ctx) != LOXC_OK) {
    return LOXC_ERR_INVALID_MODULE;
  }
  if (loxc_registry_module_status(module, &is_registered, &is_busy) != LOXC_OK) {
    return LOXC_ERR_INVALID_MODULE;
  }
  if (is_registered || is_busy) return LOXC_ERR_BUSY;

  free_loaded_ctx(ctx);
  free((void *)module->name);
  free(module);
  return LOXC_OK;
}

int loxc_module_get_table_blob(const loxc_module_t *module,
                               const uint8_t **out_blob,
                               size_t *out_size) {
  if (out_blob == NULL || out_size == NULL) return LOXC_ERR_NULL;
  *out_blob = NULL;
  *out_size = 0;
  if (module == NULL) return LOXC_ERR_NULL;

  const loxc_loaded_module_ctx_t *ctx = NULL;
  if (loaded_ctx_from_module(module, &ctx) != LOXC_OK) return LOXC_ERR_INVALID_MODULE;
  if (ctx->raw_loxctab == NULL || ctx->raw_loxctab_size == 0) {
    return LOXC_ERR_INVALID_FORMAT;
  }

  *out_blob = ctx->raw_loxctab;
  *out_size = ctx->raw_loxctab_size;
  return LOXC_OK;
}
