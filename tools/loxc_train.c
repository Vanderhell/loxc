#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "loxc_dict.h"
#include "loxc_hier.h"
#include "loxc_strategy.h"

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
  int gain;
  size_t len;
} dict_candidate_t;

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

static size_t compute_total_size_bytes(const uint64_t raw_char_counts[256],
                                      const loxc_dict_t *dict,
                                      const uint8_t *accepted_mask,
                                      size_t accepted_count,
                                      uint8_t *out_bits_per_symbol,
                                      size_t *out_symbol_count) {
  uint64_t char_counts[256];
  memcpy(char_counts, raw_char_counts, sizeof(char_counts));

  uint64_t dict_occurrences = 0;
  size_t dict_overhead = 0;
  if (dict != NULL && dict->entries != NULL && dict->count > 0) {
    for (size_t i = 0; i < dict->count; i++) {
      if (accepted_mask == NULL || accepted_mask[i] == 0) continue;
      const char *word = dict->entries[i].word;
      const size_t wlen = dict->entries[i].word_len;
      const uint64_t cnt = dict->entries[i].count;
      if (word == NULL || wlen == 0 || cnt == 0) continue;

      dict_occurrences += cnt;
      dict_overhead += (wlen + 8u);

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
  const uint8_t bps = bits_needed_u32((uint32_t)symbol_count);
  const uint64_t total_occ = byte_occurrences + dict_occurrences;
  const uint64_t data_bits = (uint64_t)bps * total_occ;
  const size_t data_bytes = (size_t)((data_bits + 7u) / 8u);

  const size_t header_bytes = 15; /* v2 header without CRC */
  const size_t total_bytes = header_bytes + dict_overhead + data_bytes;

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
  const size_t n = da->len;
  if (n == 0) return 0;
  const int rc = memcmp(da->bytes, db->bytes, n);
  if (rc != 0) return rc;
  return 0;
}

static int generate_c_file_flat(const char *input, size_t data_len,
                                const char *module_name,
                                const char *name_upper,
                                const char *generated_at,
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
                                loxc_strategy_t strategy,
                                const symbol_rec_t *symbols,
                                size_t symbol_count,
                                const dict_emit_t *dict_emit,
                                size_t dict_count,
                                const uint32_t byte_to_symbol[256],
                                const uint32_t *symbol_to_dict_index,
                                const char *func_prefix,
                                const loxc_hier_t *hier);

static int parse_args(int argc, char *argv[], const char **input,
                      const char **output, const char **module_name,
                      uint8_t *module_id) {
  *input = NULL;
  *output = NULL;
  *module_name = NULL;
  *module_id = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      *input = argv[++i];
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

  if (*input == NULL || *output == NULL || *module_name == NULL ||
      *module_id == 0) {
    fprintf(stderr,
            "Usage: loxc_train --input <file> --output <dir> --module-name "
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

static int generate_c_file_hier(const char *input, size_t data_len,
                                const char *module_name,
                                const char *name_upper,
                                const char *generated_at,
                                loxc_strategy_t strategy,
                                const symbol_rec_t *symbols,
                                size_t symbol_count,
                                const dict_emit_t *dict_emit,
                                size_t dict_count,
                                const uint32_t byte_to_symbol[256],
                                const uint32_t *symbol_to_dict_index,
                                const char *func_prefix,
                                const loxc_hier_t *hier) {
  (void)hier;

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

  const uint32_t base_size = (strategy == LOXC_STRATEGY_HIERARCHICAL_8) ? 8u : 4u;
  const uint32_t direct_slots = (strategy == LOXC_STRATEGY_HIERARCHICAL_8) ? 56u : 15u;
  const uint32_t bits_per_level = (strategy == LOXC_STRATEGY_HIERARCHICAL_8) ? 6u : 4u;
  const size_t dict_emit_count = (dict_count > 0) ? dict_count : 1;

  fprintf(cfile, "#define MOD_%s_BASE_SIZE      %uu\n", name_upper, (unsigned)base_size);
  fprintf(cfile, "#define MOD_%s_ESCAPE_POS     %uu\n", name_upper, (unsigned)direct_slots);
  fprintf(cfile, "#define MOD_%s_DIRECT_SLOTS   %uu\n", name_upper, (unsigned)direct_slots);
  fprintf(cfile, "#define MOD_%s_BITS_PER_LEVEL %uu\n", name_upper, (unsigned)bits_per_level);
  fprintf(cfile, "#define MOD_%s_DICT_COUNT     %zuu\n", name_upper, dict_count);
  fprintf(cfile, "#define MOD_%s_LEVELS         %uu\n", name_upper, (unsigned)(hier ? hier->level_count : 0u));
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
          "      if (sym_id == 0xFFFFFFFFu) return LOXC_ERR_SYMBOL_NOT_FOUND;\n"
          "      consumed = 1;\n"
          "    }\n"
          "\n"
          "    const uint32_t level = sym_id / (uint32_t)MOD_%s_DIRECT_SLOTS;\n"
          "    const uint32_t pos_in_level = sym_id %% (uint32_t)MOD_%s_DIRECT_SLOTS;\n"
          "\n"
          "    for (uint32_t l = 0; l < level; l++) {\n"
          "      int rc = loxc_write_bits(w, (uint32_t)MOD_%s_ESCAPE_POS,\n"
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
          name_upper, name_upper,
          name_upper, name_upper, name_upper);

  fprintf(cfile,
          "int %s_decode(loxc_reader_t *r, char *out, size_t *inout_len) {\n"
          "  if (r == NULL || out == NULL || inout_len == NULL) return LOXC_ERR_NULL;\n"
          "\n"
          "  size_t cap = *inout_len;\n"
          "  size_t written = 0;\n"
          "\n"
          "  for (;;) {\n"
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
          "    }\n"
          "\n"
          "    if (!found) return LOXC_ERR_INVALID_FORMAT;\n"
          "\n"
          "    uint32_t sym_id = level * (uint32_t)MOD_%s_DIRECT_SLOTS + pos;\n"
          "    if (sym_id >= (uint32_t)LOXC_MOD_%s_SYMBOLS) return LOXC_ERR_INVALID_FORMAT;\n"
          "\n"
          "    const mod_%s_symbol_t sym = mod_%s_symbols[sym_id];\n"
          "    if (sym.type == 0u) {\n"
          "      if (written >= cap) return LOXC_ERR_OVERFLOW;\n"
          "      out[written++] = (char)(uint8_t)(sym.byte_or_idx & 0xFFu);\n"
          "    } else {\n"
          "      size_t dlen = 0;\n"
          "      const uint8_t *dbytes = mod_%s_dict_entry(sym.byte_or_idx, &dlen);\n"
          "      if (written + dlen > cap) return LOXC_ERR_OVERFLOW;\n"
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
          name_upper, name_upper, name_upper,
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
          "  loxc_writer_t w;\n"
          "  int rc = loxc_writer_init(&w, out, out_cap);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  loxc_header_t h;\n"
          "  h.module_id = LOXC_MOD_%s_ID;\n"
          "  h.version = 2;\n"
          "  h.flags = 0;\n"
          "  h.strategy_id = (uint8_t)LOXC_MOD_%s_STRATEGY;\n"
          "  h.data_len = 0;\n"
          "  h.level_count = (uint16_t)MOD_%s_LEVELS;\n"
          "  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;\n"
          "  h.crc32 = 0;\n"
          "\n"
          "  rc = loxc_header_write(&w, &h);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  rc = %s_encode((const char *)in, in_len, &w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  rc = loxc_writer_flush(&w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  const size_t total = loxc_writer_size(&w);\n"
          "  const size_t header_bytes = 15;\n"
          "  if (total < header_bytes) return LOXC_ERR_INVALID_FORMAT;\n"
          "  const size_t data_bytes = total - header_bytes;\n"
          "  if (data_bytes > 0xFFFFu) return LOXC_ERR_OVERFLOW;\n"
          "  out[7] = (uint8_t)(data_bytes & 0xFFu);\n"
          "  out[8] = (uint8_t)((data_bytes >> 8) & 0xFFu);\n"
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
          "  if (h.version != 2) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.flags != 0) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.strategy_id != (uint8_t)LOXC_MOD_%s_STRATEGY) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.level_count != (uint16_t)MOD_%s_LEVELS) return LOXC_ERR_INVALID_MAGIC;\n"
          "\n"
          "  const size_t header_bytes = 15;\n"
          "  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;\n"
          "  if ((size_t)h.data_len > (in_len - header_bytes)) return LOXC_ERR_TRUNCATED;\n"
          "\n"
          "  loxc_reader_t r;\n"
          "  rc = loxc_reader_init(&r, in + header_bytes, (size_t)h.data_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  size_t cap = out_cap;\n"
          "  rc = %s_decode(&r, (char *)out, &cap);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  *out_len = cap;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          module_name, name_upper, name_upper, name_upper, func_prefix);

  fprintf(cfile,
          "static const loxc_module_t mod_%s_module = {\n"
          "  .name = \"%s\",\n"
          "  .module_id = LOXC_MOD_%s_ID,\n"
          "  .version = LOXC_MOD_%s_VERSION,\n"
          "  .strategy_id = LOXC_MOD_%s_STRATEGY,\n"
          "  .encode = mod_%s_encode_buffer,\n"
          "  .decode = mod_%s_decode_buffer,\n"
          "};\n\n",
          module_name, module_name, name_upper, name_upper, name_upper,
          module_name, module_name);

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
                                const symbol_rec_t *symbols,
                                size_t symbol_count,
                                const dict_emit_t *dict_emit,
                                size_t dict_count,
                                const uint32_t byte_to_symbol[256],
                                const uint32_t *symbol_to_dict_index,
                                const char *func_prefix) {
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

  const uint8_t sym_bits = bits_needed_u32((uint32_t)symbol_count);

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

  fprintf(cfile, "enum { MOD_%s_SYMBOL_BITS = %u };\n", name_upper, sym_bits);
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
          "      if (sym_id == 0xFFFFFFFFu) return LOXC_ERR_SYMBOL_NOT_FOUND;\n"
          "      consumed = 1;\n"
          "    }\n"
          "    int rc = loxc_write_bits(w, sym_id, MOD_%s_SYMBOL_BITS);\n"
          "    if (rc != LOXC_OK) return rc;\n"
          "    i += consumed;\n"
          "  }\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          func_prefix, dict_count, module_name, module_name, module_name, name_upper);

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
          func_prefix, name_upper, symbol_count, module_name, module_name,
          dict_count, module_name);

  fprintf(cfile,
          "static const loxc_module_t mod_%s_module = {\n"
          "  .name = \"%s\",\n"
          "  .module_id = LOXC_MOD_%s_ID,\n"
          "  .version = LOXC_MOD_%s_VERSION,\n"
          "  .strategy_id = LOXC_MOD_%s_STRATEGY,\n"
          "  .encode = mod_%s_encode_buffer,\n"
          "  .decode = mod_%s_decode_buffer,\n"
          "};\n\n",
          module_name, module_name, name_upper, name_upper, name_upper,
          module_name, module_name);

  fprintf(cfile,
          "static int mod_%s_encode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,\n"
          "                                size_t out_cap, size_t *out_len) {\n"
          "  if (out_len == NULL) return LOXC_ERR_NULL;\n"
          "  *out_len = 0;\n"
          "  if (in == NULL || out == NULL) return LOXC_ERR_NULL;\n"
          "  if (in_len > 0xFFFFFFFFu) return LOXC_ERR_OVERFLOW;\n"
          "\n"
          "  loxc_writer_t w;\n"
          "  int rc = loxc_writer_init(&w, out, out_cap);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  loxc_header_t h;\n"
          "  h.module_id = LOXC_MOD_%s_ID;\n"
          "  h.version = 2;\n"
          "  h.flags = 0;\n"
          "  h.strategy_id = (uint8_t)LOXC_MOD_%s_STRATEGY;\n"
          "  h.data_len = 0;\n"
          "  h.level_count = 0;\n"
          "  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;\n"
          "  h.crc32 = 0;\n"
          "\n"
          "  rc = loxc_header_write(&w, &h);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  rc = %s_encode((const char *)in, in_len, &w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  rc = loxc_writer_flush(&w);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  const size_t total = loxc_writer_size(&w);\n"
          "  const size_t header_bytes = 15;\n"
          "  if (total < header_bytes) return LOXC_ERR_INVALID_FORMAT;\n"
          "  const size_t data_bytes = total - header_bytes;\n"
          "  if (data_bytes > 0xFFFFu) return LOXC_ERR_OVERFLOW;\n"
          "  out[7] = (uint8_t)(data_bytes & 0xFFu);\n"
          "  out[8] = (uint8_t)((data_bytes >> 8) & 0xFFu);\n"
          "\n"
          "  *out_len = total;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          module_name, name_upper, name_upper, func_prefix);

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
          "  if (h.version != 2) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.flags != 0) return LOXC_ERR_INVALID_MAGIC;\n"
          "  if (h.strategy_id != (uint8_t)LOXC_MOD_%s_STRATEGY) return LOXC_ERR_INVALID_MAGIC;\n"
          "\n"
          "  const size_t header_bytes = 15;\n"
          "  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;\n"
          "  if ((size_t)h.data_len > (in_len - header_bytes)) return LOXC_ERR_TRUNCATED;\n"
          "\n"
          "  loxc_reader_t r;\n"
          "  rc = loxc_reader_init(&r, in + header_bytes, (size_t)h.data_len);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "\n"
          "  size_t cap = out_cap;\n"
          "  rc = %s_decode(&r, (char *)out, &cap);\n"
          "  if (rc != LOXC_OK) return rc;\n"
          "  *out_len = cap;\n"
          "  return LOXC_OK;\n"
          "}\n\n",
          module_name, name_upper, name_upper, func_prefix);

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
  return 0;
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

      /* Simple bubble sort by count descending */
      for (size_t i = 0; i < dict.count; i++) {
        for (size_t j = i + 1; j < dict.count; j++) {
          if (dict_sorted[j].count > dict_sorted[i].count) {
            loxc_dict_entry_t tmp = dict_sorted[i];
            dict_sorted[i] = dict_sorted[j];
            dict_sorted[j] = tmp;
          }
        }
      }

      printf("\nDict entries (top 20):\n");
      printf("  ID | Entry          | Count | Gain (bits)\n");
      printf("-----+----------------+-------+------------\n");
      for (size_t i = 0; i < dict.count && i < 20; i++) {
        printf("%4zu | %-14.*s | %5llu | %10d\n", i,
               (int)(dict_sorted[i].word_len < 14 ? dict_sorted[i].word_len
                                                    : 14),
               dict_sorted[i].word, (unsigned long long)dict_sorted[i].count,
               dict_sorted[i].gain);
      }

      if (dict.count > 20) {
        printf("...\n");
        printf("\nDict entries (bottom 20):\n");
        printf("  ID | Entry          | Count | Gain (bits)\n");
        printf("-----+----------------+-------+------------\n");
        size_t start = dict.count > 20 ? dict.count - 20 : 0;
        for (size_t i = start; i < dict.count; i++) {
          printf("%4zu | %-14.*s | %5llu | %10d\n", i,
                 (int)(dict_sorted[i].word_len < 14 ? dict_sorted[i].word_len
                                                      : 14),
                 dict_sorted[i].word, (unsigned long long)dict_sorted[i].count,
                 dict_sorted[i].gain);
        }
      }

      free(dict_sorted);
    }
  }

  /* Estimate overhead */
  printf("\n=== Overhead Estimation ===\n");
  size_t header_size = 19;  /* loxc_header_t with CRC */
  size_t dict_overhead = 0;
  if (dict.count > 0) {
    /* Only count accepted entries: ~8 bytes overhead + word_len per entry */
    for (size_t i = 0; i < dict.count; i++) {
      if (dict.entries[i].ref_id != 0xFFFFu) {
        dict_overhead += 8 + dict.entries[i].word_len;
      }
    }
  }
  size_t symbol_table_overhead = symbol_count * 8;  /* ~8 bytes per symbol estimate */
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
  const char *input, *output, *module_name;
  uint8_t module_id;

  if (parse_args(argc, argv, &input, &output, &module_name, &module_id) !=
      0) {
    return 1;
  }

  uint8_t *data = NULL;
  size_t data_len = 0;
  if (load_file(input, &data, &data_len) != 0) {
    return 1;
  }

  printf("Hello from loxc_train: input=%s (%zu bytes)\n", input, data_len);
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
  loxc_strategy_result_t result = loxc_strategy_select(freqs, symbol_count);

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
  uint16_t lvl8 = 0, lvl4 = 0;
  uint64_t cost_flat = loxc_strategy_cost_flat(freqs, symbol_count);
  uint64_t cost_hier8 =
      loxc_strategy_cost_hierarchical(freqs, symbol_count, 8, 8, &lvl8);
  uint64_t cost_hier4 =
      loxc_strategy_cost_hierarchical(freqs, symbol_count, 4, 4, &lvl4);

  printf("\nStrategy costs (all symbols, bits):\n");
  printf("  FLAT:  %llu bits\n", (unsigned long long)cost_flat);
  printf("  HIER8: %llu bits (levels=%u)\n", (unsigned long long)cost_hier8,
         lvl8);
  printf("  HIER4: %llu bits (levels=%u)\n", (unsigned long long)cost_hier4,
         lvl4);

  /* Calculate total output size */
  uint64_t predicted_data_bits = result.predicted_bits;
  size_t predicted_data_bytes = (predicted_data_bits + 7) / 8;  /* round up */
  size_t total_output_bytes = header_size + dict_overhead + symbol_table_overhead + predicted_data_bytes;
  double compression_ratio = (100.0 * total_output_bytes) / data_len;

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
      (result.strategy != LOXC_STRATEGY_FLAT_FIXED_WIDTH) ? (uint32_t)result.level_count : 0u;

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
          input, data_len, generated_at, symbol_count, strat_name,
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

    gen_rc = generate_c_file_flat(input, data_len, module_name, name_upper,
                                  generated_at, symbols, symbol_count,
                                  dict_emit, dict_count, byte_to_symbol,
                                  symbol_to_dict_index, func_prefix);

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

    gen_rc = generate_c_file_hier(input, data_len, module_name, name_upper,
                                  generated_at, result.strategy,
                                  symbols, symbol_count,
                                  dict_emit, dict_count,
                                  byte_to_symbol, symbol_to_dict_index,
                                  func_prefix, &hier);

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
