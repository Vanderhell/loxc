#include "loxc_simple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc_base.h"
#include "loxc_tab.h"

struct loxc_ctx {
  loxc_module_t *module;
  char *module_name;
};

static char *loxc_simple_strdup(const char *s) {
  size_t len = 0;
  char *out = NULL;

  if (s == NULL) return NULL;
  len = strlen(s);
  out = (char *)malloc(len + 1u);
  if (out == NULL) return NULL;
  memcpy(out, s, len + 1u);
  return out;
}

static int has_suffix(const char *path, const char *suffix) {
  size_t path_len = 0;
  size_t suffix_len = 0;

  if (path == NULL || suffix == NULL) return 0;
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (path_len < suffix_len) return 0;
  return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static int read_entire_file(const char *path, uint8_t **out_data,
                            size_t *out_size) {
  FILE *f = NULL;
  long file_size = 0;
  uint8_t *buf = NULL;
  size_t nread = 0;

  if (path == NULL || out_data == NULL || out_size == NULL) {
    return LOXC_ERR_NULL;
  }

  *out_data = NULL;
  *out_size = 0;

  f = fopen(path, "rb");
  if (f == NULL) return LOXC_ERR_INVALID_FORMAT;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return LOXC_ERR_INVALID_FORMAT;
  }
  file_size = ftell(f);
  if (file_size < 0) {
    fclose(f);
    return LOXC_ERR_INVALID_FORMAT;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return LOXC_ERR_INVALID_FORMAT;
  }

  if (file_size > 0) {
    buf = (uint8_t *)malloc((size_t)file_size);
    if (buf == NULL) {
      fclose(f);
      return LOXC_ERR_OVERFLOW;
    }
    nread = fread(buf, 1, (size_t)file_size, f);
    if (nread != (size_t)file_size) {
      free(buf);
      fclose(f);
      return LOXC_ERR_TRUNCATED;
    }
  }

  fclose(f);
  *out_data = buf;
  *out_size = (size_t)file_size;
  return LOXC_OK;
}

static int write_entire_file(const char *path, const uint8_t *data, size_t size) {
  FILE *f = NULL;
  size_t nwritten = 0;

  if (path == NULL || (data == NULL && size != 0)) return LOXC_ERR_NULL;

  f = fopen(path, "wb");
  if (f == NULL) return LOXC_ERR_INVALID_FORMAT;
  if (size > 0) nwritten = fwrite(data, 1, size, f);
  if (fclose(f) != 0) return LOXC_ERR_INVALID_FORMAT;
  if (nwritten != size) return LOXC_ERR_OVERFLOW;
  return LOXC_OK;
}

static int file_is_embedded(const uint8_t *data, size_t size, int *out_embedded) {
  loxc_reader_t r;
  loxc_header_t h;
  int rc = LOXC_OK;

  if (data == NULL || out_embedded == NULL) return LOXC_ERR_NULL;
  *out_embedded = 0;

  rc = loxc_reader_init(&r, data, size);
  if (rc != LOXC_OK) return rc;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) return rc;
  *out_embedded = ((h.flags & LOXC_FLAG_EMBEDDED_TABLE) != 0);
  return LOXC_OK;
}

static size_t estimate_decompressed_size(const uint8_t *data, size_t size) {
  loxc_reader_t r;
  loxc_header_t h;
  size_t guessed = size * 10u + 1024u;

  if (data == NULL || size == 0) return 1024u;

  if (loxc_reader_init(&r, data, size) == LOXC_OK &&
      loxc_header_read(&r, &h) == LOXC_OK) {
    uint32_t raw_len = (uint32_t)h.reserved[0] |
                       ((uint32_t)h.reserved[1] << 8u) |
                       ((uint32_t)h.reserved[2] << 16u) |
                       ((uint32_t)h.reserved[3] << 24u);
    if (raw_len != 0u) {
      guessed = (size_t)raw_len + 1u;
    }
  }

  if (guessed < 1024u) guessed = 1024u;
  return guessed;
}

loxc_ctx_t *loxc_open(const char *table_path) {
  loxc_module_t *module = NULL;
  loxc_ctx_t *ctx = NULL;

  if (table_path == NULL) return NULL;
  if (!has_suffix(table_path, ".loxctab")) return NULL;

  module = loxc_module_load_from_file(table_path);
  if (module == NULL) return NULL;

  if (loxc_module_register(module) != LOXC_OK) {
    loxc_module_unload(module);
    return NULL;
  }

  ctx = (loxc_ctx_t *)calloc(1u, sizeof(*ctx));
  if (ctx == NULL) {
    (void)loxc_module_unregister(module->name);
    loxc_module_unload(module);
    return NULL;
  }

  ctx->module_name = loxc_simple_strdup(module->name);
  if (ctx->module_name == NULL) {
    (void)loxc_module_unregister(module->name);
    loxc_module_unload(module);
    free(ctx);
    return NULL;
  }

  ctx->module = module;
  return ctx;
}

void loxc_close(loxc_ctx_t *ctx) {
  if (ctx == NULL) return;
  if (ctx->module_name != NULL) {
    (void)loxc_module_unregister(ctx->module_name);
  }
  loxc_module_unload(ctx->module);
  free(ctx->module_name);
  free(ctx);
}

loxc_buffer_t loxc_compress_buffer(loxc_ctx_t *ctx, const void *data, size_t len,
                                   int embed_table) {
  loxc_buffer_t out;
  size_t cap = 0;
  size_t actual = 0;
  int rc = LOXC_OK;
  int attempt = 0;

  out.data = NULL;
  out.size = 0;
  out.error = LOXC_OK;

  if (ctx == NULL || ctx->module_name == NULL || (data == NULL && len != 0)) {
    out.error = LOXC_ERR_NULL;
    return out;
  }

  cap = len * 2u + 1024u;
  if (cap < len) {
    out.error = LOXC_ERR_OVERFLOW;
    return out;
  }

  out.data = (uint8_t *)malloc(cap);
  if (out.data == NULL) {
    out.error = LOXC_ERR_OVERFLOW;
    return out;
  }

  for (attempt = 0; attempt < 8; attempt++) {
    size_t cap_arg = cap;
    actual = 0;
    rc = loxc_compress_with_options(ctx->module_name, (const char *)data, len,
                                    out.data, &cap_arg, &actual, embed_table);
    if (rc != LOXC_ERR_OVERFLOW) break;

    if (cap > (SIZE_MAX - 4096u) / 2u) {
      rc = LOXC_ERR_OVERFLOW;
      break;
    }
    cap = cap * 2u + 4096u;
    {
      uint8_t *grown = (uint8_t *)realloc(out.data, cap);
      if (grown == NULL) {
        rc = LOXC_ERR_OVERFLOW;
        break;
      }
      out.data = grown;
    }
  }

  if (rc != LOXC_OK) {
    free(out.data);
    out.data = NULL;
    out.error = rc;
    return out;
  }

  out.size = actual;
  return out;
}

loxc_buffer_t loxc_decompress_buffer(loxc_ctx_t *ctx, const void *data,
                                     size_t len) {
  loxc_buffer_t out;
  size_t cap = 0;
  size_t actual = 0;
  int rc = LOXC_OK;
  int embedded = 0;
  int attempt = 0;

  out.data = NULL;
  out.size = 0;
  out.error = LOXC_OK;

  if (data == NULL && len != 0) {
    out.error = LOXC_ERR_NULL;
    return out;
  }

  rc = file_is_embedded((const uint8_t *)data, len, &embedded);
  if (rc != LOXC_OK) {
    out.error = rc;
    return out;
  }

  if (!embedded && ctx == NULL) {
    out.error = LOXC_ERR_NULL;
    return out;
  }

  cap = estimate_decompressed_size((const uint8_t *)data, len);
  out.data = (uint8_t *)malloc(cap);
  if (out.data == NULL) {
    out.error = LOXC_ERR_OVERFLOW;
    return out;
  }

  for (attempt = 0; attempt < 10; attempt++) {
    size_t cap_arg = cap;
    actual = 0;
    rc = loxc_decompress((const uint8_t *)data, len, (char *)out.data, &cap_arg,
                         &actual);
    if (rc != LOXC_ERR_OVERFLOW) break;

    if (cap > (SIZE_MAX - 4096u) / 2u) {
      rc = LOXC_ERR_OVERFLOW;
      break;
    }
    cap = cap * 2u + 4096u;
    {
      uint8_t *grown = (uint8_t *)realloc(out.data, cap);
      if (grown == NULL) {
        rc = LOXC_ERR_OVERFLOW;
        break;
      }
      out.data = grown;
    }
  }

  if (rc != LOXC_OK) {
    free(out.data);
    out.data = NULL;
    out.error = rc;
    return out;
  }

  out.size = actual;
  return out;
}

int loxc_compress_file(loxc_ctx_t *ctx, const char *input_path,
                       const char *output_path, int embed_table) {
  uint8_t *input = NULL;
  uint8_t *output = NULL;
  size_t input_size = 0;
  size_t output_cap = 0;
  size_t output_size = 0;
  int rc = LOXC_OK;
  int attempt = 0;

  if (ctx == NULL || input_path == NULL || output_path == NULL) {
    return LOXC_ERR_NULL;
  }

  rc = read_entire_file(input_path, &input, &input_size);
  if (rc != LOXC_OK) return rc;

  output_cap = input_size * 2u + 1024u;
  if (output_cap < input_size) {
    free(input);
    return LOXC_ERR_OVERFLOW;
  }

  output = (uint8_t *)malloc(output_cap);
  if (output == NULL) {
    free(input);
    return LOXC_ERR_OVERFLOW;
  }

  for (attempt = 0; attempt < 8; attempt++) {
    size_t cap_arg = output_cap;
    output_size = 0;
    rc = loxc_compress_with_options(ctx->module_name, (const char *)input,
                                    input_size, output, &cap_arg,
                                    &output_size, embed_table);
    if (rc != LOXC_ERR_OVERFLOW) break;

    if (output_cap > (SIZE_MAX - 4096u) / 2u) {
      rc = LOXC_ERR_OVERFLOW;
      break;
    }
    output_cap = output_cap * 2u + 4096u;
    {
      uint8_t *grown = (uint8_t *)realloc(output, output_cap);
      if (grown == NULL) {
        rc = LOXC_ERR_OVERFLOW;
        break;
      }
      output = grown;
    }
  }

  if (rc == LOXC_OK) {
    rc = write_entire_file(output_path, output, output_size);
  }

  free(output);
  free(input);
  return rc;
}

int loxc_decompress_file(loxc_ctx_t *ctx, const char *input_path,
                         const char *output_path) {
  uint8_t *input = NULL;
  uint8_t *output = NULL;
  size_t input_size = 0;
  size_t output_cap = 0;
  size_t output_size = 0;
  int embedded = 0;
  int rc = LOXC_OK;
  int attempt = 0;

  if (input_path == NULL || output_path == NULL) return LOXC_ERR_NULL;

  rc = read_entire_file(input_path, &input, &input_size);
  if (rc != LOXC_OK) return rc;

  rc = file_is_embedded(input, input_size, &embedded);
  if (rc != LOXC_OK) {
    free(input);
    return rc;
  }

  if (!embedded && ctx == NULL) {
    free(input);
    return LOXC_ERR_NULL;
  }

  output_cap = estimate_decompressed_size(input, input_size);
  output = (uint8_t *)malloc(output_cap);
  if (output == NULL) {
    free(input);
    return LOXC_ERR_OVERFLOW;
  }

  for (attempt = 0; attempt < 10; attempt++) {
    size_t cap_arg = output_cap;
    output_size = 0;
    rc = loxc_decompress(input, input_size, (char *)output, &cap_arg, &output_size);
    if (rc != LOXC_ERR_OVERFLOW) break;

    if (output_cap > (SIZE_MAX - 4096u) / 2u) {
      rc = LOXC_ERR_OVERFLOW;
      break;
    }
    output_cap = output_cap * 2u + 4096u;
    {
      uint8_t *grown = (uint8_t *)realloc(output, output_cap);
      if (grown == NULL) {
        rc = LOXC_ERR_OVERFLOW;
        break;
      }
      output = grown;
    }
  }

  if (rc == LOXC_OK) {
    rc = write_entire_file(output_path, output, output_size);
  }

  free(output);
  free(input);
  return rc;
}

void loxc_buffer_free(loxc_buffer_t *buf) {
  if (buf == NULL) return;
  free(buf->data);
  buf->data = NULL;
  buf->size = 0;
  buf->error = LOXC_OK;
}

const char *loxc_strerror(int error_code) {
  switch (error_code) {
    case LOXC_OK: return "success";
    case LOXC_ERR_NULL: return "null pointer argument";
    case LOXC_ERR_INVALID_MAGIC: return "invalid file magic";
    case LOXC_ERR_TRUNCATED: return "truncated input";
    case LOXC_ERR_OVERFLOW: return "output buffer too small";
    case LOXC_ERR_SYMBOL_NOT_FOUND: return "symbol not in module dictionary";
    case LOXC_ERR_INVALID_FORMAT: return "invalid file format";
    case LOXC_ERR_MODULE_NOT_FOUND: return "module not registered";
    case LOXC_ERR_REGISTRY_FULL: return "module registry full";
    case LOXC_ERR_DUPLICATE_MODULE: return "module already registered";
    case LOXC_ERR_INVALID_MODULE: return "invalid module structure";
    default: return "unknown error";
  }
}

int loxc_check_file(const char *path) {
  uint8_t header[19];
  FILE *f = NULL;
  size_t nread = 0;
  loxc_reader_t r;
  loxc_header_t h;
  int rc = LOXC_OK;

  if (path == NULL) return LOXC_ERR_NULL;

  f = fopen(path, "rb");
  if (f == NULL) return LOXC_ERR_INVALID_FORMAT;
  nread = fread(header, 1, sizeof(header), f);
  fclose(f);

  if (nread < 15u) return LOXC_ERR_TRUNCATED;
  rc = loxc_reader_init(&r, header, nread);
  if (rc != LOXC_OK) return rc;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) return rc;
  return LOXC_OK;
}
