/*
 * End-to-end training pipeline using the wrapper API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "loxc_simple.h"

int main(void) {
  const char *sample =
      "The quick brown fox jumps over the lazy dog. "
      "She was an intelligent and witty young woman.";
  loxc_ctx_t *ctx = NULL;
  loxc_buffer_t out;
  loxc_buffer_t back;
  int rc = 0;

  printf("Step 1: Training custom module from corpus...\n");
  rc = system("./tools/loxc_train "
              "--input trainings/demo_corpus.txt "
              "--output /tmp/my_table "
              "--module-name mytable --module-id 100 "
              "> /dev/null 2>&1");
  if (rc != 0) {
    fprintf(stderr, "Training failed\n");
    return 1;
  }

  printf("Step 2: Loading trained table...\n");
  ctx = loxc_open("/tmp/my_table.loxctab");
  if (ctx == NULL) {
    fprintf(stderr, "Failed to load trained table\n");
    return 1;
  }

  out = loxc_compress_buffer(ctx, sample, strlen(sample), 0);
  if (out.error != LOXC_OK) {
    fprintf(stderr, "Compress failed: %s\n", loxc_strerror(out.error));
    loxc_close(ctx);
    return 1;
  }

  printf("Step 3: Compressed %zu -> %zu bytes (%.1f%%)\n", strlen(sample),
         out.size, 100.0 * (double)out.size / (double)strlen(sample));

  back = loxc_decompress_buffer(ctx, out.data, out.size);
  if (back.error != LOXC_OK) {
    fprintf(stderr, "Decompress failed: %s\n", loxc_strerror(back.error));
    loxc_buffer_free(&out);
    loxc_close(ctx);
    return 1;
  }

  if (back.size == strlen(sample) && memcmp(back.data, sample, back.size) == 0) {
    printf("Step 4: Round-trip OK\n");
  } else {
    fprintf(stderr, "Round-trip mismatch\n");
    loxc_buffer_free(&out);
    loxc_buffer_free(&back);
    loxc_close(ctx);
    return 1;
  }

  loxc_buffer_free(&out);
  loxc_buffer_free(&back);
  loxc_close(ctx);

  unlink("/tmp/my_table.h");
  unlink("/tmp/my_table.c");
  unlink("/tmp/my_table.loxctab");
  return 0;
}
