#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "loxc_dict.h"
#include "loxc_hier.h"
#include "loxc_strategy.h"
#include "loxc_base.h"
#include "loxc_tab.h"

enum {
  LOXC_TRAIN_MAX_INPUTS = 128,
  LOXC_TRAIN_MAX_TOTAL_BYTES = 100 * 1024 * 1024
};

/* Symbol record: either a character or a dict entry */
typedef struct {
  uint32_t symbol_id;
  uint64_t count;
  uint8_t is_dict;      /* 1 = dict entry, 0 = char */
  uint8_t char_val;     /* for char symbols */
  uint8_t *dict_bytes;  /* owned copy (NULL for char symbols) */
  size_t dict_len;
} symbol_rec_t;

/* Parse command-line arguments */
static int validate_module_name(const char *name) {
  if (name == NULL || name[0] == '\0') return 0;
  for (size_t i = 0; name[i] != '\0'; i++) {
    const char c = name[i];
    const int is_lower = (c >= 'a' && c <= 'z');
    const int is_digit = (c >= '0' && c <= '9');
    const int is_uscore = (c == '_');
    if (!(is_lower || is_digit || is_uscore)) return 0;
  }
  return 1;
}

static void name_to_upper(const char *in, char *out, size_t out_cap) {
  if (out_cap == 0) return;
  size_t i = 0;
  for (; in != NULL && in[i] != '\0' && i + 1 < out_cap; i++) {
    char c = in[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    out[i] = c;
  }
  out[i] = '\0';
}

static uint8_t bits_needed_u32(uint32_t n) {
  if (n <= 1u) return 1;
  uint8_t bits = 0;
  uint32_t v = n - 1u;
  while (v > 0) {
    bits++;
    v >>= 1u;
  }
  return bits;
}

typedef struct {
  size_t idx;
  int64_t gain;
  size_t len;
} dict_candidate_t;

typedef struct {
  loxc_strategy_t strategy;
  loxc_strategy_desc_t desc;
  uint64_t payload_bits;
  uint64_t total_bits;
  uint64_t total_symbols;
  uint16_t level_count;
  size_t total_bytes;
} strategy_eval_t;

static int cmp_dict_candidate(const void *a, const void *b) {
  const dict_candidate_t *da = (const dict_candidate_t *)a;
  const dict_candidate_t *db = (const dict_candidate_t *)b;
  if (da->gain > db->gain) return -1;
  if (da->gain < db->gain) return 1;
  if (da->len > db->len) return -1;
  if (da->len < db->len) return 1;
  if (da->idx < db->idx) return -1;
  if (da->idx > db->idx) return 1;
  return 0;
}

static int checked_add_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return 0;
  if (a > SIZE_MAX - b) return 0;
  *out = a + b;
  return 1;
}

static int checked_mul_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return 0;
  if (a != 0u && b > SIZE_MAX / a) return 0;
  *out = a * b;
  return 1;
}

static int checked_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
  if (out == NULL) return 0;
  if (a > UINT64_MAX - b) return 0;
  *out = a + b;
  return 1;
}

static int checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
  if (out == NULL) return 0;
  if (a != 0u && b > UINT64_MAX / a) return 0;
  *out = a * b;
  return 1;
}

static size_t compute_total_size_bytes(const uint64_t raw_char_counts[256],
                                      const loxc_dict_t *dict,
                                      const uint8_t *accepted_mask,
                                      size_t accepted_count,
                                      uint8_t *out_bits_per_symbol,
                                      size_t *out_symbol_count) {
  uint64_t char_counts[256];
  memcpy(char_counts, raw_char_counts, sizeof(char_counts));

  uint64_t dict_occurrences = 0;
  size_t dict_data_bytes = 0;
  size_t dict_count = 0;
  if (dict != NULL && dict->entries != NULL && dict->count > 0) {
    for (size_t i = 0; i < dict->count; i++) {
      if (accepted_mask == NULL || accepted_mask[i] == 0) continue;
      const char *word = dict->entries[i].word;
      const size_t wlen = dict->entries[i].word_len;
      const uint64_t cnt = dict->entries[i].count;
      if (word == NULL || wlen == 0 || cnt == 0) continue;

      dict_occurrences += cnt;
      if (!checked_add_size(dict_data_bytes, wlen, &dict_data_bytes)) {
        return SIZE_MAX;
      }
      dict_count++;

      for (size_t j = 0; j < wlen; j++) {
        const uint8_t ch = (uint8_t)word[j];
        if (cnt <= char_counts[ch]) {
          char_counts[ch] -= cnt;
        } else {
          char_counts[ch] = 0;
        }
      }
    }
  }

  uint64_t byte_occurrences = 0;
  size_t byte_symbols = 0;
  for (int ch = 0; ch < 256; ch++) {
    if (char_counts[ch] > 0) {
      byte_symbols++;
      byte_occurrences += char_counts[ch];
    }
  }

  const size_t symbol_count = byte_symbols + accepted_count;
  uint64_t total_occ = 0u;
  const uint8_t bps = bits_needed_u32((uint32_t)symbol_count + 1u);
  uint64_t data_bits = 0u;
  size_t table_data_bytes = 0;
  size_t symbol_bytes = 0;
  size_t offset_bytes = 0;
  size_t total_bytes = 0;

  if (!checked_add_u64(byte_occurrences, dict_occurrences, &total_occ) ||
      !checked_mul_u64(total_occ, (uint64_t)bps, &data_bits)) {
    return SIZE_MAX;
  }

  if (!checked_mul_size(symbol_count, 5u, &symbol_bytes) ||
      !checked_mul_size(dict_count + 1u, 4u, &offset_bytes) ||
      !checked_add_size(1024u, symbol_bytes, &table_data_bytes) ||
      !checked_add_size(table_data_bytes, offset_bytes, &table_data_bytes) ||
      !checked_add_size(table_data_bytes, 4u, &table_data_bytes) ||
      !checked_add_size(table_data_bytes, dict_data_bytes, &table_data_bytes) ||
      !checked_add_size(LOXC_TAB_HEADER_SIZE, table_data_bytes, &total_bytes) ||
      !checked_add_size(total_bytes, LOXC_TAB_TRAILER_SIZE, &total_bytes) ||
      !checked_add_size(total_bytes, LOXC_HEADER_SIZE_V2, &total_bytes) ||
      !checked_add_size(total_bytes, (size_t)((data_bits + 7u) / 8u),
                        &total_bytes)) {
    return SIZE_MAX;
  }

  if (out_bits_per_symbol) *out_bits_per_symbol = bps;
  if (out_symbol_count) *out_symbol_count = symbol_count;
  return total_bytes;
}

typedef struct {
  const uint8_t *bytes;
  size_t len;
  uint32_t symbol_id;
} dict_emit_t;

static int cmp_dict_emit(const void *a, const void *b) {
  const dict_emit_t *da = (const dict_emit_t *)a;
  const dict_emit_t *db = (const dict_emit_t *)b;
  if (da->len > db->len) return -1;
  if (da->len < db->len) return 1;
  return cmp_symbol_bytes(da->bytes, da->len, db->bytes, db->len);
}

static int cmp_symbol_bytes(const uint8_t *a, size_t a_len,
                            const uint8_t *b, size_t b_len) {
  const size_t n = (a_len < b_len) ? a_len : b_len;
  if (n > 0u) {
    const int rc = memcmp(a, b, n);
    if (rc != 0) return rc;
  }
  if (a_len < b_len) return -1;
  if (a_len > b_len) return 1;
  return 0;
}

static void write_u16_le_buf(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_u32_le_buf(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int write_all(FILE *f, const void *buf, size_t len) {
  if (len == 0) return 0;
  return fwrite(buf, 1, len, f) == len ? 0 : 1;
}

static uint32_t compute_table_fingerprint(const uint32_t *byte_to_symbol,
                                          const symbol_rec_t *symbols,
                                          size_t symbol_count,
                                          const uint32_t *symbol_to_dict_index,
                                          size_t dict_count,
                                          const uint8_t *dict_data,
                                          const uint32_t *dict_offsets,
                                          uint8_t strategy_id,
                                          uint8_t base_size,
                                          uint8_t bits_per_level,
                                          uint16_t level_count) {
  const uint32_t dict_data_size = dict_offsets[dict_count];
  const size_t data_size_sz =
      1024u + (symbol_count * 5u) + ((dict_count + 1u) * 4u) + 4u +
      (size_t)dict_data_size;
  const size_t meta_size = 1u + 1u + 1u + 2u + 4u + 4u + data_size_sz;
  uint8_t *buf = (uint8_t *)malloc(meta_size);
  size_t pos = 0u;
  uint32_t fingerprint = 0u;

  if (buf == NULL) return 0u;
  buf[pos++] = strategy_id;
  buf[pos++] = base_size;
  buf[pos++] = bits_per_level;
  write_u16_le_buf(buf + pos, level_count);
  pos += 2u;
  write_u32_le_buf(buf + pos, (uint32_t)symbol_count);
  pos += 4u;
  write_u32_le_buf(buf + pos, (uint32_t)dict_count);
  pos += 4u;

  for (int i = 0; i < 256; i++) {
    write_u32_le_buf(buf + pos, byte_to_symbol[i]);
    pos += 4u;
  }
  for (size_t i = 0; i < symbol_count; i++) {
    buf[pos++] = symbols[i].is_dict ? 1u : 0u;
    write_u32_le_buf(buf + pos,
                     symbols[i].is_dict ? symbol_to_dict_index[symbols[i].symbol_id]
                                        : (uint32_t)symbols[i].char_val);
    pos += 4u;
  }
  for (size_t i = 0; i < dict_count + 1u; i++) {
    write_u32_le_buf(buf + pos, dict_offsets[i]);
    pos += 4u;
  }
  write_u32_le_buf(buf + pos, dict_data_size);
  pos += 4u;
  if (dict_data_size > 0u) {
    memcpy(buf + pos, dict_data, dict_data_size);
    pos += dict_data_size;
  }

  fingerprint = loxc_crc32(buf, pos);
  free(buf);
  return fingerprint;
}

static int write_loxctab_file(
    const char *path,
    const char *module_name,
    uint8_t module_id,
    uint8_t module_version,
    const symbol_rec_t *symbols, size_t symbol_count,
    const uint32_t *byte_to_symbol,
    const uint32_t *dict_symbol_ids, size_t dict_count,
    const uint8_t *dict_data, const uint32_t *dict_offsets,
    uint32_t table_fingerprint,
    uint8_t strategy_id,
    uint8_t base_size,
    uint8_t bits_per_level,
    uint16_t level_count) {
  if (path == NULL || symbols == NULL || byte_to_symbol == NULL ||
      dict_symbol_ids == NULL || dict_offsets == NULL || module_name == NULL) {
    return 1;
  }
  if (strlen(module_name) > LOXC_TAB_MAX_NAME_LEN) return 1;

  const uint32_t dict_data_size = dict_offsets[dict_count];
  if (dict_data_size > 0 && dict_data == NULL) return 1;
  if (symbol_count > UINT32_MAX || dict_count > UINT32_MAX) return 1;
  if (symbol_count > (SIZE_MAX - 1024u) / 5u) return 1;
  if (dict_count > (SIZE_MAX / 4u) - 1u) return 1;

  const size_t data_size_sz =
      1024u + (symbol_count * 5u) + ((dict_count + 1u) * 4u) + 4u +
      (size_t)dict_data_size;
  if (data_size_sz > UINT32_MAX) return 1;
  const uint32_t data_size = (uint32_t)data_size_sz;

  uint8_t header[LOXC_TAB_HEADER_SIZE];
  memset(header, 0, sizeof(header));
  memcpy(header, LOXC_TAB_MAGIC, 4);
  header[4] = (uint8_t)LOXC_TAB_VERSION;
  header[5] = module_version;
  header[6] = module_id;
  header[7] = strategy_id;
  header[8] = base_size;
  header[9] = bits_per_level;
  write_u16_le_buf(header + 10, level_count);
  write_u32_le_buf(header + 12, (uint32_t)symbol_count);
  write_u32_le_buf(header + 16, (uint32_t)dict_count);
  write_u32_le_buf(header + 20, data_size);
  write_u32_le_buf(header + 24, table_fingerprint);
  header[28] = (uint8_t)strlen(module_name);
  memcpy(header + 29, module_name, strlen(module_name));

  uint8_t *data = (uint8_t *)malloc(data_size_sz);
  if (data == NULL) {
    fprintf(stderr, "Error: malloc loxctab data\n");
    return 1;
  }

  size_t pos = 0;
  for (int i = 0; i < 256; i++) {
    write_u32_le_buf(data + pos, byte_to_symbol[i]);
    pos += 4u;
  }

  for (size_t i = 0; i < symbol_count; i++) {
    data[pos++] = symbols[i].is_dict ? 1u : 0u;
    if (!symbols[i].is_dict) {
      write_u32_le_buf(data + pos, (uint32_t)symbols[i].char_val);
    } else {
      uint32_t dict_idx = 0xFFFFFFFFu;
      for (uint32_t d = 0; d < (uint32_t)dict_count; d++) {
        if (dict_symbol_ids[d] == symbols[i].symbol_id) {
          dict_idx = d;
          break;
        }
      }
      if (dict_idx == 0xFFFFFFFFu) {
        fprintf(stderr, "Error: dict symbol id %u has no dict index\n",
                (unsigned)symbols[i].symbol_id);
        free(data);
        return 1;
      }
      write_u32_le_buf(data + pos, dict_idx);
    }
    pos += 4u;
  }

  for (size_t i = 0; i < dict_count + 1u; i++) {
    write_u32_le_buf(data + pos, dict_offsets[i]);
    pos += 4u;
  }

  write_u32_le_buf(data + pos, dict_data_size);
  pos += 4u;
  if (dict_data_size > 0) {
    memcpy(data + pos, dict_data, dict_data_size);
    pos += dict_data_size;
  }

  if (pos != data_size_sz) {
    fprintf(stderr, "Error: internal loxctab size mismatch\n");
    free(data);
    return 1;
  }

  uint8_t *crc_input = (uint8_t *)malloc(LOXC_TAB_HEADER_SIZE + data_size_sz);
  if (crc_input == NULL) {
    fprintf(stderr, "Error: malloc loxctab crc input\n");
    free(data);
    return 1;
  }
  memcpy(crc_input, header, LOXC_TAB_HEADER_SIZE);
  memcpy(crc_input + LOXC_TAB_HEADER_SIZE, data, data_size_sz);
  const uint32_t crc = loxc_crc32(crc_input, LOXC_TAB_HEADER_SIZE + data_size_sz);
  free(crc_input);

  uint8_t trailer[LOXC_TAB_TRAILER_SIZE];
  write_u32_le_buf(trailer, crc);

  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    fprintf(stderr, "Error: cannot create %s: %s\n", path, strerror(errno));
    free(data);
    return 1;
  }

  int wr = 0;
  wr |= write_all(f, header, sizeof(header));
  wr |= write_all(f, data, data_size_sz);
  wr |= write_all(f, trailer, sizeof(trailer));
  if (fclose(f) != 0) wr = 1;
  free(data);
  if (wr != 0) {
    fprintf(stderr, "Error: failed writing %s\n", path);
    return 1;
  }

  printf("Generated: %s\n", path);
  printf("  Header: %u bytes\n", (unsigned)LOXC_TAB_HEADER_SIZE);
  printf("  Data: %u bytes\n", (unsigned)data_size);
  printf("  Trailer (CRC): %u bytes\n", (unsigned)LOXC_TAB_TRAILER_SIZE);
  printf("  Module ID: %u\n", (unsigned)module_id);
  printf("  Fingerprint: 0x%08X\n", (unsigned)table_fingerprint);
  return 0;
}

static int write_loxctab_from_emit(const char *output,
                                   const char *module_name,
                                   uint8_t module_id,
                                   uint8_t module_version,
                                   const symbol_rec_t *symbols,
                                   size_t symbol_count,
                                   const uint32_t byte_to_symbol[256],
                                   const dict_emit_t *dict_emit,
                                   size_t dict_count,
                                   const uint32_t *symbol_to_dict_index,
                                    uint8_t strategy_id,
                                    uint8_t base_size,
                                    uint8_t bits_per_level,
                                   uint16_t level_count,
                                   uint32_t *out_table_fingerprint) {
  char tab_path[512];
  snprintf(tab_path, sizeof(tab_path), "%s.loxctab", output);

  uint32_t *dict_offsets =
      (uint32_t *)malloc((dict_count + 1u) * sizeof(uint32_t));
  uint32_t *dict_symbol_ids =
      (uint32_t *)malloc((dict_count > 0 ? dict_count : 1u) * sizeof(uint32_t));
  if (dict_offsets == NULL || dict_symbol_ids == NULL) {
    fprintf(stderr, "Error: malloc loxctab dict metadata\n");
    free(dict_offsets);
    free(dict_symbol_ids);
    return 1;
  }

  size_t dict_data_size_sz = 0;
  for (size_t i = 0; i < dict_count; i++) {
    if (dict_emit[i].len > UINT32_MAX - dict_data_size_sz) {
      fprintf(stderr, "Error: loxctab dict data too large\n");
      free(dict_offsets);
      free(dict_symbol_ids);
      return 1;
    }
    dict_offsets[i] = (uint32_t)dict_data_size_sz;
    dict_symbol_ids[i] = dict_emit[i].symbol_id;
    dict_data_size_sz += dict_emit[i].len;
  }
  dict_offsets[dict_count] = (uint32_t)dict_data_size_sz;

  uint8_t *dict_data = NULL;
  if (dict_data_size_sz > 0) {
    dict_data = (uint8_t *)malloc(dict_data_size_sz);
    if (dict_data == NULL) {
      fprintf(stderr, "Error: malloc loxctab dict data\n");
      free(dict_offsets);
      free(dict_symbol_ids);
      return 1;
    }
  }

  size_t pos = 0;
  uint32_t table_fingerprint = 0u;
  for (size_t i = 0; i < dict_count; i++) {
    if (dict_emit[i].len > 0) {
      memcpy(dict_data + pos, dict_emit[i].bytes, dict_emit[i].len);
      pos += dict_emit[i].len;
    }
  }

  if (symbol_to_dict_index == NULL) {
    free(dict_data);
    free(dict_offsets);
    free(dict_symbol_ids);
    return 1;
  }
  table_fingerprint =
      compute_table_fingerprint(byte_to_symbol, symbols, symbol_count,
                                symbol_to_dict_index, dict_count, dict_data,
                                dict_offsets, strategy_id, base_size,
                                bits_per_level, level_count);

  printf("\n=== KROK 6: Loxctab File Generation ===\n");
  int rc = write_loxctab_file(tab_path, module_name, module_id, module_version,
                              symbols, symbol_count, byte_to_symbol,
                              dict_symbol_ids, dict_count, dict_data,
                              dict_offsets, table_fingerprint, strategy_id, base_size,
                              bits_per_level, level_count);
  free(dict_data);
  free(dict_offsets);
  free(dict_symbol_ids);
  if (rc == 0 && out_table_fingerprint != NULL) {
    *out_table_fingerprint = table_fingerprint;
  }
  return rc;
}

static int generate_c_file_flat(const char *input, size_t data_len,
                                const char *module_name,
                                const char *name_upper,
                                const char *generated_at,
                                uint8_t module_id,
                                uint32_t table_fingerprint,
                                const symbol_rec_t *symbols,
                                size_t symbol_count,
                                const dict_emit_t *dict_emit,
                                size_t dict_count,
                                const uint32_t byte_to_symbol[256],
                                const uint32_t *symbol_to_dict_index,
                                const char *func_prefix);

static int generate_c_file_hier(const char *input, size_t data_len,
                                const char *module_name,
                                const char *name_upper,
                                const char *generated_at,
                                uint8_t module_id,
                                uint32_t table_fingerprint,
                                loxc_strategy_t strategy,
                                const symbol_rec_t *symbols,
                                size_t symbol_count,
                                const dict_emit_t *dict_emit,
                                size_t dict_count,
                                const uint32_t byte_to_symbol[256],
                                const uint32_t *symbol_to_dict_index,
                                const char *func_prefix,
                                const loxc_hier_t *hier);

static int parse_args(int argc, char *argv[], const char *inputs[],
                      size_t *input_count,
                      const char **output, const char **module_name,
                      uint8_t *module_id) {
  *input_count = 0;
  *output = NULL;
  *module_name = NULL;
  *module_id = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      if (*input_count >= LOXC_TRAIN_MAX_INPUTS) {
        fprintf(stderr, "Error: too many --input files (max %u)\n",
                (unsigned)LOXC_TRAIN_MAX_INPUTS);
        return 1;
      }
      inputs[*input_count] = argv[++i];
      (*input_count)++;
    } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
      *output = argv[++i];
    } else if (strcmp(argv[i], "--module-name") == 0 && i + 1 < argc) {
      *module_name = argv[++i];
    } else if (strcmp(argv[i], "--module-id") == 0 && i + 1 < argc) {
      char *endptr;
      long id = strtol(argv[++i], &endptr, 10);
      if (id < 0 || id > 255 || *endptr != '\0') {
        fprintf(stderr, "Error: --module-id must be 0-255\n");
        return 1;
      }
      *module_id = (uint8_t)id;
    }
  }

  if (*input_count == 0 || *output == NULL || *module_name == NULL ||
      *module_id == 0) {
    fprintf(stderr,
            "Usage: loxc_train --input <file> [--input <file> ...] --output <dir> --module-name "
            "<name> --module-id <0-255>\n");
    return 1;
  }

  if (!validate_module_name(*module_name)) {
    fprintf(stderr,
            "Error: --module-name must be lowercase [a-z0-9_]+ (no hyphens or special chars)\n");
    return 1;
  }

  return 0;
}

static int strategy_eval_better(const strategy_eval_t *cand,
                                const strategy_eval_t *best) {
  if (cand->total_bits != best->total_bits) {
    return cand->total_bits < best->total_bits;
  }
  if (cand->payload_bits != best->payload_bits) {
    return cand->payload_bits < best->payload_bits;
  }
  if (cand->level_count != best->level_count) {
    return cand->level_count < best->level_count;
  }
  return cand->strategy < best->strategy;
}

static int evaluate_strategy(const loxc_freq_entry_t *freqs,
                             size_t symbol_count,
                             size_t dict_count,
                             size_t dict_data_bytes,
                             loxc_strategy_t strategy,
                             strategy_eval_t *out_eval) {
  strategy_eval_t eval;
  size_t symbol_meta_bytes = 0u;
  size_t offset_bytes = 0u;
  size_t table_data_bytes = 0u;
  size_t table_blob_bytes = 0u;
  size_t total_bytes = 0u;
  uint64_t total_syms = 0u;

  if (out_eval == NULL) return 1;
  memset(&eval, 0, sizeof(eval));
  eval.strategy = strategy;
  if (loxc_strategy_describe(strategy, &eval.desc) != LOXC_OK) return 1;

  if (strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
    eval.payload_bits = loxc_strategy_cost_flat_with_raw(freqs, symbol_count);
    eval.level_count = 0u;
  } else {
    eval.payload_bits =
        loxc_strategy_cost_hierarchical(freqs, symbol_count, strategy,
                                        &eval.level_count);
  }
  if (eval.payload_bits == UINT64_MAX) return 1;

  for (size_t i = 0; i < symbol_count; i++) {
    if (!checked_add_u64(total_syms, freqs[i].count, &total_syms)) return 1;
  }
  eval.total_symbols = total_syms;

  if (!checked_mul_size(symbol_count, 5u, &symbol_meta_bytes) ||
      !checked_mul_size(dict_count + 1u, 4u, &offset_bytes) ||
      !checked_add_size(1024u, symbol_meta_bytes, &table_data_bytes) ||
      !checked_add_size(table_data_bytes, offset_bytes, &table_data_bytes) ||
      !checked_add_size(table_data_bytes, 4u, &table_data_bytes) ||
      !checked_add_size(table_data_bytes, dict_data_bytes, &table_data_bytes) ||
      !checked_add_size(LOXC_TAB_HEADER_SIZE, table_data_bytes, &table_blob_bytes) ||
      !checked_add_size(table_blob_bytes, LOXC_TAB_TRAILER_SIZE, &table_blob_bytes) ||
      !checked_add_size(LOXC_HEADER_SIZE_V2,
                        (size_t)((eval.payload_bits + 7u) / 8u), &total_bytes) ||
      !checked_add_size(total_bytes, table_blob_bytes, &total_bytes)) {
    return 1;
  }

  eval.total_bytes = total_bytes;
  eval.total_bits = (uint64_t)total_bytes * 8u;
  *out_eval = eval;
  return 0;
}

static int generate_c_file_hier(const char *input, size_t data_len,
                                const char *module_name,
                                const char *name_upper,
                                const char *generated_at,
                                uint8_t module_id,
                                uint32_t table_fingerprint,
                                loxc_strategy_t strategy,
                                const symbol_rec_t *symbols,
                                size_t symbol_count,
                                const dict_emit_t *dict_emit,
                                size_t dict_count,
                                const uint32_t byte_to_symbol[256],
                                const uint32_t *symbol_to_dict_index,
                                const char *func_prefix,
                                const loxc_hier_t *hier) {
  loxc_strategy_desc_t desc;
  (void)module_id;
  if (loxc_strategy_describe(strategy, &desc) != LOXC_OK) return 1;
  if (hier == NULL) return 1;

  char source_path[512];
  snprintf(source_path, sizeof(source_path), "modules/loxc_%s.c", module_name);

  FILE *cfile = fopen(source_path, "w");
  if (cfile == NULL) {
    fprintf(stderr, "Error: cannot create %s: %s\n", source_path,
            strerror(errno));
    return 1;
  }

  fprintf(cfile,
          "/* AUTO-GENERATED by loxc_train. DO NOT EDIT.\n"
          " * Source: %s (%zu bytes)\n"
          " * Generated: %s\n"
          " */\n\n",
          input, data_len, generated_at);
  fprintf(cfile, "#include \"loxc_%s.h\"\n", module_name);
  fprintf(cfile, "#include \"loxc.h\"\n");
  fprintf(cfile, "#include \"loxc_stream.h\"\n");
  fprintf(cfile, "#include \"loxc_strategy.h\"\n");
  fprintf(cfile, "#include \"loxc_hier.h\"\n");
  fprintf(cfile, "#include \"loxc_base.h\"\n");
  fprintf(cfile, "\n");

  fprintf(cfile, "#include <stddef.h>\n");
  fprintf(cfile, "#include <stdint.h>\n");
  fprintf(cfile, "#include <string.h>\n");
  fprintf(cfile, "\n");

  const uint32_t base_size = desc.base_size;
  const uint32_t direct_slots = desc.direct_slots;
  const uint32_t raw_pos = desc.raw_pos;
  const uint32_t continue_pos = desc.continue_pos;
  const uint32_t bits_per_level = desc.bits_per_level;
  const size_t dict_emit_count = (dict_count > 0) ? dict_count : 1;

  fprintf(cfile, "#define MOD_%s_BASE_SIZE      %uu\n", name_upper, (unsigned)base_size);
  fprintf(cfile, "#define MOD_%s_RAW_POS        %uu\n", name_upper, (unsigned)raw_pos);
  fprintf(cfile, "#define MOD_%s_CONTINUE_POS   %uu\n", name_upper, (unsigned)continue_pos);
  fprintf(cfile, "#define MOD_%s_DIRECT_SLOTS   %uu\n", name_upper, (unsigned)direct_slots);
  fprintf(cfile, "#define MOD_%s_BITS_PER_LEVEL %uu\n", name_upper, (unsigned)bits_per_level);
  fprintf(cfile, "#define MOD_%s_DICT_COUNT     %zuu\n", name_upper, dict_count);
  fprintf(cfile, "#define MOD_%s_LEVELS         %uu\n", name_upper, (unsigned)hier->level_count);
  fprintf(cfile, "#define MOD_%s_FINGERPRINT    0x%08Xu\n", name_upper,
          (unsigned)table_fingerprint);
  fprintf(cfile, "\n");

  fprintf(cfile,
          "typedef struct {\n"
          "  uint8_t type;\n"
          "  uint32_t byte_or_idx;\n"
          "} mod_%s_symbol_t;\n\n",
          module_name);

  fprintf(cfile, "static const mod_%s_symbol_t mod_%s_symbols[%zu] = {\n",
          module_name, module_name, symbol_count);
  for (size_t i = 0; i < symbol_count; i++) {
    if (!symbols[i].is_dict) {
      fprintf(cfile, "  { 0u, %uu },\n", (unsigned)symbols[i].char_val);
    } else {
      const uint32_t di = symbol_to_dict_index[symbols[i].symbol_id];
      fprintf(cfile, "  { 1u, %uu },\n", (unsigned)di);
    }
  }
  fprintf(cfile, "};\n\n");

  /* Dict data blob + offsets (no C string escaping; supports embedded NULs) */
  fprintf(cfile, "static const uint8_t mod_%s_dict_data[] = {", module_name);
  if (dict_count == 0) {
    fprintf(cfile, " 0x00 ");
  } else {
    size_t byte_pos = 0;
    for (size_t i = 0; i < dict_count; i++) {
      for (size_t j = 0; j < dict_emit[i].len; j++) {
        if ((byte_pos % 12) == 0) fprintf(cfile, "\n  ");
        fprintf(cfile, "0x%02X,", (unsigned)dict_emit[i].bytes[j]);
        byte_pos++;
      }
    }
  }
  fprintf(cfile, "\n};\n\n");

  fprintf(cfile, "static const uint32_t mod_%s_dict_offsets[%zu] = {\n",
          module_name, dict_emit_count + 1);
  size_t off = 0;
  for (size_t i = 0; i < dict_emit_count; i++) {
    fprintf(cfile, "  %zuu,\n", off);
    if (i < dict_count) off += dict_emit[i].len;
  }
  fprintf(cfile, "  %zuu /* sentinel */\n", off);
  fprintf(cfile, "};\n\n");

  fprintf(cfile,
          "static const uint8_t *mod_%s_dict_entry(uint32_t idx, size_t *out_len) {\n"
          "  const uint32_t start = mod_%s_dict_offsets[idx];\n"
          "  const uint32_t end = mod_%s_dict_offsets[idx + 1];\n"
          "  *out_len = (size_t)(end - start);\n"
          "  return &mod_%s_dict_data[start];\n"
          "}\n\n",
          module_name, module_name, module_name, module_name);

  fprintf(cfile, "static const uint32_t mod_%s_byte_to_symbol[256] = {\n", module_name);
  for (int i = 0; i < 256; i++) {
    fprintf(cfile, "  0x%08Xu%s", (unsigned)byte_to_symbol[i],
            (i == 255) ? "\n" : ",\n");
  }
  fprintf(cfile, "};\n\n");

  fprintf(cfile, "static const uint32_t mod_%s_dict_symbol_ids[%zu] = {\n",
          module_name, dict_emit_count);
  if (dict_count == 0) {
    fprintf(cfile, "  0u,\n");
  } else {
    for (size_t i = 0; i < dict_count; i++) {
      fprintf(cfile, "  %uu,\n", (unsigned)dict_emit[i].symbol_id);
    }
  }
  fprintf(cfile, "};\n\n");

  /* 5C: Encode (HIER) */
  fprintf(cfile,
          "int %s_encode(const char *text, size_t text_len, loxc_writer_t *w) {\n"
          "  if (text == NULL || w == NULL) return LOXC_ERR_NULL;\n"
          "\n"
          "  size_t i = 0;\n"
          "  while (i < text_len) {\n"
          "    uint32_t sym_id = 0xFFFFFFFFu;\n"
          "    size_t consumed = 1;\n"
          "\n"
          "    for (uint32_t d = 0; d < (uint32_t)MOD_%s_DICT_COUNT; d++) {\n"
          "      size_t dlen = 0;\n"
          "      const uint8_t *dbytes = mod_%s_dict_entry(d, &dlen);\n"
          "      if (dlen == 0) continue;\n"
          "      if (i + dlen <= text_len &&\n"
          "          memcmp((const uint8_t *)text + i, dbytes, dlen) == 0) {\n"
          "        sym_id = mod_%s_dict_symbol_ids[d];\n"
          "        consumed = dlen;\n"
          "        break;\n"
          "      }\n"
          "    }\n"
          "\n"
          "    if (sym_id == 0xFFFFFFFFu) {\n"
          "      sym_id = mod_%s_byte_to_symbol[(uint8_t)text[i]];\n"
          "      if (sym_id == 0xFFFFFFFFu) {\n"
          "        int rc = loxc_write_bits(w, (uint32_t)MOD_%s_RAW_POS,\n"
          "                                (uint8_t)MOD_%s_BITS_PER_LEVEL);\n"
          "        if (rc != LOXC_OK) return rc;\n"
          "        rc = loxc_write_bits(w, (uint8_t)text[i], 8u);\n"
          "        if (rc != LOXC_OK) return rc;\n"
          "        i += 1u;\n"
          "        continue;\n"
          "      }\n"
          "      consumed = 1;\n"
          "    }\n"
          "\n"
          "    const uint32_t level = sym_id / (uint32_t)MOD_%s_DIRECT_SLOTS;\n"
          "    const uint32_t pos_in_level = sym_id %% (uint32_t)MOD_%s_DIRECT_SLOTS;\n"
          "\n"
          "    for (uint32_t l = 0; l < level; l++) {\n"
          "      int rc = loxc_write_bits(w, (uint32_t)MOD_%s_CONTINUE_POS,\n"
          "                              (uint8_t)MOD_%s_BITS_PER_LEVEL);\n"
          "      if (rc != LOXC_OK) return rc;\n"
          "    }\n"
          "    {\n"
          "      int rc = loxc_write_bits(w, pos_in_level,\n"
          "                              (uint8_t)MOD_%s_BITS_PER_LEVEL);\n"
          "      if (rc != LOXC_OK) return rc;\n"
          "    }\n"
          "\n"
          "    i += consumed;\n"
          "  }\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          func_prefix,
          name_upper, module_name, module_name, module_name,
          name_upper, name_upper, name_upper, name_upper,
          name_upper, name_upper, name_upper);

  fprintf(cfile,
          "int %s_decode(loxc_reader_t *r, char *out, size_t *inout_len) {\n"
          "  if (r == NULL || out == NULL || inout_len == NULL) return LOXC_ERR_NULL;\n"
          "\n"
          "  size_t cap = *inout_len;\n"
          "  size_t written = 0;\n"
          "\n"
          "  for (;;) {\n"
          "    if (written == cap) goto done;\n"
          "    uint32_t level = 0;\n"
          "    uint32_t pos = 0;\n"
          "    int found = 0;\n"
          "\n"
          "    for (level = 0; level < (uint32_t)MOD_%s_LEVELS; level++) {\n"
          "      uint32_t val = 0;\n"
          "      int rc = loxc_read_bits(r, (uint8_t)MOD_%s_BITS_PER_LEVEL, &val);\n"
          "      if (rc != LOXC_OK) {\n"
          "        if (level == 0) goto done;\n"
          "        return rc;\n"
          "      }\n"
          "      if (val < (uint32_t)MOD_%s_DIRECT_SLOTS) {\n"
          "        pos = val;\n"
          "        found = 1;\n"
          "        break;\n"
          "      }\n"
          "      if (val == (uint32_t)MOD_%s_RAW_POS) {\n"
          "        uint32_t raw_byte = 0;\n"
          "        rc = loxc_read_bits(r, 8u, &raw_byte);\n"
          "        if (rc != LOXC_OK) return rc;\n"
          "        if (written >= cap) { *inout_len = written; return LOXC_ERR_OVERFLOW; }\n"
          "        out[written++] = (char)(uint8_t)raw_byte;\n"
          "        found = 2;\n"
          "        break;\n"
          "      }\n"
          "      if (val != (uint32_t)MOD_%s_CONTINUE_POS) return LOXC_ERR_INVALID_FORMAT;\n"
          "    }\n"
          "\n"
          "    if (found == 2) continue;\n"
          "    if (!found) return LOXC_ERR_INVALID_FORMAT;\n"
          "\n"
          "    uint32_t sym_id = level * (uint32_t)MOD_%s_DIRECT_SLOTS + pos;\n"
          "    if (sym_id >= (uint32_t)LOXC_MOD_%s_SYMBOLS) return LOXC_ERR_INVALID_FORMAT;\n"
          "\n"
          "    const mod_%s_symbol_t sym = mod_%s_symbols[sym_id];\n"
          "    if (sym.type == 0u) {\n"
          "      if (written >= cap) { *inout_len = written; return LOXC_ERR_OVERFLOW; }\n"
          "      out[written++] = (char)(uint8_t)(sym.byte_or_idx & 0xFFu);\n"
          "    } else {\n"
          "      size_t dlen = 0;\n"
          "      const uint8_t *dbytes = mod_%s_dict_entry(sym.byte_or_idx, &dlen);\n"
          "      if (written + dlen > cap) { *inout_len = written; return LOXC_ERR_OVERFLOW; }\n"
          "      memcpy(out + written, dbytes, dlen);\n"
          "      written += dlen;\n"
          "    }\n"
          "  }\n"
          "\n"
          "done:\n"
          "  *inout_len = written;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          func_prefix,
          name_upper,
          name_upper, name_upper, name_upper, name_upper, name_upper,
          name_upper,
          module_name, module_name,
          module_name);

  fprintf(cfile,
          "static int mod_%s_encode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,\n"
          "                                size_t out_cap, size_t *out_len) {\n"
          "  if (out_len == NULL) return LOXC_ERR_NULL;\n"
          "  *out_len = 0;\n"
          "  if (in == NULL || out == NULL) return LOXC_ERR_NULL;\n"
          "  if (in_len > 0xFFFFFFFFu) return LOXC_ERR_OVERFLOW;\n"
          "\n"
          "  loxc_header_t h;\n"
          "  h.module_id = LOXC_MOD_%s_ID;\n"
          "  h.version = 2;\n"
          "  h.flags = 0;\n"
          "  h.strategy_id = (uint8_t)LOXC_MOD_%s_STRATEGY;\n"
          "  h.payload_len = 0;\n"
          "  h.level_count = (uint16_t)MOD_%s_LEVELS;\n"
          "  h.uncompressed_len = (uint32_t)in_len;\n"
          "  h.table_fingerprint = MOD_%s_FINGERPRINT;\n"
          "  h.crc32 = 0;\n"
          "\n"
          "  const size_t header_bytes = loxc_header_size(&h);\n"
          "  if (out_cap < header_bytes) return LOXC_ERR_OVERFLOW;\n"
          "\n"
          "  loxc_writer_t w;\n"
          "  int rc = loxc_writer_init(&w, out + header_bytes, out_cap - header_bytes);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  rc = %s_encode((const char *)in, in_len, &w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  rc = loxc_writer_flush(&w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  const size_t data_bytes = loxc_writer_size(&w);\n"
          "  if (data_bytes > LOXC_HEADER_MAX_EXACT_PAYLOAD_LEN) return LOXC_ERR_OVERFLOW;\n"
          "  h.payload_len = (uint16_t)data_bytes;\n"
          "  const size_t total = header_bytes + data_bytes;\n"
          "  loxc_writer_t hw;\n"
          "  rc = loxc_writer_init(&hw, out, header_bytes);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  rc = loxc_header_write(&hw, &h);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  if (loxc_writer_size(&hw) != header_bytes) return LOXC_ERR_INVALID_FORMAT;\n"
          "\n"
          "  *out_len = total;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          module_name, name_upper, name_upper, name_upper, name_upper,
          func_prefix);

  fprintf(cfile,
          "static int mod_%s_decode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,\n"
          "                                size_t out_cap, size_t *out_len) {\n"
          "  if (out_len == NULL) return LOXC_ERR_NULL;\n"
          "  *out_len = 0;\n"
          "  if (in == NULL || out == NULL) return LOXC_ERR_NULL;\n"
          "\n"
          "  loxc_reader_t hr;\n"
          "  int rc = loxc_reader_init(&hr, in, in_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  loxc_header_t h;\n"
          "  rc = loxc_header_read(&hr, &h);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  if (h.module_id != LOXC_MOD_%s_ID) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.version != LOXC_HEADER_VERSION_V2) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.flags != 0) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.strategy_id != (uint8_t)LOXC_MOD_%s_STRATEGY) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.level_count != (uint16_t)MOD_%s_LEVELS) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.table_fingerprint != MOD_%s_FINGERPRINT) return LOXC_ERR_INVALID_MAGIC;\n"
          "\n"
          "  const size_t header_bytes = loxc_header_size(&h);\n"
          "  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;\n"
          "  size_t data_len = 0;\n"
          "  rc = loxc_header_resolve_payload_len(&h, in_len - header_bytes, &data_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  loxc_reader_t r;\n"
          "  rc = loxc_reader_init(&r, in + header_bytes, data_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  const size_t expected_len = (size_t)h.uncompressed_len;\n"
          "  if (expected_len > out_cap) return LOXC_ERR_OVERFLOW;\n"
          "  size_t cap = expected_len;\n"
          "  rc = %s_decode(&r, (char *)out, &cap);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  if (cap != expected_len) return LOXC_ERR_TRUNCATED;\n"
          "  rc = loxc_reader_finish_zero_padding(&r);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  *out_len = cap;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          module_name, name_upper, name_upper, name_upper, name_upper,
          func_prefix);

  fprintf(cfile,
          "static const loxc_module_t mod_%s_module = {\n"
          "  .name = \"%s\",\n"
          "  .table_name = \"%s\",\n"
          "  .module_id = LOXC_MOD_%s_ID,\n"
          "  .version = LOXC_MOD_%s_VERSION,\n"
          "  .strategy_id = LOXC_MOD_%s_STRATEGY,\n"
          "  .level_count = MOD_%s_LEVELS,\n"
          "  .table_fingerprint = MOD_%s_FINGERPRINT,\n"
          "  .encode = mod_%s_encode_buffer,\n"
          "  .decode = mod_%s_decode_buffer,\n"
          "};\n\n",
          module_name, module_name, module_name, name_upper, name_upper,
          name_upper, name_upper, name_upper, module_name, module_name);

  fprintf(cfile,
          "int %s_register(void) {\n"
          "  return loxc_module_register(&mod_%s_module);\n"
          "}\n",
          func_prefix, module_name);

  fclose(cfile);

  printf("Generated: %s\n", source_path);
  printf("KROK 5: hierarchical stub emitted\n");
  return 0;
}

static int generate_c_file_flat(const char *input, size_t data_len,
                                const char *module_name,
                                const char *name_upper,
                                const char *generated_at,
                                uint8_t module_id,
                                uint32_t table_fingerprint,
                                const symbol_rec_t *symbols,
                                size_t symbol_count,
                                const dict_emit_t *dict_emit,
                                size_t dict_count,
                                const uint32_t byte_to_symbol[256],
                                const uint32_t *symbol_to_dict_index,
                                const char *func_prefix) {
  (void)module_id;
  (void)symbol_to_dict_index;

  const size_t dict_emit_count = (dict_count > 0) ? dict_count : 1;

  char source_path[512];
  snprintf(source_path, sizeof(source_path), "modules/loxc_%s.c", module_name);

  FILE *cfile = fopen(source_path, "w");
  if (cfile == NULL) {
    fprintf(stderr, "Error: cannot create %s: %s\n", source_path,
            strerror(errno));
    return 1;
  }

  /* Write .c */
  fprintf(cfile,
          "/* AUTO-GENERATED by loxc_train. DO NOT EDIT.\n"
          " * Source: %s (%zu bytes)\n"
          " * Generated: %s\n"
          " */\n\n",
          input, data_len, generated_at);

  fprintf(cfile, "#include \"loxc_%s.h\"\n", module_name);
  fprintf(cfile, "#include \"loxc.h\"\n");
  fprintf(cfile, "#include \"loxc_stream.h\"\n");
  fprintf(cfile, "#include \"loxc_strategy.h\"\n");
  fprintf(cfile, "#include \"loxc_base.h\"\n");
  fprintf(cfile, "\n");
  fprintf(cfile, "#include <stddef.h>\n");
  fprintf(cfile, "#include <stdint.h>\n");
  fprintf(cfile, "#include <string.h>\n");
  fprintf(cfile, "\n");

  fprintf(cfile,
          "static int mod_%s_encode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,\n"
          "                                size_t out_cap, size_t *out_len);\n"
          "static int mod_%s_decode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,\n"
          "                                size_t out_cap, size_t *out_len);\n\n",
          module_name, module_name);

  fprintf(cfile, "enum { MOD_%s_SYMBOL_BITS = %u };\n", name_upper,
          bits_needed_u32((uint32_t)symbol_count + 1u));
  fprintf(cfile, "#define MOD_%s_FINGERPRINT    0x%08Xu\n", name_upper,
          (unsigned)table_fingerprint);
  fprintf(cfile, "\n");

  fprintf(cfile,
          "typedef struct {\n"
          "  uint8_t type;       /* 0=byte, 1=dict */\n"
          "  uint32_t byte_or_idx; /* byte value or dict idx */\n"
          "} mod_%s_symbol_t;\n\n",
          module_name);

  fprintf(cfile, "static const mod_%s_symbol_t mod_%s_symbols[%zu] = {\n",
          module_name, module_name, symbol_count);
  for (size_t i = 0; i < symbol_count; i++) {
    if (!symbols[i].is_dict) {
      fprintf(cfile, "  { 0u, %uu },\n", (unsigned)symbols[i].char_val);
    } else {
      const uint32_t di = symbol_to_dict_index[symbols[i].symbol_id];
      fprintf(cfile, "  { 1u, %uu },\n", (unsigned)di);
    }
  }
  fprintf(cfile, "};\n\n");

  fprintf(cfile, "static const uint32_t mod_%s_byte_to_symbol[256] = {\n",
          module_name);
  for (int i = 0; i < 256; i++) {
    fprintf(cfile, "  0x%08X%s", (unsigned)byte_to_symbol[i],
            (i == 255) ? "\n" : ",\n");
  }
  fprintf(cfile, "};\n\n");

  /* Dict data blob + offsets (no C string escaping; supports embedded NULs) */
  fprintf(cfile, "static const uint8_t mod_%s_dict_data[] = {", module_name);
  if (dict_count == 0) {
    fprintf(cfile, " 0x00 ");
  } else {
    size_t byte_pos = 0;
    for (size_t i = 0; i < dict_count; i++) {
      for (size_t j = 0; j < dict_emit[i].len; j++) {
        if ((byte_pos % 12) == 0) fprintf(cfile, "\n  ");
        fprintf(cfile, "0x%02X,", (unsigned)dict_emit[i].bytes[j]);
        byte_pos++;
      }
    }
  }
  fprintf(cfile, "\n};\n\n");

  fprintf(cfile, "static const uint32_t mod_%s_dict_offsets[%zu] = {\n",
          module_name, dict_emit_count + 1);
  size_t off = 0;
  for (size_t i = 0; i < dict_emit_count; i++) {
    fprintf(cfile, "  %zuu,\n", off);
    if (i < dict_count) off += dict_emit[i].len;
  }
  fprintf(cfile, "  %zuu /* sentinel */\n", off);
  fprintf(cfile, "};\n\n");

  fprintf(cfile,
          "static const uint8_t *mod_%s_dict_entry(uint32_t idx, size_t *out_len) {\n"
          "  const uint32_t start = mod_%s_dict_offsets[idx];\n"
          "  const uint32_t end = mod_%s_dict_offsets[idx + 1];\n"
          "  *out_len = (size_t)(end - start);\n"
          "  return &mod_%s_dict_data[start];\n"
          "}\n\n",
          module_name, module_name, module_name, module_name);

  fprintf(cfile, "static const uint32_t mod_%s_dict_symbol_ids[%zu] = {\n",
          module_name, dict_emit_count);
  if (dict_count == 0) {
    fprintf(cfile, "  0u,\n");
  } else {
    for (size_t i = 0; i < dict_count; i++) {
      fprintf(cfile, "  %uu,\n", (unsigned)dict_emit[i].symbol_id);
    }
  }
  fprintf(cfile, "};\n\n");

  fprintf(cfile,
          "int %s_encode(const char *text, size_t text_len, loxc_writer_t *w) {\n"
          "  if (text == NULL || w == NULL) return LOXC_ERR_NULL;\n"
          "  size_t i = 0;\n"
          "  while (i < text_len) {\n"
          "    uint32_t sym_id = 0xFFFFFFFFu;\n"
          "    size_t consumed = 1;\n"
          "    for (uint32_t d = 0; d < (uint32_t)%zu; d++) {\n"
          "      size_t len = 0;\n"
          "      const uint8_t *s = mod_%s_dict_entry(d, &len);\n"
          "      if (len == 0) continue;\n"
          "      if (i + len <= text_len && memcmp((const uint8_t *)text + i, s, len) == 0) {\n"
          "        sym_id = mod_%s_dict_symbol_ids[d];\n"
          "        consumed = len;\n"
          "        break;\n"
          "      }\n"
          "    }\n"
          "    if (sym_id == 0xFFFFFFFFu) {\n"
          "      sym_id = mod_%s_byte_to_symbol[(uint8_t)text[i]];\n"
          "      if (sym_id == 0xFFFFFFFFu) {\n"
          "        int rc = loxc_write_bits(w, %zuu, MOD_%s_SYMBOL_BITS);\n"
          "        if (rc != LOXC_OK) return rc;\n"
          "        rc = loxc_write_bits(w, (uint8_t)text[i], 8u);\n"
          "        if (rc != LOXC_OK) return rc;\n"
          "        i += 1;\n"
          "        continue;\n"
          "      }\n"
          "      consumed = 1;\n"
          "    }\n"
          "    int rc = loxc_write_bits(w, sym_id, MOD_%s_SYMBOL_BITS);\n"
          "    if (rc != LOXC_OK) return rc;\n"
          "    i += consumed;\n"
          "  }\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          func_prefix, dict_count, module_name, module_name, module_name,
          symbol_count, name_upper, name_upper);

  fprintf(cfile,
          "int %s_decode(loxc_reader_t *r, char *out, size_t *inout_len) {\n"
          "  if (r == NULL || out == NULL || inout_len == NULL) return LOXC_ERR_NULL;\n"
          "  size_t cap = *inout_len;\n"
          "  size_t pos = 0;\n"
          "  while (!loxc_reader_eof(r)) {\n"
          "    uint32_t sym_id = 0;\n"
          "    int rc = loxc_read_bits(r, MOD_%s_SYMBOL_BITS, &sym_id);\n"
          "    if (rc == LOXC_ERR_TRUNCATED) break;\n"
          "    if (rc != LOXC_OK) return rc;\n"
          "    if (sym_id == %zuu) {\n"
          "      uint32_t raw_byte = 0;\n"
          "      rc = loxc_read_bits(r, 8u, &raw_byte);\n"
          "      if (rc != LOXC_OK) return rc;\n"
          "      if (pos + 1 > cap) return LOXC_ERR_OVERFLOW;\n"
          "      out[pos++] = (char)(uint8_t)raw_byte;\n"
          "      continue;\n"
          "    }\n"
          "    if (sym_id >= %zuu) return LOXC_ERR_INVALID_FORMAT;\n"
          "    const mod_%s_symbol_t s = mod_%s_symbols[sym_id];\n"
          "    if (s.type == 0u) {\n"
          "      if (pos + 1 > cap) return LOXC_ERR_OVERFLOW;\n"
          "      out[pos++] = (char)(uint8_t)(s.byte_or_idx & 0xFFu);\n"
          "    } else if (s.type == 1u) {\n"
          "      const uint32_t di = s.byte_or_idx;\n"
          "      if (di >= (uint32_t)%zuu) return LOXC_ERR_INVALID_FORMAT;\n"
          "      size_t len = 0;\n"
          "      const uint8_t *ds = mod_%s_dict_entry(di, &len);\n"
          "      if (pos + len > cap) return LOXC_ERR_OVERFLOW;\n"
          "      memcpy(out + pos, ds, len);\n"
          "      pos += len;\n"
          "    } else {\n"
          "      return LOXC_ERR_INVALID_FORMAT;\n"
          "    }\n"
          "  }\n"
          "  *inout_len = pos;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          func_prefix, name_upper, symbol_count, symbol_count, module_name,
          module_name, dict_count, module_name);

  fprintf(cfile,
          "static const loxc_module_t mod_%s_module = {\n"
          "  .name = \"%s\",\n"
          "  .table_name = \"%s\",\n"
          "  .module_id = LOXC_MOD_%s_ID,\n"
          "  .version = LOXC_MOD_%s_VERSION,\n"
          "  .strategy_id = LOXC_MOD_%s_STRATEGY,\n"
          "  .level_count = 0u,\n"
          "  .table_fingerprint = MOD_%s_FINGERPRINT,\n"
          "  .encode = mod_%s_encode_buffer,\n"
          "  .decode = mod_%s_decode_buffer,\n"
          "};\n\n",
          module_name, module_name, module_name, name_upper, name_upper,
          name_upper, name_upper, module_name, module_name);

  fprintf(cfile,
          "static int mod_%s_encode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,\n"
          "                                size_t out_cap, size_t *out_len) {\n"
          "  if (out_len == NULL) return LOXC_ERR_NULL;\n"
          "  *out_len = 0;\n"
          "  if (in == NULL || out == NULL) return LOXC_ERR_NULL;\n"
          "  if (in_len > 0xFFFFFFFFu) return LOXC_ERR_OVERFLOW;\n"
          "\n"
          "  loxc_header_t h;\n"
          "  h.module_id = LOXC_MOD_%s_ID;\n"
          "  h.version = 2;\n"
          "  h.flags = 0;\n"
          "  h.strategy_id = (uint8_t)LOXC_MOD_%s_STRATEGY;\n"
          "  h.payload_len = 0;\n"
          "  h.level_count = 0;\n"
          "  h.uncompressed_len = (uint32_t)in_len;\n"
          "  h.table_fingerprint = MOD_%s_FINGERPRINT;\n"
          "  h.crc32 = 0;\n"
          "\n"
          "  const size_t header_bytes = loxc_header_size(&h);\n"
          "  if (out_cap < header_bytes) return LOXC_ERR_OVERFLOW;\n"
          "\n"
          "  loxc_writer_t w;\n"
          "  int rc = loxc_writer_init(&w, out + header_bytes, out_cap - header_bytes);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  rc = %s_encode((const char *)in, in_len, &w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  rc = loxc_writer_flush(&w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  const size_t data_bytes = loxc_writer_size(&w);\n"
          "  if (data_bytes > LOXC_HEADER_MAX_EXACT_PAYLOAD_LEN) return LOXC_ERR_OVERFLOW;\n"
          "  h.payload_len = (uint16_t)data_bytes;\n"
          "  const size_t total = header_bytes + data_bytes;\n"
          "  loxc_writer_t hw;\n"
          "  rc = loxc_writer_init(&hw, out, header_bytes);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  rc = loxc_header_write(&hw, &h);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  if (loxc_writer_size(&hw) != header_bytes) return LOXC_ERR_INVALID_FORMAT;\n"
          "\n"
          "  *out_len = total;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          module_name, name_upper, name_upper, name_upper, func_prefix);

  fprintf(cfile,
          "static int mod_%s_decode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,\n"
          "                                size_t out_cap, size_t *out_len) {\n"
          "  if (out_len == NULL) return LOXC_ERR_NULL;\n"
          "  *out_len = 0;\n"
          "  if (in == NULL || out == NULL) return LOXC_ERR_NULL;\n"
          "\n"
          "  loxc_reader_t hr;\n"
          "  int rc = loxc_reader_init(&hr, in, in_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  loxc_header_t h;\n"
          "  rc = loxc_header_read(&hr, &h);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  if (h.module_id != LOXC_MOD_%s_ID) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.version != LOXC_HEADER_VERSION_V2) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.flags != 0) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.strategy_id != (uint8_t)LOXC_MOD_%s_STRATEGY) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.table_fingerprint != MOD_%s_FINGERPRINT) return LOXC_ERR_INVALID_MAGIC;\n"
          "\n"
          "  const size_t header_bytes = loxc_header_size(&h);\n"
          "  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;\n"
          "  size_t data_len = 0;\n"
          "  rc = loxc_header_resolve_payload_len(&h, in_len - header_bytes, &data_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  loxc_reader_t r;\n"
          "  rc = loxc_reader_init(&r, in + header_bytes, data_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  const size_t expected_len = (size_t)h.uncompressed_len;\n"
          "  if (expected_len > out_cap) return LOXC_ERR_OVERFLOW;\n"
          "  size_t cap = expected_len;\n"
          "  rc = %s_decode(&r, (char *)out, &cap);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  if (cap != expected_len) return LOXC_ERR_TRUNCATED;\n"
          "  rc = loxc_reader_finish_zero_padding(&r);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  *out_len = cap;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          module_name, name_upper, name_upper, name_upper, func_prefix);

  fprintf(cfile,
          "int %s_register(void) {\n"
          "  return loxc_module_register(&mod_%s_module);\n"
          "}\n",
          func_prefix, module_name);

  fclose(cfile);
  printf("Generated: %s\n", source_path);
  return 0;
}

/* Load file into memory */
static int load_file(const char *path, uint8_t **out_data, size_t *out_len) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Error: cannot open %s: %s\n", path, strerror(errno));
    return 1;
  }

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  if (size < 0 || size > 100 * 1024 * 1024) {
    fprintf(stderr, "Error: file size invalid or too large\n");
    fclose(fp);
    return 1;
  }
  fseek(fp, 0, SEEK_SET);

  uint8_t *data = (uint8_t *)malloc((size_t)size);
  if (data == NULL) {
    fprintf(stderr, "Error: malloc failed\n");
    fclose(fp);
    return 1;
  }

  size_t nread = fread(data, 1, (size_t)size, fp);
  fclose(fp);

  if (nread != (size_t)size) {
    fprintf(stderr, "Error: read %zu bytes, expected %ld\n", nread, size);
    free(data);
    return 1;
  }

  *out_data = data;
  *out_len = (size_t)size;
  return 0;
}

static int get_file_size(const char *path, size_t *out_size) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Error: cannot open %s: %s\n", path, strerror(errno));
    return 1;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fprintf(stderr, "Error: cannot seek %s: %s\n", path, strerror(errno));
    fclose(fp);
    return 1;
  }

  long size = ftell(fp);
  fclose(fp);
  if (size < 0 || size > LOXC_TRAIN_MAX_TOTAL_BYTES) {
    fprintf(stderr, "Error: file size invalid or too large: %s\n", path);
    return 1;
  }

  *out_size = (size_t)size;
  return 0;
}

static int load_input_files(const char *inputs[], size_t input_count,
                            uint8_t **out_data, size_t *out_len) {
  *out_data = NULL;
  *out_len = 0;
  if (input_count == 1) return load_file(inputs[0], out_data, out_len);

  size_t total = 0;
  for (size_t i = 0; i < input_count; i++) {
    size_t size = 0;
    if (get_file_size(inputs[i], &size) != 0) return 1;
    if (size > (size_t)LOXC_TRAIN_MAX_TOTAL_BYTES - total) {
      fprintf(stderr, "Error: combined input size exceeds %u bytes\n",
              (unsigned)LOXC_TRAIN_MAX_TOTAL_BYTES);
      return 1;
    }
    total += size;
  }

  uint8_t *data = NULL;
  if (total > 0) {
    data = (uint8_t *)malloc(total);
    if (data == NULL) {
      fprintf(stderr, "Error: malloc failed for combined input\n");
      return 1;
    }
  }

  size_t offset = 0;
  for (size_t i = 0; i < input_count; i++) {
    size_t size = 0;
    if (get_file_size(inputs[i], &size) != 0) {
      free(data);
      return 1;
    }

    FILE *fp = fopen(inputs[i], "rb");
    if (fp == NULL) {
      fprintf(stderr, "Error: cannot open %s: %s\n", inputs[i], strerror(errno));
      free(data);
      return 1;
    }

    size_t nread = 0;
    if (size > 0) nread = fread(data + offset, 1, size, fp);
    fclose(fp);
    if (nread != size) {
      fprintf(stderr, "Error: read %zu bytes from %s, expected %zu\n",
              nread, inputs[i], size);
      free(data);
      return 1;
    }
    offset += size;
  }

  *out_data = data;
  *out_len = total;
  return 0;
}

static void describe_inputs(const char *inputs[], size_t input_count,
                            char *out, size_t out_cap) {
  if (out_cap == 0) return;
  out[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < input_count; i++) {
    const char *sep = (i == 0) ? "" : " + ";
    int n = snprintf(out + used, out_cap - used, "%s%s", sep, inputs[i]);
    if (n < 0) break;
    if ((size_t)n >= out_cap - used) {
      used = out_cap - 1;
      out[used] = '\0';
      break;
    }
    used += (size_t)n;
  }
}

static void free_symbol_recs(symbol_rec_t *symbols, size_t count) {
  if (symbols == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free(symbols[i].dict_bytes);
    symbols[i].dict_bytes = NULL;
    symbols[i].dict_len = 0;
  }
}

/* Compare symbols by count descending */
static int cmp_symbols(const void *a, const void *b) {
  const symbol_rec_t *sa = (const symbol_rec_t *)a;
  const symbol_rec_t *sb = (const symbol_rec_t *)b;
  if (sa->count > sb->count) return -1;
  if (sa->count < sb->count) return 1;
  if (sa->is_dict != sb->is_dict) return (sa->is_dict < sb->is_dict) ? -1 : 1;
  if (!sa->is_dict) {
    if (sa->char_val < sb->char_val) return -1;
    if (sa->char_val > sb->char_val) return 1;
    return 0;
  }
  return cmp_symbol_bytes(sa->dict_bytes, sa->dict_len,
                          sb->dict_bytes, sb->dict_len);
}

static int cmp_dict_entry_display(const void *a, const void *b) {
  const loxc_dict_entry_t *da = (const loxc_dict_entry_t *)a;
  const loxc_dict_entry_t *db = (const loxc_dict_entry_t *)b;
  if (da->count > db->count) return -1;
  if (da->count < db->count) return 1;
  if (da->gain > db->gain) return -1;
  if (da->gain < db->gain) return 1;
  return cmp_symbol_bytes((const uint8_t *)da->word, da->word_len,
                          (const uint8_t *)db->word, db->word_len);
}

/* Frequency analysis: chars + dict entries */
static int analyze_freqs(const uint8_t *data, size_t data_len,
                         symbol_rec_t **out_symbols, size_t *out_count,
                         size_t *out_header_size, size_t *out_dict_overhead,
                         size_t *out_symbol_table_overhead) {
  /* Step 1: Count uni-grams */
  uint64_t char_counts[256];
  memset(char_counts, 0, sizeof(char_counts));
  for (size_t i = 0; i < data_len; i++) {
    char_counts[data[i]]++;
  }
  printf("Step 1: Uni-grams counted\n");
  uint64_t raw_char_counts[256];
  memcpy(raw_char_counts, char_counts, sizeof(raw_char_counts));

  /* Step 2: Dict analysis */
  loxc_dict_t dict;
  memset(&dict, 0, sizeof(dict));
  int rc = loxc_dict_analyze((const char *)data, data_len, &dict);
  if (rc != LOXC_OK) {
    fprintf(stderr, "Error: dict_analyze failed: %d\n", rc);
    return 1;
  }
  printf("Step 2: Dict analyzed (%zu entries)\n", dict.count);

  /* Greedy global filter over gain>0 candidates */
  uint8_t *accepted_mask = NULL;
  size_t accepted_count = 0;
  if (dict.entries != NULL && dict.count > 0) {
    accepted_mask = (uint8_t *)calloc(dict.count, 1);
    if (accepted_mask == NULL) {
      fprintf(stderr, "Error: calloc accepted_mask\n");
      loxc_dict_free(&dict);
      return 1;
    }

    size_t cand_count = 0;
    for (size_t i = 0; i < dict.count; i++) {
      if (dict.entries[i].gain > 0 && dict.entries[i].word_len > 0 && dict.entries[i].count > 0) {
        cand_count++;
      }
    }

    dict_candidate_t *cands = NULL;
    if (cand_count > 0) {
      cands = (dict_candidate_t *)malloc(cand_count * sizeof(dict_candidate_t));
      if (cands == NULL) {
        fprintf(stderr, "Error: malloc candidates\n");
        free(accepted_mask);
        loxc_dict_free(&dict);
        return 1;
      }

      size_t pos = 0;
      for (size_t i = 0; i < dict.count; i++) {
        if (dict.entries[i].gain > 0 && dict.entries[i].word_len > 0 && dict.entries[i].count > 0) {
          cands[pos].idx = i;
          cands[pos].gain = dict.entries[i].gain;
          cands[pos].len = dict.entries[i].word_len;
          pos++;
        }
      }
      qsort(cands, cand_count, sizeof(dict_candidate_t), cmp_dict_candidate);
    }

    uint8_t bps_all = 0;
    size_t sym_all = 0;

    /* Baselines for diagnostics: all gain>0 vs greedy */
    uint8_t *all_mask = NULL;
    if (cand_count > 0) {
      all_mask = (uint8_t *)calloc(dict.count, 1);
      if (all_mask != NULL) {
        for (size_t i = 0; i < cand_count; i++) {
          all_mask[cands[i].idx] = 1;
        }
      }
    }
    const size_t total_all = compute_total_size_bytes(
        raw_char_counts, &dict, all_mask, cand_count, &bps_all, &sym_all);
    free(all_mask);

    size_t best_total = compute_total_size_bytes(
        raw_char_counts, &dict, accepted_mask, 0, NULL, NULL);

    for (size_t ci = 0; ci < cand_count; ci++) {
      const size_t idx = cands[ci].idx;
      accepted_mask[idx] = 1;
      const size_t trial_total = compute_total_size_bytes(
          raw_char_counts, &dict, accepted_mask, accepted_count + 1, NULL, NULL);
      if (trial_total < best_total) {
        best_total = trial_total;
        accepted_count++;
      } else {
        accepted_mask[idx] = 0;
      }
    }

    free(cands);

    /* Persist acceptance onto dict.entries via ref_id marker used later */
    for (size_t i = 0; i < dict.count; i++) {
      dict.entries[i].ref_id = accepted_mask[i] ? 0u : 0xFFFFu;
    }

    uint8_t bps_greedy = 0;
    size_t sym_greedy = 0;
    const size_t total_greedy = compute_total_size_bytes(
        raw_char_counts, &dict, accepted_mask, accepted_count, &bps_greedy, &sym_greedy);

    printf("\n=== Greedy Dict Filter ===\n");
    printf("Candidates (gain>0): %zu / %zu\n", cand_count, dict.count);
    printf("Accepted (greedy):   %zu\n", accepted_count);
    printf("All gain>0:          %zu bytes, symbols=%zu, bits/sym=%u\n",
           total_all, sym_all, (unsigned)bps_all);
    printf("After greedy:        %zu bytes, symbols=%zu, bits/sym=%u\n",
           total_greedy, sym_greedy, (unsigned)bps_greedy);
    if (total_all > 0) {
      const double improve = 100.0 * (double)((int64_t)total_all - (int64_t)total_greedy) / (double)total_all;
      printf("Improvement:         %.2f%%\n", improve);
    }
  }

  /* Step 3: Subtract dict occurrences from char counts */
  uint64_t chars_covered_by_dict = 0;
  if (dict.entries != NULL && dict.count > 0) {
    printf("Step 3: Subtracting dict entries...\n");
    for (size_t i = 0; i < dict.count; i++) {
      /* Skip rejected entries — they won't be in output symbols. */
      if (dict.entries[i].ref_id == 0xFFFFu) continue;
      const char *entry = dict.entries[i].word;
      size_t entry_len = dict.entries[i].word_len;
      uint64_t entry_count = dict.entries[i].count;

      if (entry == NULL || entry_len == 0) continue;

      /* Subtract this entry's characters from counts */
      for (size_t j = 0; j < entry_len; j++) {
        uint8_t ch = (uint8_t)entry[j];
        if (entry_count <= char_counts[ch]) {
          char_counts[ch] -= entry_count;
          chars_covered_by_dict += entry_count;
        }
      }
    }
    printf("Step 3: Dict subtraction complete (%llu chars covered)\n",
           (unsigned long long)chars_covered_by_dict);
  } else {
    printf("Step 3: No dict entries\n");
  }

  /* Step 4: Build symbol array (chars + dict entries) */
  size_t symbol_cap = 256 + (dict.count > 0 ? dict.count : 0);
  symbol_rec_t *symbols =
      (symbol_rec_t *)malloc(symbol_cap * sizeof(symbol_rec_t));
  if (symbols == NULL) {
    fprintf(stderr, "Error: malloc symbols\n");
    loxc_dict_free(&dict);
    return 1;
  }

  size_t symbol_count = 0;

  /* Add characters with count > 0 */
  size_t char_symbols = 0;
  for (int ch = 0; ch < 256; ch++) {
    if (char_counts[ch] > 0) {
      symbols[symbol_count].symbol_id = (uint32_t)symbol_count;
      symbols[symbol_count].count = char_counts[ch];
      symbols[symbol_count].is_dict = 0;
      symbols[symbol_count].char_val = (uint8_t)ch;
      symbols[symbol_count].dict_bytes = NULL;
      symbols[symbol_count].dict_len = 0;
      symbol_count++;
      char_symbols++;
    }
  }

  /* Add dict entries with positive gain (ref_id != 0xFFFF means accepted) */
  size_t accepted_dict_entries = 0;
  if (dict.entries != NULL && dict.count > 0) {
    for (size_t i = 0; i < dict.count; i++) {
      if (dict.entries[i].ref_id != 0xFFFFu) {
        /* Only add entries with gain > 0 (marked with valid ref_id) */
        size_t wlen = dict.entries[i].word_len;
        uint8_t *copy = NULL;
        if (wlen > 0) {
          copy = (uint8_t *)malloc(wlen);
          if (copy == NULL) {
            fprintf(stderr, "Error: malloc dict entry copy\n");
            free_symbol_recs(symbols, symbol_count);
            free(symbols);
            loxc_dict_free(&dict);
            return 1;
          }
          memcpy(copy, dict.entries[i].word, wlen);
        }
        symbols[symbol_count].symbol_id = (uint32_t)symbol_count;
        symbols[symbol_count].count = dict.entries[i].count;
        symbols[symbol_count].is_dict = 1;
        symbols[symbol_count].char_val = 0;
        symbols[symbol_count].dict_bytes = copy;
        symbols[symbol_count].dict_len = wlen;
        symbol_count++;
        accepted_dict_entries++;
      }
    }
  }

  /* Sort by count descending */
  qsort(symbols, symbol_count, sizeof(symbol_rec_t), cmp_symbols);

  /* Reassign symbol_ids after sort */
  for (size_t i = 0; i < symbol_count; i++) {
    symbols[i].symbol_id = (uint32_t)i;
  }

  /* Print diagnostics */
  printf("\n=== Frequency Analysis ===\n");
  printf("Total chars in text: %zu\n", data_len);
  printf("Chars covered by dict: %llu (%.1f%%)\n",
         (unsigned long long)chars_covered_by_dict,
         data_len > 0 ? (100.0 * chars_covered_by_dict / data_len) : 0.0);
  printf("Unique symbols: %zu (%zu chars + %zu dict entries)\n", symbol_count,
         char_symbols, accepted_dict_entries);
  printf("Dict entries analyzed: %zu total, %zu accepted (greedy), %zu rejected\n",
         dict.count, accepted_dict_entries, dict.count - accepted_dict_entries);

  printf("\nTop 10 symbols:\n");
  printf("  ID | Symbol       | Count      | %% of text\n");
  printf("-----+--------------+------------+-----------\n");
  for (size_t i = 0; i < symbol_count && i < 10; i++) {
    uint8_t ch = symbols[i].char_val;
    char ch_display[16];
    if (!symbols[i].is_dict) {
      if (ch >= 32 && ch < 127) {
        snprintf(ch_display, sizeof(ch_display), "'%c'", (char)ch);
      } else {
        snprintf(ch_display, sizeof(ch_display), "0x%02x", ch);
      }
      printf("%3u | %-12s | %10llu | %6.2f%%\n", symbols[i].symbol_id,
             ch_display, (unsigned long long)symbols[i].count,
             data_len > 0 ? (100.0 * symbols[i].count / data_len) : 0.0);
    } else {
      char dict_preview[64];
      size_t n = symbols[i].dict_len;
      if (n > 12) n = 12;
      size_t pos = 0;
      for (size_t j = 0; j < n && pos + 2 < sizeof(dict_preview); j++) {
        uint8_t b = symbols[i].dict_bytes ? symbols[i].dict_bytes[j] : 0;
        if (b >= 32 && b < 127) {
          dict_preview[pos++] = (char)b;
        } else {
          dict_preview[pos++] = '.';
        }
      }
      dict_preview[pos] = '\0';
      printf("%3u | %-12s | %10llu | %6.2f%%\n", symbols[i].symbol_id,
             dict_preview, (unsigned long long)symbols[i].count,
             data_len > 0 ? (100.0 * symbols[i].count / data_len) : 0.0);
    }
  }

  if (dict.count > 0) {
    printf("\nDict entries: %zu entries\n", dict.count);

    /* Sort dict by count for analysis */
    loxc_dict_entry_t *dict_sorted =
        (loxc_dict_entry_t *)malloc(dict.count * sizeof(loxc_dict_entry_t));
    if (dict_sorted != NULL) {
      memcpy(dict_sorted, dict.entries, dict.count * sizeof(loxc_dict_entry_t));

      qsort(dict_sorted, dict.count, sizeof(loxc_dict_entry_t),
            cmp_dict_entry_display);

      printf("\nDict entries (top 20):\n");
      printf("  ID | Entry          | Count | Gain (bits)\n");
      printf("-----+----------------+-------+------------\n");
      for (size_t i = 0; i < dict.count && i < 20; i++) {
        printf("%4zu | %-14.*s | %5llu | %10lld\n", i,
               (int)(dict_sorted[i].word_len < 14 ? dict_sorted[i].word_len
                                                    : 14),
               dict_sorted[i].word, (unsigned long long)dict_sorted[i].count,
               (long long)dict_sorted[i].gain);
      }

      if (dict.count > 20) {
        printf("...\n");
        printf("\nDict entries (bottom 20):\n");
        printf("  ID | Entry          | Count | Gain (bits)\n");
        printf("-----+----------------+-------+------------\n");
        size_t start = dict.count > 20 ? dict.count - 20 : 0;
        for (size_t i = start; i < dict.count; i++) {
          printf("%4zu | %-14.*s | %5llu | %10lld\n", i,
                 (int)(dict_sorted[i].word_len < 14 ? dict_sorted[i].word_len
                                                      : 14),
                 dict_sorted[i].word, (unsigned long long)dict_sorted[i].count,
                 (long long)dict_sorted[i].gain);
        }
      }

      free(dict_sorted);
    }
  }

  /* Estimate overhead using actual table/container layout */
  printf("\n=== Overhead Estimation ===\n");
  size_t header_size = LOXC_HEADER_SIZE_V2 + LOXC_TAB_HEADER_SIZE +
                       LOXC_TAB_TRAILER_SIZE;
  size_t dict_overhead = 4u; /* dict_data_size field */
  if (dict.count > 0) {
    dict_overhead += (accepted_dict_entries + 1u) * 4u; /* offsets */
    for (size_t i = 0; i < dict.count; i++) {
      if (dict.entries[i].ref_id != 0xFFFFu) {
        dict_overhead += dict.entries[i].word_len;
      }
    }
  }
  size_t symbol_table_overhead = 1024u + (symbol_count * 5u);
  size_t total_overhead = header_size + dict_overhead + symbol_table_overhead;

  printf("Header size: %zu bytes\n", header_size);
  printf("Dict overhead (accepted): %zu bytes\n", dict_overhead);
  printf("Symbol table (est.): %zu bytes\n", symbol_table_overhead);
  printf("Total overhead: %zu bytes (%.1f%% of input)\n", total_overhead,
         (100.0 * total_overhead / data_len));

  *out_symbols = symbols;
  *out_count = symbol_count;
  *out_header_size = header_size;
  *out_dict_overhead = dict_overhead;
  *out_symbol_table_overhead = symbol_table_overhead;

  loxc_dict_free(&dict);
  free(accepted_mask);
  return 0;
}

int main(int argc, char *argv[]) {
  const char *inputs[LOXC_TRAIN_MAX_INPUTS];
  size_t input_count = 0;
  const char *output, *module_name;
  uint8_t module_id;

  if (parse_args(argc, argv, inputs, &input_count, &output, &module_name,
                 &module_id) != 0) {
    return 1;
  }

  uint8_t *data = NULL;
  size_t data_len = 0;
  if (load_input_files(inputs, input_count, &data, &data_len) != 0) {
    return 1;
  }

  char input_desc[512];
  describe_inputs(inputs, input_count, input_desc, sizeof(input_desc));

  printf("Hello from loxc_train: inputs=%zu (%zu bytes)\n", input_count, data_len);
  for (size_t i = 0; i < input_count; i++) {
    printf("  input[%zu]: %s\n", i, inputs[i]);
  }
  printf("  output: %s\n", output);
  printf("  module: %s (id=%u)\n", module_name, module_id);

  /* Frequency analysis */
  symbol_rec_t *symbols = NULL;
  size_t symbol_count = 0;
  size_t header_size = 0, dict_overhead = 0, symbol_table_overhead = 0;
  if (analyze_freqs(data, data_len, &symbols, &symbol_count, &header_size,
                    &dict_overhead, &symbol_table_overhead) != 0) {
    free(data);
    return 1;
  }

  /* KROK 3: Strategy selection */
  printf("\n=== Strategy Selection ===\n");

  /* Build freq array for strategy selector */
  loxc_freq_entry_t *freqs =
      (loxc_freq_entry_t *)malloc(symbol_count * sizeof(loxc_freq_entry_t));
  if (freqs == NULL) {
    fprintf(stderr, "Error: malloc freqs\n");
    free_symbol_recs(symbols, symbol_count);
    free(symbols);
    free(data);
    return 1;
  }

  for (size_t i = 0; i < symbol_count; i++) {
    freqs[i].symbol_id = symbols[i].symbol_id;
    freqs[i].count = symbols[i].count;
  }

  /* Select strategy */
  size_t dict_count_for_strategy = 0u;
  size_t dict_data_bytes_for_strategy = 0u;
  strategy_eval_t evals[3];
  strategy_eval_t best_eval;
  loxc_strategy_result_t result;

  memset(&result, 0, sizeof(result));
  memset(&best_eval, 0, sizeof(best_eval));
  best_eval.total_bits = UINT64_MAX;
  best_eval.payload_bits = UINT64_MAX;
  best_eval.level_count = UINT16_MAX;
  best_eval.strategy = LOXC_STRATEGY_HIERARCHICAL_4;

  for (size_t i = 0; i < symbol_count; i++) {
    if (symbols[i].is_dict) {
      dict_count_for_strategy++;
      if (!checked_add_size(dict_data_bytes_for_strategy, symbols[i].dict_len,
                            &dict_data_bytes_for_strategy)) {
        fprintf(stderr, "Error: dict data size overflow\n");
        free(freqs);
        free_symbol_recs(symbols, symbol_count);
        free(symbols);
        free(data);
        return 1;
      }
    }
  }

  if (evaluate_strategy(freqs, symbol_count, dict_count_for_strategy,
                        dict_data_bytes_for_strategy,
                        LOXC_STRATEGY_FLAT_FIXED_WIDTH, &evals[0]) != 0 ||
      evaluate_strategy(freqs, symbol_count, dict_count_for_strategy,
                        dict_data_bytes_for_strategy,
                        LOXC_STRATEGY_HIERARCHICAL_8, &evals[1]) != 0 ||
      evaluate_strategy(freqs, symbol_count, dict_count_for_strategy,
                        dict_data_bytes_for_strategy,
                        LOXC_STRATEGY_HIERARCHICAL_4, &evals[2]) != 0) {
    fprintf(stderr, "Error: strategy evaluation failed\n");
    free(freqs);
    free_symbol_recs(symbols, symbol_count);
    free(symbols);
    free(data);
    return 1;
  }

  for (size_t i = 0; i < 3u; i++) {
    if (strategy_eval_better(&evals[i], &best_eval)) {
      best_eval = evals[i];
    }
  }

  result.strategy = best_eval.strategy;
  result.predicted_bits = best_eval.payload_bits;
  result.predicted_total_bits = best_eval.total_bits;
  result.level_count = best_eval.level_count;
  result.bits_per_symbol =
      best_eval.total_symbols > 0u
          ? (double)best_eval.payload_bits / (double)best_eval.total_symbols
          : 0.0;

  const char *strategy_name =
      (result.strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH)
          ? "FLAT"
          : (result.strategy == LOXC_STRATEGY_HIERARCHICAL_8)
                ? "HIER8"
                : (result.strategy == LOXC_STRATEGY_HIERARCHICAL_4) ? "HIER4"
                                                                     : "?";

  printf("Selected strategy: %s\n", strategy_name);
  printf("Predicted bits (encoding only): %llu\n",
         (unsigned long long)result.predicted_bits);
  printf("Bits per symbol: %.2f\n", result.bits_per_symbol);
  if (result.strategy != LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
    printf("Level count: %u\n", result.level_count);
  }

  /* Show all three strategies for comparison */
  printf("\nStrategy costs (all symbols, bits):\n");
  printf("  FLAT:  %llu bits (levels=%u, total=%zu bytes)\n",
         (unsigned long long)evals[0].payload_bits, evals[0].level_count,
         evals[0].total_bytes);
  printf("  HIER8: %llu bits (levels=%u, total=%zu bytes)\n",
         (unsigned long long)evals[1].payload_bits, evals[1].level_count,
         evals[1].total_bytes);
  printf("  HIER4: %llu bits (levels=%u, total=%zu bytes)\n",
         (unsigned long long)evals[2].payload_bits, evals[2].level_count,
         evals[2].total_bytes);

  /* Calculate total output size */
  uint64_t predicted_data_bits = result.predicted_bits;
  size_t predicted_data_bytes = (predicted_data_bits + 7) / 8;  /* round up */
  size_t total_output_bytes = best_eval.total_bytes;
  double compression_ratio =
      (data_len > 0u) ? (100.0 * total_output_bytes) / data_len : 0.0;

  printf("\n=== Output Size Estimation ===\n");
  printf("Data bits (encoding): %llu\n", (unsigned long long)predicted_data_bits);
  printf("Data bytes (rounded): %zu\n", predicted_data_bytes);
  printf("Header: %zu bytes\n", header_size);
  printf("Dict overhead: %zu bytes\n", dict_overhead);
  printf("Symbol table: %zu bytes\n", symbol_table_overhead);
  printf("Total output: %zu bytes (%.1f%% of input)\n", total_output_bytes,
         compression_ratio);

  /* Build hier if strategy is HIER */
  loxc_hier_t hier;
  memset(&hier, 0, sizeof(hier));
  if (result.strategy != LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
    int rc = loxc_hier_build(freqs, symbol_count, result.strategy, &hier);
    if (rc == LOXC_OK) {
      if (hier.level_count != result.level_count) {
        fprintf(stderr, "Error: hierarchy level-count mismatch\n");
        loxc_hier_free(&hier);
        free(freqs);
        free_symbol_recs(symbols, symbol_count);
        free(symbols);
        free(data);
        return 1;
      }
      printf("\nHierarchical structure built: %u levels, %u symbols\n",
             hier.level_count, hier.symbol_count);
    }
  }

  /* KROK 4: Generate .h header file */
  printf("\n=== KROK 4: Header File Generation ===\n");

  /* Create output path: modules/loxc_<module_name>.h */
  char header_path[512];
  snprintf(header_path, sizeof(header_path), "modules/loxc_%s.h", module_name);

  FILE *hfile = fopen(header_path, "w");
  if (hfile == NULL) {
    fprintf(stderr, "Error: cannot create %s: %s\n", header_path,
            strerror(errno));
    loxc_hier_free(&hier);
    free(freqs);
    free_symbol_recs(symbols, symbol_count);
    free(symbols);
    free(data);
    return 1;
  }

  char name_upper[128];
  name_to_upper(module_name, name_upper, sizeof(name_upper));

  char guard_name[160];
  snprintf(guard_name, sizeof(guard_name), "LOXC_MOD_%s_H", name_upper);

  char func_prefix[160];
  snprintf(func_prefix, sizeof(func_prefix), "loxc_mod_%s", module_name);

  const char *strat_str =
      (result.strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH)
          ? "LOXC_STRATEGY_FLAT_FIXED_WIDTH"
          : (result.strategy == LOXC_STRATEGY_HIERARCHICAL_8)
                ? "LOXC_STRATEGY_HIERARCHICAL_8"
                : "LOXC_STRATEGY_HIERARCHICAL_4";

  const char *strat_name =
      (result.strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH)
          ? "FLAT_FIXED_WIDTH"
          : (result.strategy == LOXC_STRATEGY_HIERARCHICAL_8)
                ? "HIERARCHICAL_8"
                : "HIERARCHICAL_4";

  const uint32_t levels =
      (result.strategy != LOXC_STRATEGY_FLAT_FIXED_WIDTH) ? (uint32_t)hier.level_count : 0u;

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  char generated_at[64];
  if (lt != NULL) {
    strftime(generated_at, sizeof(generated_at), "%Y-%m-%d %H:%M:%S", lt);
  } else {
    snprintf(generated_at, sizeof(generated_at), "%s", __DATE__);
  }

  /* Write header file */
  fprintf(hfile, "#ifndef %s\n", guard_name);
  fprintf(hfile, "#define %s\n", guard_name);
  fprintf(hfile, "\n");
  fprintf(hfile, "#include <stddef.h> /* size_t */\n");
  fprintf(hfile, "#include <stdint.h> /* uint32_t etc. */\n");
  fprintf(hfile, "\n");
  fprintf(hfile, "#include \"loxc.h\"          /* loxc_module_t */\n");
  fprintf(hfile, "#include \"loxc_stream.h\"   /* writer/reader */\n");
  fprintf(hfile, "#include \"loxc_strategy.h\" /* strategy enum */\n");
  fprintf(hfile, "\n");
  fprintf(hfile,
          "/* AUTO-GENERATED by loxc_train. DO NOT EDIT.\n"
          " * Source: %s (%zu bytes)\n"
          " * Generated: %s\n"
          " * Symbols: %zu, Strategy: %s\n"
          " * Predicted compression: %.1f%%\n"
          " */\n",
          input_desc, data_len, generated_at, symbol_count, strat_name,
          compression_ratio);
  fprintf(hfile, "\n");
  fprintf(hfile, "#define LOXC_MOD_%s_ID        %u\n", name_upper, module_id);
  fprintf(hfile, "#define LOXC_MOD_%s_VERSION   2\n", name_upper);
  fprintf(hfile, "#define LOXC_MOD_%s_STRATEGY  %s\n", name_upper, strat_str);
  fprintf(hfile, "#define LOXC_MOD_%s_SYMBOLS   %zu\n", name_upper, symbol_count);
  fprintf(hfile, "#define LOXC_MOD_%s_LEVELS    %u\n", name_upper, levels);
  fprintf(hfile, "\n");
  fprintf(hfile, "/* Encode plain text → bit stream */\n");
  fprintf(hfile,
          "int %s_encode(const char *text, size_t text_len, loxc_writer_t *w);\n",
          func_prefix);
  fprintf(hfile, "\n");
  fprintf(hfile, "/* Decode bit stream → plain text */\n");
  fprintf(hfile,
          "int %s_decode(loxc_reader_t *r, char *out, size_t *inout_len);\n",
          func_prefix);
  fprintf(hfile, "\n");
  fprintf(hfile, "/* Register module in global registry */\n");
  fprintf(hfile, "int %s_register(void);\n", func_prefix);
  fprintf(hfile, "\n");
  fprintf(hfile, "#endif /* %s */\n", guard_name);

  fclose(hfile);

  printf("Generated: %s\n", header_path);
  printf("Module name: %s\n", module_name);
  printf("Module ID: %u\n", module_id);
  printf("Strategy: %s\n", strat_str);
  printf("Symbols: %zu\n", symbol_count);
  printf("Levels: %u\n",
         result.strategy != LOXC_STRATEGY_FLAT_FIXED_WIDTH
             ? hier.level_count
             : 0);

  /* Read and display the generated header file */
  printf("\n=== Generated Header File Content ===\n");
  FILE *hread = fopen(header_path, "r");
  if (hread != NULL) {
    int ch;
    while ((ch = fgetc(hread)) != EOF) {
      putchar(ch);
    }
    fclose(hread);
  }

  /* KROK 5: Generate .c source file */
  printf("\n=== KROK 5: Source File Generation ===\n");
  int gen_rc = 0;
  if (result.strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
    /* Prepare byte->symbol lookup and dict list */
    uint32_t byte_to_symbol[256];
    for (int i = 0; i < 256; i++) byte_to_symbol[i] = 0xFFFFFFFFu;

    size_t dict_count = 0;
    for (size_t i = 0; i < symbol_count; i++) {
      if (symbols[i].is_dict) dict_count++;
    }

    dict_emit_t *dict_emit = NULL;
    if (dict_count > 0) {
      dict_emit = (dict_emit_t *)malloc(dict_count * sizeof(dict_emit_t));
      if (dict_emit == NULL) {
        fprintf(stderr, "Error: malloc dict_emit\n");
        loxc_hier_free(&hier);
        free(freqs);
        free_symbol_recs(symbols, symbol_count);
        free(symbols);
        free(data);
        return 1;
      }
    }

    size_t dict_pos = 0;
    for (size_t i = 0; i < symbol_count; i++) {
      if (!symbols[i].is_dict) {
        byte_to_symbol[symbols[i].char_val] = symbols[i].symbol_id;
      } else {
        dict_emit[dict_pos].bytes = symbols[i].dict_bytes;
        dict_emit[dict_pos].len = symbols[i].dict_len;
        dict_emit[dict_pos].symbol_id = symbols[i].symbol_id;
        dict_pos++;
      }
    }
    if (dict_count > 0) {
      qsort(dict_emit, dict_count, sizeof(dict_emit_t), cmp_dict_emit);
    }

    uint32_t *symbol_to_dict_index =
        (uint32_t *)malloc(symbol_count * sizeof(uint32_t));
    if (symbol_to_dict_index == NULL) {
      fprintf(stderr, "Error: malloc symbol_to_dict_index\n");
      free(dict_emit);
      loxc_hier_free(&hier);
      free(freqs);
      free_symbol_recs(symbols, symbol_count);
      free(symbols);
      free(data);
      return 1;
    }
    for (size_t i = 0; i < symbol_count; i++) symbol_to_dict_index[i] = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < (uint32_t)dict_count; i++) {
      const uint32_t sid = dict_emit[i].symbol_id;
      if (sid < symbol_count) symbol_to_dict_index[sid] = i;
    }
    uint32_t table_fingerprint = 0u;
    gen_rc = write_loxctab_from_emit(output, module_name, module_id, 2u,
                                     symbols, symbol_count,
                                     byte_to_symbol, dict_emit, dict_count,
                                     symbol_to_dict_index,
                                     (uint8_t)result.strategy, 0u, 0u, 0u,
                                     &table_fingerprint);
    if (gen_rc == 0) {
      gen_rc = generate_c_file_flat(input_desc, data_len, module_name, name_upper,
                                    generated_at, module_id, table_fingerprint,
                                    symbols, symbol_count,
                                    dict_emit, dict_count, byte_to_symbol,
                                    symbol_to_dict_index, func_prefix);
    }

    free(symbol_to_dict_index);
    free(dict_emit);
  } else if (result.strategy == LOXC_STRATEGY_HIERARCHICAL_8 ||
             result.strategy == LOXC_STRATEGY_HIERARCHICAL_4) {
    /* Prepare byte->symbol lookup and dict list (same as FLAT) */
    uint32_t byte_to_symbol[256];
    for (int i = 0; i < 256; i++) byte_to_symbol[i] = 0xFFFFFFFFu;

    size_t dict_count = 0;
    for (size_t i = 0; i < symbol_count; i++) {
      if (symbols[i].is_dict) dict_count++;
    }

    dict_emit_t *dict_emit = NULL;
    if (dict_count > 0) {
      dict_emit = (dict_emit_t *)malloc(dict_count * sizeof(dict_emit_t));
      if (dict_emit == NULL) {
        fprintf(stderr, "Error: malloc dict_emit\n");
        loxc_hier_free(&hier);
        free(freqs);
        free_symbol_recs(symbols, symbol_count);
        free(symbols);
        free(data);
        return 1;
      }
    }

    size_t dict_pos = 0;
    for (size_t i = 0; i < symbol_count; i++) {
      if (!symbols[i].is_dict) {
        byte_to_symbol[symbols[i].char_val] = symbols[i].symbol_id;
      } else {
        dict_emit[dict_pos].bytes = symbols[i].dict_bytes;
        dict_emit[dict_pos].len = symbols[i].dict_len;
        dict_emit[dict_pos].symbol_id = symbols[i].symbol_id;
        dict_pos++;
      }
    }
    if (dict_count > 0) {
      qsort(dict_emit, dict_count, sizeof(dict_emit_t), cmp_dict_emit);
    }

    uint32_t *symbol_to_dict_index =
        (uint32_t *)malloc(symbol_count * sizeof(uint32_t));
    if (symbol_to_dict_index == NULL) {
      fprintf(stderr, "Error: malloc symbol_to_dict_index\n");
      free(dict_emit);
      loxc_hier_free(&hier);
      free(freqs);
      free_symbol_recs(symbols, symbol_count);
      free(symbols);
      free(data);
      return 1;
    }
    for (size_t i = 0; i < symbol_count; i++) symbol_to_dict_index[i] = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < (uint32_t)dict_count; i++) {
      const uint32_t sid = dict_emit[i].symbol_id;
      if (sid < symbol_count) symbol_to_dict_index[sid] = i;
    }

    const uint8_t base_size = best_eval.desc.base_size;
    const uint8_t bits_per_level = best_eval.desc.bits_per_level;
    uint32_t table_fingerprint = 0u;
    gen_rc = write_loxctab_from_emit(output, module_name, module_id, 2u,
                                     symbols, symbol_count,
                                     byte_to_symbol, dict_emit, dict_count,
                                     symbol_to_dict_index,
                                     (uint8_t)result.strategy, base_size,
                                     bits_per_level,
                                     (uint16_t)hier.level_count,
                                     &table_fingerprint);
    if (gen_rc == 0) {
      gen_rc = generate_c_file_hier(input_desc, data_len, module_name, name_upper,
                                    generated_at, module_id, table_fingerprint,
                                    result.strategy,
                                    symbols, symbol_count,
                                    dict_emit, dict_count,
                                    byte_to_symbol, symbol_to_dict_index,
                                    func_prefix, &hier);
    }

    free(symbol_to_dict_index);
    free(dict_emit);
  } else {
    fprintf(stderr, "Error: unsupported strategy=%d\n", (int)result.strategy);
    gen_rc = 1;
  }

  if (gen_rc != 0) {
    loxc_hier_free(&hier);
    free(freqs);
    free_symbol_recs(symbols, symbol_count);
    free(symbols);
    free(data);
    return 1;
  }

  loxc_hier_free(&hier);
  free(freqs);
  free_symbol_recs(symbols, symbol_count);
  free(symbols);
  free(data);
  return 0;
}
