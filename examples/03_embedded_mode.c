/*
 * Self-contained .loxc file with an embedded table.
 */

#include <stdio.h>
#include <string.h>

#include "loxc_simple.h"

int main(void) {
  const char *text = "This text will travel without a separate table file.";
  const char *path = "/tmp/self_contained.loxc";
  loxc_ctx_t *ctx = NULL;
  loxc_buffer_t embedded;
  loxc_buffer_t restored;
  FILE *f = NULL;

  ctx = loxc_open("modules/loxc_demo.loxctab");
  if (ctx == NULL) {
    fprintf(stderr, "Failed to load demo table\n");
    return 1;
  }

  embedded = loxc_compress_buffer(ctx, text, strlen(text), 1);
  if (embedded.error != LOXC_OK) {
    fprintf(stderr, "Compress: %s\n", loxc_strerror(embedded.error));
    loxc_close(ctx);
    return 1;
  }

  f = fopen(path, "wb");
  if (f == NULL) {
    fprintf(stderr, "Failed to create %s\n", path);
    loxc_buffer_free(&embedded);
    loxc_close(ctx);
    return 1;
  }
  if (fwrite(embedded.data, 1, embedded.size, f) != embedded.size) {
    fprintf(stderr, "Failed to write %s\n", path);
    fclose(f);
    loxc_buffer_free(&embedded);
    loxc_close(ctx);
    return 1;
  }
  fclose(f);

  printf("Created self-contained file: %s (%zu bytes)\n", path, embedded.size);

  loxc_close(ctx);

  restored = loxc_decompress_buffer(NULL, embedded.data, embedded.size);
  if (restored.error != LOXC_OK) {
    fprintf(stderr, "Decompress: %s\n", loxc_strerror(restored.error));
    loxc_buffer_free(&embedded);
    return 1;
  }

  if (restored.size == strlen(text) &&
      memcmp(restored.data, text, restored.size) == 0) {
    printf("Embedded round-trip OK\n");
  } else {
    fprintf(stderr, "Embedded round-trip mismatch\n");
    loxc_buffer_free(&embedded);
    loxc_buffer_free(&restored);
    return 1;
  }

  loxc_buffer_free(&embedded);
  loxc_buffer_free(&restored);
  return 0;
}
