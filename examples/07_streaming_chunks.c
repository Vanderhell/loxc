/*
 * True streaming API is not implemented yet.
 * This example shows the current whole-file workaround.
 */

#include <stdio.h>

#include "loxc_simple.h"

int main(int argc, char **argv) {
  loxc_ctx_t *ctx = NULL;
  int rc = LOXC_OK;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <large-file.txt>\n", argv[0]);
    return 1;
  }

  ctx = loxc_open("modules/loxc_demo.loxctab");
  if (ctx == NULL) {
    fprintf(stderr, "Failed to load demo table\n");
    return 1;
  }

  printf("Note: Current API loads the entire file into memory.\n");
  printf("True streaming is planned for v0.2.\n\n");

  rc = loxc_compress_file(ctx, argv[1], "/tmp/streaming_out.loxc", 0);
  if (rc == LOXC_OK) {
    printf("Compressed to /tmp/streaming_out.loxc\n");
  } else {
    printf("Error: %s\n", loxc_strerror(rc));
  }

  loxc_close(ctx);
  return (rc == LOXC_OK) ? 0 : 1;
}
