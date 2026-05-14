#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc.h"
#include "loxc_simple.h"

static void write_file(const char *path, const void *data, size_t size) {
  FILE *f = fopen(path, "wb");
  assert(f != NULL);
  assert(fwrite(data, 1, size, f) == size);
  assert(fclose(f) == 0);
}

static void read_file(const char *path, uint8_t **out_data, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  long sz = 0;
  uint8_t *buf = NULL;

  assert(f != NULL);
  assert(fseek(f, 0, SEEK_END) == 0);
  sz = ftell(f);
  assert(sz >= 0);
  assert(fseek(f, 0, SEEK_SET) == 0);

  buf = (uint8_t *)malloc((size_t)sz);
  assert(buf != NULL || sz == 0);
  if (sz > 0) {
    assert(fread(buf, 1, (size_t)sz, f) == (size_t)sz);
  }
  assert(fclose(f) == 0);

  *out_data = buf;
  *out_size = (size_t)sz;
}

static void test_simple_open_close(void) {
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
  assert(ctx != NULL);
  loxc_close(ctx);
}

static void test_simple_buffer_roundtrip(void) {
  const char *sample = "Hello world";
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
  loxc_buffer_t encoded;
  loxc_buffer_t decoded;

  assert(ctx != NULL);

  encoded = loxc_compress_buffer(ctx, sample, strlen(sample), 0);
  assert(encoded.error == LOXC_OK);
  assert(encoded.data != NULL);
  assert(encoded.size > 0);

  decoded = loxc_decompress_buffer(ctx, encoded.data, encoded.size);
  assert(decoded.error == LOXC_OK);
  assert(decoded.size == strlen(sample));
  assert(memcmp(decoded.data, sample, decoded.size) == 0);

  loxc_buffer_free(&encoded);
  loxc_buffer_free(&decoded);
  loxc_close(ctx);
}

static void test_simple_file_roundtrip(void) {
  static const char sample[] =
      "The quick brown fox jumps over the lazy dog.\n";
  const char *input_path = "/tmp/loxc_simple_in.txt";
  const char *compressed_path = "/tmp/loxc_simple_out.loxc";
  const char *restored_path = "/tmp/loxc_simple_restored.txt";
  uint8_t *restored = NULL;
  size_t restored_size = 0;
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");

  assert(ctx != NULL);
  write_file(input_path, sample, sizeof(sample) - 1u);
  assert(loxc_compress_file(ctx, input_path, compressed_path, 0) == LOXC_OK);
  assert(loxc_decompress_file(ctx, compressed_path, restored_path) == LOXC_OK);

  read_file(restored_path, &restored, &restored_size);
  assert(restored_size == sizeof(sample) - 1u);
  assert(memcmp(restored, sample, restored_size) == 0);

  free(restored);
  loxc_close(ctx);
}

static void test_simple_embedded_mode(void) {
  const char *sample = "Embedded round-trip";
  const char *input_path = "/tmp/loxc_simple_embed_input.txt";
  const char *embedded_path = "/tmp/loxc_simple_embedded.loxc";
  const char *restored_path = "/tmp/loxc_simple_embedded.txt";
  uint8_t *restored = NULL;
  size_t restored_size = 0;
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");

  assert(ctx != NULL);
  write_file(input_path, sample, strlen(sample));
  assert(loxc_compress_file(ctx, input_path, embedded_path, 1) == LOXC_OK);
  loxc_close(ctx);

  assert(loxc_decompress_file(NULL, embedded_path, restored_path) == LOXC_OK);
  read_file(restored_path, &restored, &restored_size);
  assert(restored_size == strlen(sample));
  assert(memcmp(restored, sample, restored_size) == 0);
  free(restored);
}

static void test_simple_error_handling(void) {
  assert(strcmp(loxc_strerror(LOXC_OK), "success") == 0);
  assert(strcmp(loxc_strerror(LOXC_ERR_NULL), "null pointer argument") == 0);
  assert(strcmp(loxc_strerror(LOXC_ERR_INVALID_MAGIC), "invalid file magic") == 0);
  assert(strcmp(loxc_strerror(LOXC_ERR_SYMBOL_NOT_FOUND),
                "symbol not in module dictionary") == 0);
}

static void test_simple_invalid_file(void) {
  const char *bad_path = "/tmp/loxc_simple_invalid.bin";
  const uint8_t bogus[] = {0xFFu, 0x00u, 0xAAu, 0x55u};

  assert(loxc_check_file("/tmp/loxc_does_not_exist.loxc") != LOXC_OK);
  write_file(bad_path, bogus, sizeof(bogus));
  assert(loxc_check_file(bad_path) != LOXC_OK);
}

int main(void) {
  test_simple_open_close();
  puts("test_simple: PASS (open/close)");

  test_simple_buffer_roundtrip();
  puts("test_simple: PASS (buffer round-trip)");

  test_simple_file_roundtrip();
  puts("test_simple: PASS (file round-trip)");

  test_simple_embedded_mode();
  puts("test_simple: PASS (embedded mode)");

  test_simple_error_handling();
  puts("test_simple: PASS (error handling)");

  test_simple_invalid_file();
  puts("test_simple: PASS (invalid file)");

  puts("test_simple: PASS (all)");
  return 0;
}
