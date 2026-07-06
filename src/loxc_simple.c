#include "loxc_simple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "loxc_base.h"
#include "loxc_tab.h"

struct loxc_ctx {
  loxc_module_t *module;
  char *module_name;
  int owns_module;
  size_t max_table_size;
  size_t max_input_size;
  size_t max_output_size;
  size_t max_file_size;
  loxc_simple_alloc_fn alloc_fn;
  loxc_simple_free_fn free_fn;
  void *user_data;
};

enum {
  LOXC_SIMPLE_DEFAULT_MAX_TABLE_SIZE = 16u * 1024u * 1024u,
  LOXC_SIMPLE_DEFAULT_MAX_INPUT_SIZE = 16u * 1024u * 1024u,
  LOXC_SIMPLE_DEFAULT_MAX_OUTPUT_SIZE = 32u * 1024u * 1024u,
  LOXC_SIMPLE_DEFAULT_MAX_FILE_SIZE = 16u * 1024u * 1024u
};

static void *loxc_simple_default_alloc(void *user_data, size_t size) {
  (void)user_data;
  return malloc(size);
}

static void loxc_simple_default_free(void *user_data, void *ptr) {
  (void)user_data;
  free(ptr);
}

static int loxc_simple_checked_add_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a > SIZE_MAX - b) return LOXC_ERR_OVERFLOW;
  *out = a + b;
  return LOXC_OK;
}

static int loxc_simple_checked_mul_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a != 0u && b > SIZE_MAX / a) return LOXC_ERR_OVERFLOW;
  *out = a * b;
  return LOXC_OK;
}

static const loxc_simple_config_t *loxc_simple_resolve_config(
    const loxc_simple_config_t *config, loxc_simple_config_t *scratch) {
  if (scratch == NULL) return NULL;
  if (config != NULL) {
    *scratch = *config;
  } else {
    memset(scratch, 0, sizeof(*scratch));
  }

  if (scratch->max_table_size == 0u) scratch->max_table_size = LOXC_SIMPLE_DEFAULT_MAX_TABLE_SIZE;
  if (scratch->max_input_size == 0u) scratch->max_input_size = LOXC_SIMPLE_DEFAULT_MAX_INPUT_SIZE;
  if (scratch->max_output_size == 0u) scratch->max_output_size = LOXC_SIMPLE_DEFAULT_MAX_OUTPUT_SIZE;
  if (scratch->max_file_size == 0u) scratch->max_file_size = LOXC_SIMPLE_DEFAULT_MAX_FILE_SIZE;
  if (scratch->alloc == NULL) scratch->alloc = loxc_simple_default_alloc;
  if (scratch->free == NULL) scratch->free = loxc_simple_default_free;
  return scratch;
}

static size_t loxc_simple_min_size(size_t a, size_t b) {
  return (a < b) ? a : b;
}

static void *loxc_simple_ctx_alloc(const loxc_ctx_t *ctx, size_t size) {
  if (ctx == NULL || ctx->alloc_fn == NULL) return NULL;
  return ctx->alloc_fn(ctx->user_data, size);
}

static void loxc_simple_ctx_free(const loxc_ctx_t *ctx, void *ptr) {
  if (ptr == NULL) return;
  if (ctx != NULL && ctx->free_fn != NULL) {
    ctx->free_fn(ctx->user_data, ptr);
  } else {
    free(ptr);
  }
}

static char *loxc_simple_strdup_range_alloc(loxc_simple_alloc_fn alloc_fn,
                                            void *user_data,
                                            const char *start,
                                            size_t len) {
  char *out = NULL;

  if (alloc_fn == NULL || start == NULL) return NULL;
  out = (char *)alloc_fn(user_data, len + 1u);
  if (out == NULL) return NULL;
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

static int has_suffix(const char *path, const char *suffix) {
  size_t path_len = 0u;
  size_t suffix_len = 0u;

  if (path == NULL || suffix == NULL) return 0;
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (path_len < suffix_len) return 0;
  return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static char *module_name_from_path_simple(loxc_simple_alloc_fn alloc_fn,
                                          void *user_data,
                                          const char *path) {
  const char *base = NULL;
  const char *base_win = NULL;
  const char *start = NULL;
  size_t len = 0u;
  const char suffix[] = ".loxctab";
  const size_t suffix_len = sizeof(suffix) - 1u;

  if (path == NULL) return NULL;
  base = strrchr(path, '/');
  base_win = strrchr(path, '\\');
  if (base_win != NULL && (base == NULL || base_win > base)) base = base_win;
  start = (base == NULL) ? path : base + 1u;
  if (strncmp(start, "loxc_", 5u) == 0) start += 5u;

  len = strlen(start);
  if (len >= suffix_len && strcmp(start + len - suffix_len, suffix) == 0) {
    len -= suffix_len;
  }

  return loxc_simple_strdup_range_alloc(alloc_fn, user_data, start, len);
}

static int read_entire_file_limited(const char *path, size_t max_size,
                                    loxc_simple_alloc_fn alloc_fn,
                                    loxc_simple_free_fn free_fn,
                                    void *user_data,
                                    uint8_t **out_data,
                                    size_t *out_size) {
  FILE *f = NULL;
  long file_size = 0;
  uint8_t *buf = NULL;
  size_t nread = 0u;

  if (path == NULL || out_data == NULL || out_size == NULL ||
      alloc_fn == NULL || free_fn == NULL) {
    return LOXC_ERR_NULL;
  }

  *out_data = NULL;
  *out_size = 0u;

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
  if ((size_t)file_size > max_size) {
    fclose(f);
    return LOXC_ERR_OVERFLOW;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return LOXC_ERR_INVALID_FORMAT;
  }

  if (file_size > 0) {
    buf = (uint8_t *)alloc_fn(user_data, (size_t)file_size);
    if (buf == NULL) {
      fclose(f);
      return LOXC_ERR_ALLOC;
    }
    nread = fread(buf, 1, (size_t)file_size, f);
    if (nread != (size_t)file_size) {
      free_fn(user_data, buf);
      fclose(f);
      return LOXC_ERR_TRUNCATED;
    }
  }

  fclose(f);
  *out_data = buf;
  *out_size = (size_t)file_size;
  return LOXC_OK;
}

static int write_entire_file_atomic(const char *path, const uint8_t *data,
                                    size_t size) {
  FILE *f = NULL;
  char *tmp_path = NULL;
  size_t path_len = 0u;
  size_t tmp_len = 0u;
  unsigned long stamp = 0ul;
  size_t nwritten = 0u;

  if (path == NULL || (data == NULL && size != 0u)) return LOXC_ERR_NULL;

  path_len = strlen(path);
  if (loxc_simple_checked_add_size(path_len, 32u, &tmp_len) != LOXC_OK) {
    return LOXC_ERR_OVERFLOW;
  }
  tmp_path = (char *)malloc(tmp_len);
  if (tmp_path == NULL) return LOXC_ERR_ALLOC;

  stamp = (unsigned long)time(NULL) ^ (unsigned long)clock();
  if (snprintf(tmp_path, tmp_len, "%s.tmp.%lu", path, stamp) < 0) {
    free(tmp_path);
    return LOXC_ERR_INVALID_FORMAT;
  }

  f = fopen(tmp_path, "wb");
  if (f == NULL) {
    free(tmp_path);
    return LOXC_ERR_INVALID_FORMAT;
  }
  if (size > 0u) {
    nwritten = fwrite(data, 1, size, f);
  }
  if (nwritten != size || fflush(f) != 0 || fclose(f) != 0) {
    (void)remove(tmp_path);
    free(tmp_path);
    return (nwritten != size) ? LOXC_ERR_OVERFLOW : LOXC_ERR_INVALID_FORMAT;
  }

  if (rename(tmp_path, path) != 0) {
    (void)remove(tmp_path);
    free(tmp_path);
    return LOXC_ERR_INVALID_FORMAT;
  }

  free(tmp_path);
  return LOXC_OK;
}

static int file_is_embedded(const uint8_t *data, size_t size, int *out_embedded) {
  loxc_reader_t r;
  loxc_header_t h;
  int rc = LOXC_OK;

  if (out_embedded == NULL) return LOXC_ERR_NULL;
  *out_embedded = 0;
  if (size == 0u) return LOXC_ERR_TRUNCATED;
  if (data == NULL) return LOXC_ERR_NULL;

  rc = loxc_reader_init(&r, data, size);
  if (rc != LOXC_OK) return rc;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) return rc;
  *out_embedded = ((h.flags & LOXC_FLAG_EMBEDDED_TABLE) != 0);
  return LOXC_OK;
}

static int estimate_decompressed_cap(size_t input_size, size_t max_output_size,
                                     size_t *out_cap) {
  size_t cap = 0u;

  if (out_cap == NULL) return LOXC_ERR_NULL;
  if (loxc_simple_checked_mul_size(input_size, 2u, &cap) != LOXC_OK ||
      loxc_simple_checked_add_size(cap, 1024u, &cap) != LOXC_OK) {
    return LOXC_ERR_OVERFLOW;
  }
  cap = loxc_simple_min_size(cap, max_output_size);
  if (cap == 0u) cap = 1024u;
  *out_cap = cap;
  return LOXC_OK;
}

static int grow_buffer(const loxc_ctx_t *ctx, uint8_t **buf, size_t old_cap,
                       size_t *cap, size_t max_cap) {
  uint8_t *grown = NULL;
  size_t next_cap = 0u;

  if (ctx == NULL || buf == NULL || *buf == NULL || cap == NULL) {
    return LOXC_ERR_NULL;
  }
  if (old_cap > (SIZE_MAX - 4096u) / 2u) return LOXC_ERR_OVERFLOW;
  next_cap = old_cap * 2u + 4096u;
  next_cap = loxc_simple_min_size(next_cap, max_cap);
  if (next_cap <= old_cap) return LOXC_ERR_OVERFLOW;

  grown = (uint8_t *)loxc_simple_ctx_alloc(ctx, next_cap);
  if (grown == NULL) return LOXC_ERR_ALLOC;
  memcpy(grown, *buf, old_cap);
  loxc_simple_ctx_free(ctx, *buf);
  *buf = grown;
  *cap = next_cap;
  return LOXC_OK;
}

static int compress_buffer_internal(loxc_ctx_t *ctx, const char *module_name,
                                    const void *data, size_t len, int embed_table,
                                    loxc_buffer_t *out) {
  uint8_t *buf = NULL;
  size_t cap = 0u;
  size_t actual = 0u;
  int rc = LOXC_OK;
  int attempt = 0;

  if (out == NULL) return LOXC_ERR_NULL;
  out->data = NULL;
  out->size = 0u;
  out->error = LOXC_OK;
  out->user_data = NULL;
  out->free_fn = NULL;

  if (ctx == NULL || module_name == NULL) return LOXC_ERR_NULL;
  if (data == NULL && len != 0u) return LOXC_ERR_NULL;
  if (len > ctx->max_input_size) return LOXC_ERR_OVERFLOW;

  if (loxc_simple_checked_mul_size(len, 2u, &cap) != LOXC_OK ||
      loxc_simple_checked_add_size(cap, 1024u, &cap) != LOXC_OK) {
    return LOXC_ERR_OVERFLOW;
  }
  cap = loxc_simple_min_size(cap, ctx->max_output_size);
  if (cap == 0u) cap = 1024u;

  buf = (uint8_t *)loxc_simple_ctx_alloc(ctx, cap);
  if (buf == NULL) return LOXC_ERR_ALLOC;

  for (attempt = 0; attempt < 8; attempt++) {
    size_t cap_arg = cap;
    actual = 0u;
    rc = loxc_compress_with_options(module_name, (const char *)data, len, buf,
                                    &cap_arg, &actual, embed_table);
    if (rc != LOXC_ERR_OVERFLOW) break;
    if (cap >= ctx->max_output_size) {
      break;
    }
    rc = grow_buffer(ctx, &buf, cap, &cap, ctx->max_output_size);
    if (rc != LOXC_OK) break;
  }

  if (rc != LOXC_OK) {
    out->error = rc;
    out->size = actual;
    loxc_simple_ctx_free(ctx, buf);
    return rc;
  }

  out->data = buf;
  out->size = actual;
  out->error = LOXC_OK;
  out->user_data = ctx->user_data;
  out->free_fn = ctx->free_fn;
  return LOXC_OK;
}

static int decompress_buffer_internal(loxc_ctx_t *ctx, const void *data,
                                      size_t len, loxc_buffer_t *out) {
  uint8_t *buf = NULL;
  size_t cap = 0u;
  size_t actual = 0u;
  int rc = LOXC_OK;
  int embedded = 0;
  int attempt = 0;
  loxc_simple_alloc_fn alloc_fn = NULL;
  loxc_simple_free_fn free_fn = NULL;
  void *user_data = NULL;
  loxc_ctx_t fallback_ctx;

  if (out == NULL) return LOXC_ERR_NULL;
  out->data = NULL;
  out->size = 0u;
  out->error = LOXC_OK;
  out->user_data = NULL;
  out->free_fn = NULL;

  if (data == NULL && len != 0u) return LOXC_ERR_NULL;
  if (len == 0u) return LOXC_ERR_TRUNCATED;

  rc = file_is_embedded((const uint8_t *)data, len, &embedded);
  if (rc != LOXC_OK) {
    out->error = rc;
    return rc;
  }
  if (!embedded && ctx == NULL) return LOXC_ERR_NULL;

  if (ctx != NULL) {
    alloc_fn = ctx->alloc_fn;
    free_fn = ctx->free_fn;
    user_data = ctx->user_data;
    if (len > ctx->max_input_size) return LOXC_ERR_OVERFLOW;
    rc = estimate_decompressed_cap(len, ctx->max_output_size, &cap);
  } else {
    alloc_fn = loxc_simple_default_alloc;
    free_fn = loxc_simple_default_free;
    user_data = NULL;
    memset(&fallback_ctx, 0, sizeof(fallback_ctx));
    fallback_ctx.alloc_fn = alloc_fn;
    fallback_ctx.free_fn = free_fn;
    fallback_ctx.user_data = user_data;
    fallback_ctx.max_output_size = LOXC_SIMPLE_DEFAULT_MAX_OUTPUT_SIZE;
    rc = estimate_decompressed_cap(len, fallback_ctx.max_output_size, &cap);
    ctx = &fallback_ctx;
  }
  if (rc != LOXC_OK) return rc;

  buf = (uint8_t *)alloc_fn(user_data, cap);
  if (buf == NULL) return LOXC_ERR_ALLOC;

  for (attempt = 0; attempt < 10; attempt++) {
    size_t cap_arg = cap;
    actual = 0u;
    rc = loxc_decompress((const uint8_t *)data, len, (char *)buf, &cap_arg,
                         &actual);
    if (rc != LOXC_ERR_OVERFLOW) break;
    if (cap >= ctx->max_output_size) {
      break;
    }
    rc = grow_buffer(ctx, &buf, cap, &cap, ctx->max_output_size);
    if (rc != LOXC_OK) break;
  }

  if (rc != LOXC_OK) {
    out->error = rc;
    out->size = actual;
    if (free_fn != NULL) {
      free_fn(user_data, buf);
    } else {
      free(buf);
    }
    return rc;
  }

  out->data = buf;
  out->size = actual;
  out->error = LOXC_OK;
  out->user_data = user_data;
  out->free_fn = free_fn;
  return LOXC_OK;
}

static int file_api_encode(loxc_ctx_t *ctx, const char *input_path,
                           const char *output_path, int embed_table) {
  uint8_t *input = NULL;
  uint8_t *output = NULL;
  size_t input_size = 0u;
  size_t output_cap = 0u;
  size_t output_size = 0u;
  int rc = LOXC_OK;
  int attempt = 0;
  size_t file_limit = 0u;

  if (ctx == NULL || input_path == NULL || output_path == NULL) {
    return LOXC_ERR_NULL;
  }

  file_limit = loxc_simple_min_size(ctx->max_file_size, ctx->max_input_size);
  rc = read_entire_file_limited(input_path, file_limit, ctx->alloc_fn,
                                ctx->free_fn, ctx->user_data, &input,
                                &input_size);
  if (rc != LOXC_OK) return rc;

  if (loxc_simple_checked_mul_size(input_size, 2u, &output_cap) != LOXC_OK ||
      loxc_simple_checked_add_size(output_cap, 1024u, &output_cap) != LOXC_OK) {
    loxc_simple_ctx_free(ctx, input);
    return LOXC_ERR_OVERFLOW;
  }
  output_cap = loxc_simple_min_size(output_cap, ctx->max_output_size);
  if (output_cap == 0u) output_cap = 1024u;

  output = (uint8_t *)loxc_simple_ctx_alloc(ctx, output_cap);
  if (output == NULL) {
    loxc_simple_ctx_free(ctx, input);
    return LOXC_ERR_ALLOC;
  }

  for (attempt = 0; attempt < 8; attempt++) {
    size_t cap_arg = output_cap;
    output_size = 0u;
    rc = loxc_compress_with_options(ctx->module_name, (const char *)input,
                                    input_size, output, &cap_arg,
                                    &output_size, embed_table);
    if (rc != LOXC_ERR_OVERFLOW) break;
    if (output_cap >= ctx->max_output_size) {
      break;
    }
    rc = grow_buffer(ctx, &output, output_cap, &output_cap,
                     ctx->max_output_size);
    if (rc != LOXC_OK) break;
  }

  if (rc == LOXC_OK) {
    rc = write_entire_file_atomic(output_path, output, output_size);
  }

  loxc_simple_ctx_free(ctx, output);
  loxc_simple_ctx_free(ctx, input);
  return rc;
}

static int file_api_decode(loxc_ctx_t *ctx, const char *input_path,
                           const char *output_path) {
  uint8_t *input = NULL;
  uint8_t *output = NULL;
  size_t input_size = 0u;
  size_t output_cap = 0u;
  size_t output_size = 0u;
  size_t file_limit = 0u;
  int embedded = 0;
  int rc = LOXC_OK;
  int attempt = 0;
  loxc_simple_alloc_fn alloc_fn = NULL;
  loxc_simple_free_fn free_fn = NULL;
  void *user_data = NULL;
  loxc_ctx_t fallback_ctx;
  int had_ctx = 0;

  if (input_path == NULL || output_path == NULL) {
    return LOXC_ERR_NULL;
  }

  had_ctx = (ctx != NULL);
  if (ctx != NULL) {
    alloc_fn = ctx->alloc_fn;
    free_fn = ctx->free_fn;
    user_data = ctx->user_data;
    file_limit = loxc_simple_min_size(ctx->max_file_size, ctx->max_input_size);
  } else {
    alloc_fn = loxc_simple_default_alloc;
    free_fn = loxc_simple_default_free;
    user_data = NULL;
    memset(&fallback_ctx, 0, sizeof(fallback_ctx));
    fallback_ctx.alloc_fn = alloc_fn;
    fallback_ctx.free_fn = free_fn;
    fallback_ctx.user_data = user_data;
    fallback_ctx.max_file_size = LOXC_SIMPLE_DEFAULT_MAX_FILE_SIZE;
    fallback_ctx.max_input_size = LOXC_SIMPLE_DEFAULT_MAX_INPUT_SIZE;
    fallback_ctx.max_output_size = LOXC_SIMPLE_DEFAULT_MAX_OUTPUT_SIZE;
    file_limit = loxc_simple_min_size(fallback_ctx.max_file_size,
                                      fallback_ctx.max_input_size);
    ctx = &fallback_ctx;
  }

  rc = read_entire_file_limited(input_path, file_limit, alloc_fn, free_fn,
                                user_data, &input, &input_size);
  if (rc != LOXC_OK) return rc;

  rc = file_is_embedded(input, input_size, &embedded);
  if (rc != LOXC_OK) {
    if (free_fn != NULL) {
      free_fn(user_data, input);
    } else {
      free(input);
    }
    return rc;
  }
  if (!embedded && !had_ctx) {
    if (free_fn != NULL) {
      free_fn(user_data, input);
    } else {
      free(input);
    }
    return LOXC_ERR_NULL;
  }

  rc = estimate_decompressed_cap(input_size, ctx->max_output_size, &output_cap);
  if (rc != LOXC_OK) {
    if (free_fn != NULL) {
      free_fn(user_data, input);
    } else {
      free(input);
    }
    return rc;
  }

  output = (uint8_t *)alloc_fn(user_data, output_cap);
  if (output == NULL) {
    if (free_fn != NULL) {
      free_fn(user_data, input);
    } else {
      free(input);
    }
    return LOXC_ERR_ALLOC;
  }

  for (attempt = 0; attempt < 10; attempt++) {
    size_t cap_arg = output_cap;
    output_size = 0u;
    rc = loxc_decompress(input, input_size, (char *)output, &cap_arg,
                         &output_size);
    if (rc != LOXC_ERR_OVERFLOW) break;
    if (output_cap >= ctx->max_output_size) {
      break;
    }
    rc = grow_buffer(ctx, &output, output_cap, &output_cap,
                     ctx->max_output_size);
    if (rc != LOXC_OK) break;
  }

  if (rc == LOXC_OK) {
    rc = write_entire_file_atomic(output_path, output, output_size);
  }

  if (free_fn != NULL) {
    free_fn(user_data, output);
    free_fn(user_data, input);
  } else {
    free(output);
    free(input);
  }
  return rc;
}

int loxc_open_ex(const char *table_path, const loxc_simple_config_t *config,
                 loxc_ctx_t **out_ctx) {
  loxc_simple_config_t cfg;
  uint8_t *blob = NULL;
  size_t blob_size = 0u;
  size_t table_limit = 0u;
  loxc_module_t *module = NULL;
  loxc_ctx_t *ctx = NULL;
  char *module_name = NULL;
  int rc = LOXC_OK;

  if (out_ctx == NULL) return LOXC_ERR_NULL;
  *out_ctx = NULL;
  if (table_path == NULL) return LOXC_ERR_NULL;
  if (loxc_simple_resolve_config(config, &cfg) == NULL) return LOXC_ERR_INVALID_FORMAT;
  if (!has_suffix(table_path, ".loxctab")) return LOXC_ERR_INVALID_FORMAT;

  table_limit = loxc_simple_min_size(cfg.max_table_size, cfg.max_file_size);
  rc = read_entire_file_limited(table_path, table_limit, cfg.alloc, cfg.free,
                                cfg.user_data, &blob, &blob_size);
  if (rc != LOXC_OK) return rc;

  module_name = module_name_from_path_simple(cfg.alloc, cfg.user_data, table_path);
  if (module_name == NULL) {
    cfg.free(cfg.user_data, blob);
    return LOXC_ERR_ALLOC;
  }

  rc = loxc_module_load_from_memory_ex(blob, blob_size, module_name, &module,
                                       NULL);
  cfg.free(cfg.user_data, blob);
  if (rc != LOXC_OK) {
    cfg.free(cfg.user_data, module_name);
    return rc;
  }

  ctx = (loxc_ctx_t *)cfg.alloc(cfg.user_data, sizeof(*ctx));
  if (ctx == NULL) {
    (void)loxc_module_unload(module);
    cfg.free(cfg.user_data, module_name);
    return LOXC_ERR_ALLOC;
  }
  memset(ctx, 0, sizeof(*ctx));
  ctx->alloc_fn = cfg.alloc;
  ctx->free_fn = cfg.free;
  ctx->user_data = cfg.user_data;
  ctx->max_table_size = cfg.max_table_size;
  ctx->max_input_size = cfg.max_input_size;
  ctx->max_output_size = cfg.max_output_size;
  ctx->max_file_size = cfg.max_file_size;
  ctx->module_name = module_name;

  rc = loxc_module_register(module);
  if (rc == LOXC_OK) {
    ctx->module = module;
    ctx->owns_module = 1;
  } else if (rc == LOXC_ERR_DUPLICATE_MODULE) {
    ctx->module = NULL;
    ctx->owns_module = 0;
    (void)loxc_module_unload(module);
  } else {
    cfg.free(cfg.user_data, module_name);
    cfg.free(cfg.user_data, ctx);
    (void)loxc_module_unload(module);
    return rc;
  }

  *out_ctx = ctx;
  return LOXC_OK;
}

loxc_ctx_t *loxc_open(const char *table_path) {
  loxc_ctx_t *ctx = NULL;

  if (loxc_open_ex(table_path, NULL, &ctx) != LOXC_OK) return NULL;
  return ctx;
}

void loxc_close(loxc_ctx_t *ctx) {
  if (ctx == NULL) return;
  if (ctx->owns_module && ctx->module_name != NULL) {
    (void)loxc_module_unregister(ctx->module_name);
  }
  if (ctx->owns_module && ctx->module != NULL) {
    (void)loxc_module_unload(ctx->module);
  }
  loxc_simple_ctx_free(ctx, ctx->module_name);
  loxc_simple_ctx_free(ctx, ctx);
}

loxc_buffer_t loxc_compress_buffer(loxc_ctx_t *ctx, const void *data, size_t len,
                                   int embed_table) {
  loxc_buffer_t out;
  int rc = LOXC_OK;

  out.data = NULL;
  out.size = 0u;
  out.error = LOXC_OK;
  out.user_data = NULL;
  out.free_fn = NULL;

  if (ctx == NULL || ctx->module_name == NULL) {
    out.error = LOXC_ERR_NULL;
    return out;
  }
  rc = compress_buffer_internal(ctx, ctx->module_name, data, len, embed_table,
                                &out);
  out.error = rc;
  return out;
}

loxc_buffer_t loxc_decompress_buffer(loxc_ctx_t *ctx, const void *data,
                                     size_t len) {
  loxc_buffer_t out;
  int rc = LOXC_OK;
  int embedded = 0;

  out.data = NULL;
  out.size = 0u;
  out.error = LOXC_OK;
  out.user_data = NULL;
  out.free_fn = NULL;

  if (data == NULL && len != 0u) {
    out.error = LOXC_ERR_NULL;
    return out;
  }
  if (len == 0u) {
    out.error = LOXC_ERR_TRUNCATED;
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

  rc = decompress_buffer_internal(ctx, data, len, &out);
  out.error = rc;
  return out;
}

int loxc_compress_file(loxc_ctx_t *ctx, const char *input_path,
                       const char *output_path, int embed_table) {
  return file_api_encode(ctx, input_path, output_path, embed_table);
}

int loxc_decompress_file(loxc_ctx_t *ctx, const char *input_path,
                         const char *output_path) {
  return file_api_decode(ctx, input_path, output_path);
}

void loxc_buffer_free(loxc_buffer_t *buf) {
  if (buf == NULL) return;
  if (buf->data != NULL) {
    if (buf->free_fn != NULL) {
      buf->free_fn(buf->user_data, buf->data);
    } else {
      free(buf->data);
    }
  }
  buf->data = NULL;
  buf->size = 0u;
  buf->error = LOXC_OK;
  buf->user_data = NULL;
  buf->free_fn = NULL;
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
    case LOXC_ERR_BUSY: return "module operation in progress";
    case LOXC_ERR_ALLOC: return "allocation failed";
    default: return "unknown error";
  }
}

int loxc_check_file(const char *path) {
  uint8_t header[19];
  FILE *f = NULL;
  size_t nread = 0u;
  loxc_reader_t r;
  loxc_header_t h;
  int rc = LOXC_OK;

  if (path == NULL) return LOXC_ERR_NULL;

  f = fopen(path, "rb");
  if (f == NULL) return LOXC_ERR_INVALID_FORMAT;
  nread = fread(header, 1, sizeof(header), f);
  fclose(f);

  if (nread < LOXC_HEADER_SIZE_V2) return LOXC_ERR_TRUNCATED;
  rc = loxc_reader_init(&r, header, nread);
  if (rc != LOXC_OK) return rc;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) return rc;
  return LOXC_OK;
}
