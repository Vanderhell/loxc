#ifndef LOXC_SIMPLE_H
#define LOXC_SIMPLE_H

#include <stddef.h>
#include <stdint.h>

#include "loxc.h"

typedef struct loxc_ctx loxc_ctx_t;

typedef void *(*loxc_simple_alloc_fn)(void *user_data, size_t size);
typedef void (*loxc_simple_free_fn)(void *user_data, void *ptr);

typedef struct {
  size_t max_table_size;
  size_t max_input_size;
  size_t max_output_size;
  size_t max_file_size;
  loxc_simple_alloc_fn alloc;
  loxc_simple_free_fn free;
  void *user_data;
} loxc_simple_config_t;

typedef struct {
  uint8_t *data;
  size_t size;
  int error;
  void *user_data;
  loxc_simple_free_fn free_fn;
} loxc_buffer_t;

/*
 * Threading contract:
 * - independent loxc_ctx_t instances may be used independently, but this is
 *   not verified under ThreadSanitizer in this repository
 * - one shared loxc_ctx_t is not guaranteed thread-safe
 * - runtime module close/unregister/unload must not race with active use
 */
loxc_ctx_t *loxc_open(const char *table_path);
int loxc_open_ex(const char *table_path, const loxc_simple_config_t *config,
                 loxc_ctx_t **out_ctx);
void loxc_close(loxc_ctx_t *ctx);

int loxc_compress_file(loxc_ctx_t *ctx, const char *input_path,
                       const char *output_path, int embed_table);
int loxc_decompress_file(loxc_ctx_t *ctx, const char *input_path,
                         const char *output_path);

loxc_buffer_t loxc_compress_buffer(loxc_ctx_t *ctx, const void *data, size_t len,
                                   int embed_table);
loxc_buffer_t loxc_decompress_buffer(loxc_ctx_t *ctx, const void *data,
                                     size_t len);
void loxc_buffer_free(loxc_buffer_t *buf);

const char *loxc_strerror(int error_code);
int loxc_check_file(const char *path);

#endif /* LOXC_SIMPLE_H */
