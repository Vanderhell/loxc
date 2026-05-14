/*
 * Compress and decompress a file using an external table.
 * Show timing for the compression step.
 */

#include <stdio.h>
#include <time.h>

#include "loxc_simple.h"

int main(int argc, char **argv) {
  loxc_ctx_t *ctx = NULL;
  clock_t start = 0;
  clock_t end = 0;
  double ms = 0.0;
  int rc = LOXC_OK;

  if (argc != 4) {
    fprintf(stderr, "Usage: %s <table.loxctab> <input> <output>\n", argv[0]);
    return 1;
  }

  ctx = loxc_open(argv[1]);
  if (ctx == NULL) {
    fprintf(stderr, "Failed to load %s\n", argv[1]);
    return 1;
  }

  start = clock();
  rc = loxc_compress_file(ctx, argv[2], argv[3], 0);
  end = clock();

  if (rc != LOXC_OK) {
    fprintf(stderr, "Error: %s\n", loxc_strerror(rc));
    loxc_close(ctx);
    return 1;
  }

  ms = (double)(end - start) / (double)CLOCKS_PER_SEC * 1000.0;
  printf("Compressed %s -> %s in %.2f ms\n", argv[2], argv[3], ms);

  loxc_close(ctx);
  return 0;
}
