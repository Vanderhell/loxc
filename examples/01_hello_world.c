/*
 * examples/01_hello_world.c
 *
 * The simplest possible loxc usage.
 * Compress a string, decompress it, verify match.
 *
 * Build: make examples
 * Run:   ./examples/01_hello_world
 */

#include <stdio.h>
#include <string.h>

#include "loxc_simple.h"

int main(void) {
  const char *original = "Hello, world!";
  loxc_ctx_t *ctx = NULL;
  loxc_buffer_t compressed;
  loxc_buffer_t restored;

  ctx = loxc_open("modules/loxc_demo.loxctab");
  if (ctx == NULL) {
    fprintf(stderr, "Failed to load module\n");
    return 1;
  }

  compressed = loxc_compress_buffer(ctx, original, strlen(original), 0);
  if (compressed.error != LOXC_OK) {
    fprintf(stderr, "Compress: %s\n", loxc_strerror(compressed.error));
    loxc_close(ctx);
    return 1;
  }

  printf("Compressed %zu bytes -> %zu bytes\n", strlen(original),
         compressed.size);

  restored = loxc_decompress_buffer(ctx, compressed.data, compressed.size);
  if (restored.error != LOXC_OK) {
    fprintf(stderr, "Decompress: %s\n", loxc_strerror(restored.error));
    loxc_buffer_free(&compressed);
    loxc_close(ctx);
    return 1;
  }

  if (restored.size == strlen(original) &&
      memcmp(original, restored.data, restored.size) == 0) {
    printf("Round-trip OK: \"%.*s\"\n", (int)restored.size,
           (const char *)restored.data);
  } else {
    fprintf(stderr, "Round-trip mismatch\n");
    loxc_buffer_free(&compressed);
    loxc_buffer_free(&restored);
    loxc_close(ctx);
    return 1;
  }

  loxc_buffer_free(&compressed);
  loxc_buffer_free(&restored);
  loxc_close(ctx);
  return 0;
}
