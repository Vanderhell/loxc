#include "loxc_tab.h"

#include "loxc_base.h"
#include "loxc_stream.h"

#include <errno.h>
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
  uint8_t module_id;
  uint8_t strategy_id;
  uint8_t base_size;
  uint8_t bits_per_level;
  uint8_t direct_slots;
  uint8_t escape_pos;
  uint16_t level_count;
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

static loxc_loaded_module_ctx_t *g_current_ctx = NULL;

static uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8u) |
         ((uint32_t)p[2] << 16u) |
         ((uint32_t)p[3] << 24u);
}

static char *loxc_strdup_range(const char *start, size_t len) {
  char *out = (char *)malloc(len + 1u);
  if (out == NULL) return NULL;
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

static char *module_name_from_path(const char *path) {
  const char *base = strrchr(path, '/');
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
  free(ctx->byte_to_symbol);
  free(ctx->symbols);
  free(ctx->dict_offsets);
  free(ctx->dict_data);
  free(ctx->raw_loxctab);
  free(ctx);
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

  const uint8_t flat_bits = bits_needed_u32(ctx->symbol_count);
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
      if (sym_id == 0xFFFFFFFFu) return LOXC_ERR_SYMBOL_NOT_FOUND;
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
        int rc = loxc_write_bits(w, (uint32_t)ctx->escape_pos,
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
  const uint8_t flat_bits = bits_needed_u32(ctx->symbol_count);

  for (;;) {
    if (written == cap) goto done;

    uint32_t sym_id = 0;
    if (ctx->strategy_id == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
      int rc = loxc_read_bits(r, flat_bits, &sym_id);
      if (rc != LOXC_OK) goto done;
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
        if (rc != LOXC_OK) {
          if (level == 0) goto done;
          return rc;
        }
        if (val < (uint32_t)ctx->direct_slots) {
          pos = val;
          found = 1;
          break;
        }
      }
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

static int loaded_encode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,
                                size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;
  if (g_current_ctx == NULL) return LOXC_ERR_INVALID_MODULE;
  if (in_len > 0xFFFFFFFFu) return LOXC_ERR_OVERFLOW;

  loxc_writer_t w;
  int rc = loxc_writer_init(&w, out, out_cap);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  h.module_id = g_current_ctx->module_id;
  h.version = 2u;
  h.flags = 0u;
  h.strategy_id = g_current_ctx->strategy_id;
  h.data_len = 0u;
  h.level_count = g_current_ctx->level_count;
  h.reserved[0] = (uint8_t)(in_len & 0xFFu);
  h.reserved[1] = (uint8_t)((in_len >> 8) & 0xFFu);
  h.reserved[2] = (uint8_t)((in_len >> 16) & 0xFFu);
  h.reserved[3] = (uint8_t)((in_len >> 24) & 0xFFu);
  h.crc32 = 0u;

  rc = loxc_header_write(&w, &h);
  if (rc != LOXC_OK) return rc;

  rc = loxc_generic_encode((const char *)in, in_len, &w, g_current_ctx);
  if (rc != LOXC_OK) return rc;
  rc = loxc_writer_flush(&w);
  if (rc != LOXC_OK) return rc;

  const size_t total = loxc_writer_size(&w);
  const size_t header_bytes = 15u;
  if (total < header_bytes) return LOXC_ERR_INVALID_FORMAT;
  const size_t data_bytes = total - header_bytes;
  h.data_len = data_bytes > 0xFFFFu ? 0xFFFFu : (uint16_t)data_bytes;

  loxc_writer_t hw;
  rc = loxc_writer_init(&hw, out, header_bytes);
  if (rc != LOXC_OK) return rc;
  rc = loxc_header_write(&hw, &h);
  if (rc != LOXC_OK) return rc;
  if (loxc_writer_size(&hw) != header_bytes) return LOXC_ERR_INVALID_FORMAT;

  *out_len = total;
  return LOXC_OK;
}

static int loaded_decode_buffer(const uint8_t *in, size_t in_len, uint8_t *out,
                                size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;
  if (g_current_ctx == NULL) return LOXC_ERR_INVALID_MODULE;

  loxc_reader_t hr;
  int rc = loxc_reader_init(&hr, in, in_len);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  rc = loxc_header_read(&hr, &h);
  if (rc != LOXC_OK) return rc;
  if (h.module_id != g_current_ctx->module_id) return LOXC_ERR_INVALID_MAGIC;
  if (h.version != 2u) return LOXC_ERR_INVALID_MAGIC;
  if (h.flags != 0u) return LOXC_ERR_INVALID_MAGIC;
  if (h.strategy_id != g_current_ctx->strategy_id) return LOXC_ERR_INVALID_MAGIC;
  if (h.level_count != g_current_ctx->level_count) return LOXC_ERR_INVALID_MAGIC;

  const size_t header_bytes = 15u;
  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;
  size_t data_len = (size_t)h.data_len;
  if (h.data_len == 0xFFFFu) {
    data_len = in_len - header_bytes;
  } else if (data_len > in_len - header_bytes) {
    return LOXC_ERR_TRUNCATED;
  }

  const size_t expected_len =
      (size_t)h.reserved[0] |
      ((size_t)h.reserved[1] << 8u) |
      ((size_t)h.reserved[2] << 16u) |
      ((size_t)h.reserved[3] << 24u);
  if (expected_len > out_cap) return LOXC_ERR_OVERFLOW;

  loxc_reader_t r;
  rc = loxc_reader_init(&r, in + header_bytes, data_len);
  if (rc != LOXC_OK) return rc;

  size_t cap = expected_len;
  rc = loxc_generic_decode(&r, (char *)out, &cap, g_current_ctx);
  if (rc != LOXC_OK) return rc;
  *out_len = cap;
  return LOXC_OK;
}

static loxc_module_t *load_module_from_buffer(const uint8_t *buf,
                                              size_t file_size,
                                              const char *name) {
  if (buf == NULL || name == NULL) {
    fprintf(stderr, "loxc_module_load_from_memory: invalid argument\n");
    return NULL;
  }
  if (g_current_ctx != NULL) {
    fprintf(stderr,
            "loxc_module_load_from_file: another module already loaded, unload first\n");
    return NULL;
  }

  if (file_size < LOXC_TAB_HEADER_SIZE + LOXC_TAB_TRAILER_SIZE) {
    fprintf(stderr, "loxc_module_load_from_file: file too small (%zu bytes)\n",
            file_size);
    return NULL;
  }

  if (memcmp(buf, LOXC_TAB_MAGIC, 4) != 0) {
    fprintf(stderr, "loxc_module_load_from_file: invalid magic\n");
    return NULL;
  }

  if (buf[4] != (uint8_t)LOXC_TAB_VERSION) {
    fprintf(stderr,
            "loxc_module_load_from_file: unsupported version %u, expected %u\n",
            (unsigned)buf[4], (unsigned)LOXC_TAB_VERSION);
    return NULL;
  }

  const uint8_t strategy_id = buf[5];
  const uint8_t base_size = buf[6];
  const uint8_t bits_per_level = buf[7];
  const uint16_t level_count = read_u16_le(buf + 8);
  const uint32_t symbol_count = read_u32_le(buf + 12);
  const uint32_t dict_count = read_u32_le(buf + 16);
  const uint32_t data_size = read_u32_le(buf + 20);
  (void)strategy_id;
  (void)base_size;
  (void)bits_per_level;
  (void)level_count;
  (void)symbol_count;
  (void)dict_count;

  const size_t expected_size =
      (size_t)LOXC_TAB_HEADER_SIZE + (size_t)data_size +
      (size_t)LOXC_TAB_TRAILER_SIZE;
  if (file_size != expected_size) {
    fprintf(stderr,
            "loxc_module_load_from_file: size mismatch file=%zu expected=%zu data_size=%u\n",
            file_size, expected_size, (unsigned)data_size);
    return NULL;
  }

  const uint32_t trailer_crc = read_u32_le(buf + file_size - LOXC_TAB_TRAILER_SIZE);
  const uint32_t computed_crc =
      loxc_crc32(buf, (size_t)LOXC_TAB_HEADER_SIZE + (size_t)data_size);
  if (computed_crc != trailer_crc) {
    fprintf(stderr,
            "loxc_module_load_from_file: crc mismatch computed=0x%08x trailer=0x%08x\n",
            (unsigned)computed_crc, (unsigned)trailer_crc);
    return NULL;
  }

  const uint8_t *data = buf + LOXC_TAB_HEADER_SIZE;
  size_t pos = 0;
  const size_t data_size_sz = (size_t)data_size;

  if (data_size_sz < 1024u) {
    fprintf(stderr, "loxc_module_load_from_file: data section too small\n");
    return NULL;
  }
  loxc_loaded_module_ctx_t *ctx =
      (loxc_loaded_module_ctx_t *)calloc(1u, sizeof(loxc_loaded_module_ctx_t));
  if (ctx == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: calloc ctx failed\n");
    return NULL;
  }

  ctx->strategy_id = strategy_id;
  ctx->module_id = 200u;
  ctx->base_size = base_size;
  ctx->bits_per_level = bits_per_level;
  ctx->level_count = level_count;
  ctx->symbol_count = symbol_count;
  ctx->dict_count = dict_count;
  if (base_size == 4u) {
    ctx->direct_slots = 15u;
  } else if (base_size == 8u) {
    ctx->direct_slots = 56u;
  } else {
    ctx->direct_slots = 0u;
  }
  ctx->escape_pos = ctx->direct_slots;

  ctx->byte_to_symbol = (uint32_t *)malloc(256u * sizeof(uint32_t));
  ctx->symbols = (loxc_sym_t *)malloc((size_t)symbol_count * sizeof(loxc_sym_t));
  ctx->dict_offsets =
      (uint32_t *)malloc(((size_t)dict_count + 1u) * sizeof(uint32_t));
  if (ctx->byte_to_symbol == NULL || ctx->symbols == NULL ||
      ctx->dict_offsets == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: table allocation failed\n");
    free_loaded_ctx(ctx);
    return NULL;
  }

  if (pos + 1024u > data_size_sz) {
    fprintf(stderr, "loxc_module_load_from_file: truncated byte_to_symbol\n");
    free_loaded_ctx(ctx);
    return NULL;
  }
  for (size_t i = 0; i < 256u; i++) {
    ctx->byte_to_symbol[i] = read_u32_le(data + pos);
    pos += 4u;
  }

  const size_t symbols_bytes = (size_t)symbol_count * 5u;
  if (pos + symbols_bytes > data_size_sz) {
    fprintf(stderr, "loxc_module_load_from_file: truncated symbols table\n");
    free_loaded_ctx(ctx);
    return NULL;
  }
  for (uint32_t i = 0; i < symbol_count; i++) {
    ctx->symbols[i].type = data[pos++];
    ctx->symbols[i].byte_or_idx = read_u32_le(data + pos);
    pos += 4u;
  }

  const size_t offsets_bytes = ((size_t)dict_count + 1u) * 4u;
  if (pos + offsets_bytes > data_size_sz) {
    fprintf(stderr, "loxc_module_load_from_file: truncated dict offsets\n");
    free_loaded_ctx(ctx);
    return NULL;
  }
  for (uint32_t i = 0; i < dict_count + 1u; i++) {
    ctx->dict_offsets[i] = read_u32_le(data + pos);
    pos += 4u;
  }

  if (pos + 4u > data_size_sz) {
    fprintf(stderr, "loxc_module_load_from_file: missing dict_data_size\n");
    free_loaded_ctx(ctx);
    return NULL;
  }
  ctx->dict_data_size = read_u32_le(data + pos);
  pos += 4u;

  if (pos + (size_t)ctx->dict_data_size != data_size_sz) {
    fprintf(stderr,
            "loxc_module_load_from_file: dict data size mismatch pos=%zu dict_data_size=%u data_size=%u\n",
            pos, (unsigned)ctx->dict_data_size, (unsigned)data_size);
    free_loaded_ctx(ctx);
    return NULL;
  }
  if (dict_count > 0 && ctx->dict_offsets[dict_count] != ctx->dict_data_size) {
    fprintf(stderr,
            "loxc_module_load_from_file: dict offset sentinel mismatch offset=%u data_size=%u\n",
            (unsigned)ctx->dict_offsets[dict_count],
            (unsigned)ctx->dict_data_size);
    free_loaded_ctx(ctx);
    return NULL;
  }

  if (ctx->dict_data_size > 0) {
    ctx->dict_data = (uint8_t *)malloc(ctx->dict_data_size);
    if (ctx->dict_data == NULL) {
      fprintf(stderr, "loxc_module_load_from_file: dict data allocation failed\n");
      free_loaded_ctx(ctx);
      return NULL;
    }
    memcpy(ctx->dict_data, data + pos, ctx->dict_data_size);
  }

  ctx->raw_loxctab = (uint8_t *)malloc(file_size);
  if (ctx->raw_loxctab == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: raw loxctab allocation failed\n");
    free_loaded_ctx(ctx);
    return NULL;
  }
  memcpy(ctx->raw_loxctab, buf, file_size);
  ctx->raw_loxctab_size = file_size;

  loxc_module_t *module = (loxc_module_t *)calloc(1u, sizeof(loxc_module_t));
  if (module == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: calloc module failed\n");
    free_loaded_ctx(ctx);
    return NULL;
  }

  char *module_name = loxc_strdup_range(name, strlen(name));
  if (module_name == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: module name allocation failed\n");
    free(module);
    free_loaded_ctx(ctx);
    return NULL;
  }

  module->name = module_name;
  module->module_id = ctx->module_id;
  module->version = 2u;
  module->strategy_id = ctx->strategy_id;
  module->encode = loaded_encode_buffer;
  module->decode = loaded_decode_buffer;
  module->private_data = ctx;
  g_current_ctx = ctx;

  fprintf(stderr,
          "loxc_module_load_from_file: loaded module '%s', strategy=%d, symbols=%u, dict=%u, levels=%u\n",
          module->name, (int)ctx->strategy_id, (unsigned)ctx->symbol_count,
          (unsigned)ctx->dict_count, (unsigned)ctx->level_count);
  return module;
}

loxc_module_t *loxc_module_load_from_memory(const uint8_t *buf, size_t buf_size,
                                            const char *name) {
  return load_module_from_buffer(buf, buf_size, name);
}

loxc_module_t *loxc_module_load_from_file(const char *path) {
  if (path == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: path is NULL\n");
    return NULL;
  }

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: cannot open %s: %s\n",
            path, strerror(errno));
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fprintf(stderr, "loxc_module_load_from_file: cannot seek %s: %s\n",
            path, strerror(errno));
    fclose(f);
    return NULL;
  }

  long file_size_long = ftell(f);
  if (file_size_long < 0) {
    fprintf(stderr, "loxc_module_load_from_file: cannot tell %s: %s\n",
            path, strerror(errno));
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fprintf(stderr, "loxc_module_load_from_file: cannot rewind %s: %s\n",
            path, strerror(errno));
    fclose(f);
    return NULL;
  }

  const size_t file_size = (size_t)file_size_long;
  uint8_t *buf = NULL;
  if (file_size > 0) {
    buf = (uint8_t *)malloc(file_size);
    if (buf == NULL) {
      fprintf(stderr, "loxc_module_load_from_file: malloc failed for %zu bytes\n",
              file_size);
      fclose(f);
      return NULL;
    }
  }

  size_t nread = 0;
  if (file_size > 0) nread = fread(buf, 1, file_size, f);
  fclose(f);
  if (nread != file_size) {
    fprintf(stderr, "loxc_module_load_from_file: short read %s (%zu/%zu bytes)\n",
            path, nread, file_size);
    free(buf);
    return NULL;
  }

  char *name = module_name_from_path(path);
  if (name == NULL) {
    fprintf(stderr, "loxc_module_load_from_file: module name allocation failed\n");
    free(buf);
    return NULL;
  }
  loxc_module_t *module = load_module_from_buffer(buf, file_size, name);
  free(name);
  free(buf);
  return module;
}

void loxc_module_unload(loxc_module_t *module) {
  if (module == NULL) return;

  if (module->private_data == g_current_ctx) {
    g_current_ctx = NULL;
  }
  free_loaded_ctx((loxc_loaded_module_ctx_t *)module->private_data);
  free((void *)module->name);
  free(module);
}

int loxc_module_get_table_blob(const loxc_module_t *module,
                               const uint8_t **out_blob,
                               size_t *out_size) {
  if (out_blob == NULL || out_size == NULL) return LOXC_ERR_NULL;
  *out_blob = NULL;
  *out_size = 0;
  if (module == NULL) return LOXC_ERR_NULL;

  const loxc_loaded_module_ctx_t *ctx =
      (const loxc_loaded_module_ctx_t *)module->private_data;
  if (ctx == NULL || ctx->raw_loxctab == NULL || ctx->raw_loxctab_size == 0) {
    return LOXC_ERR_INVALID_FORMAT;
  }

  *out_blob = ctx->raw_loxctab;
  *out_size = ctx->raw_loxctab_size;
  return LOXC_OK;
}
