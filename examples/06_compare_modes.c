/*
 * Compare external vs embedded mode size trade-offs.
 */

#include <stdio.h>
#include <string.h>

#include "loxc_simple.h"

int main(void) {
  const char *text = "The quick brown fox jumps over the lazy dog";
  loxc_ctx_t *ctx = NULL;
  loxc_buffer_t external;
  loxc_buffer_t embedded;

  ctx = loxc_open("modules/loxc_demo.loxctab");
  if (ctx == NULL) {
    fprintf(stderr, "Failed to load demo table\n");
    return 1;
  }

  external = loxc_compress_buffer(ctx, text, strlen(text), 0);
  embedded = loxc_compress_buffer(ctx, text, strlen(text), 1);
  if (external.error != LOXC_OK || embedded.error != LOXC_OK) {
    fprintf(stderr, "Compression failed\n");
    loxc_buffer_free(&external);
    loxc_buffer_free(&embedded);
    loxc_close(ctx);
    return 1;
  }

  printf("Input:             %4zu bytes\n", strlen(text));
  printf("External .loxc:    %4zu bytes (needs separate table)\n", external.size);
  printf("Embedded .loxc:    %4zu bytes (self-contained)\n", embedded.size);
  printf("Embedded overhead: %4zu bytes\n", embedded.size - external.size);

  loxc_buffer_free(&external);
  loxc_buffer_free(&embedded);
  loxc_close(ctx);
  return 0;
}
