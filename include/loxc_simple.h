#ifndef LOXC_SIMPLE_H
#define LOXC_SIMPLE_H

#include <stddef.h>
#include <stdint.h>

#include "loxc.h"

typedef struct loxc_ctx loxc_ctx_t;

typedef struct {
  uint8_t *data;
  size_t size;
  int error;
} loxc_buffer_t;

loxc_ctx_t *loxc_open(const char *table_path);
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
