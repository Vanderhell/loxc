/*
 * Demonstrate typical error handling paths.
 */

#include <stdio.h>

#include "loxc_simple.h"

int main(void) {
  loxc_ctx_t *ctx = NULL;
  loxc_buffer_t buf;
  char weird[] = {0x01, 0x02, 0x03, 0x00};
  unsigned char bogus[] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
  int rc = LOXC_OK;

  ctx = loxc_open("/nonexistent.loxctab");
  if (ctx == NULL) {
    printf("Test 1 OK: loxc_open returned NULL for missing file\n");
  }

  ctx = loxc_open("modules/loxc_demo.loxctab");
  if (ctx == NULL) {
    fprintf(stderr, "demo table not found\n");
    return 1;
  }

  buf = loxc_compress_buffer(ctx, weird, 3, 0);
  if (buf.error != LOXC_OK) {
    printf("Test 3 OK: compress failed with: %s\n", loxc_strerror(buf.error));
  } else {
    printf("Test 3 unexpected: compress of unusual bytes succeeded\n");
    loxc_buffer_free(&buf);
  }

  buf = loxc_decompress_buffer(ctx, bogus, sizeof(bogus));
  if (buf.error != LOXC_OK) {
    printf("Test 4 OK: decompress failed with: %s\n", loxc_strerror(buf.error));
  } else {
    printf("Test 4 unexpected: bogus input decompressed\n");
    loxc_buffer_free(&buf);
  }

  rc = loxc_check_file("/etc/hostname");
  if (rc != LOXC_OK) {
    printf("Test 5 OK: check_file rejected non-loxc: %s\n",
           loxc_strerror(rc));
  }

  loxc_close(ctx);
  return 0;
}
