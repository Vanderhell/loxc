#include <stdio.h>
#include <string.h>

#include "loxc.h"
#include "loxc_stream.h"

#include "loxc_demo.h"

static const char *SAMPLE =
    "The quick brown fox jumps over the lazy dog. "
    "She and her sister were very happy that day. "
    "It was the best of times.";

int main(void) {
  /* Test 1: Direct encode/decode (bypassing registry) */
  uint8_t buf[8192];
  loxc_writer_t w;
  if (loxc_writer_init(&w, buf, sizeof(buf)) != LOXC_OK) {
    fprintf(stderr, "writer_init failed\n");
    return 1;
  }

  const size_t in_len = strlen(SAMPLE);
  int rc = loxc_mod_demo_encode(SAMPLE, in_len, &w);
  if (rc != LOXC_OK) {
    fprintf(stderr, "encode failed: %d\n", rc);
    return 1;
  }
  rc = loxc_writer_flush(&w);
  if (rc != LOXC_OK) {
    fprintf(stderr, "writer_flush failed: %d\n", rc);
    return 1;
  }

  const size_t out_bytes = loxc_writer_size(&w);
  printf("test_train_demo: input=%zu bytes, encoded=%zu bytes (%.1f%%)\n",
         in_len, out_bytes, in_len ? (100.0 * (double)out_bytes / (double)in_len) : 0.0);

  loxc_reader_t r;
  if (loxc_reader_init(&r, buf, out_bytes) != LOXC_OK) {
    fprintf(stderr, "reader_init failed\n");
    return 1;
  }

  char decoded[8192];
  size_t dec_cap = sizeof(decoded) - 1;
  rc = loxc_mod_demo_decode(&r, decoded, &dec_cap);
  if (rc != LOXC_OK) {
    fprintf(stderr, "decode failed: %d\n", rc);
    return 1;
  }
  decoded[dec_cap] = '\0';

  if (dec_cap != in_len) {
    fprintf(stderr, "FAIL: length mismatch %zu vs %zu\n", dec_cap, in_len);
    return 1;
  }
  if (memcmp(SAMPLE, decoded, in_len) != 0) {
    fprintf(stderr, "FAIL: content mismatch\n");
    fprintf(stderr, "Input:  %s\n", SAMPLE);
    fprintf(stderr, "Output: %s\n", decoded);
    return 1;
  }
  printf("test_train_demo: PASS (round-trip OK, %zu chars)\n", in_len);

  /* Test 2: Via registry dispatch */
  rc = loxc_mod_demo_register();
  if (rc != LOXC_OK) {
    fprintf(stderr, "register failed: %d\n", rc);
    return 1;
  }

  uint8_t buf2[8192];
  size_t cap2 = sizeof(buf2);
  size_t actual2 = 0;
  rc = loxc_compress("demo", SAMPLE, in_len, buf2, &cap2, &actual2);
  if (rc != LOXC_OK) {
    fprintf(stderr, "loxc_compress failed: %d\n", rc);
    return 1;
  }

  char dec2[8192];
  size_t cap_dec2 = sizeof(dec2) - 1;
  size_t actual_dec2 = 0;
  rc = loxc_decompress(buf2, actual2, dec2, &cap_dec2, &actual_dec2);
  if (rc != LOXC_OK) {
    fprintf(stderr, "loxc_decompress failed: %d\n", rc);
    return 1;
  }
  dec2[actual_dec2] = '\0';

  if (actual_dec2 != in_len || memcmp(SAMPLE, dec2, in_len) != 0) {
    fprintf(stderr, "FAIL: registry round-trip mismatch\n");
    return 1;
  }
  printf("test_train_demo: PASS (registry dispatch OK)\n");

  puts("test_train_demo: PASS (all)");
  return 0;
}

