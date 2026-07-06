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

static void remove_file_if_exists(const char *path) {
  (void)remove(path);
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
  const char *input_path = "tests/tmp_loxc_simple_in.txt";
  const char *compressed_path = "tests/tmp_loxc_simple_out.loxc";
  const char *restored_path = "tests/tmp_loxc_simple_restored.txt";
  uint8_t *restored = NULL;
  size_t restored_size = 0;
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");

  assert(ctx != NULL);
  remove_file_if_exists(input_path);
  remove_file_if_exists(compressed_path);
  remove_file_if_exists(restored_path);
  write_file(input_path, sample, sizeof(sample) - 1u);
  assert(loxc_compress_file(ctx, input_path, compressed_path, 0) == LOXC_OK);
  assert(loxc_decompress_file(ctx, compressed_path, restored_path) == LOXC_OK);

  read_file(restored_path, &restored, &restored_size);
  assert(restored_size == sizeof(sample) - 1u);
  assert(memcmp(restored, sample, restored_size) == 0);

  free(restored);
  loxc_close(ctx);
  remove_file_if_exists(input_path);
  remove_file_if_exists(compressed_path);
  remove_file_if_exists(restored_path);
}

static void test_simple_embedded_mode(void) {
  const char *sample = "Embedded round-trip";
  const char *input_path = "tests/tmp_loxc_simple_embed_input.txt";
  const char *embedded_path = "tests/tmp_loxc_simple_embedded.loxc";
  const char *restored_path = "tests/tmp_loxc_simple_embedded.txt";
  uint8_t *restored = NULL;
  size_t restored_size = 0;
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");

  assert(ctx != NULL);
  remove_file_if_exists(input_path);
  remove_file_if_exists(embedded_path);
  remove_file_if_exists(restored_path);
  write_file(input_path, sample, strlen(sample));
  assert(loxc_compress_file(ctx, input_path, embedded_path, 1) == LOXC_OK);
  loxc_close(ctx);

  assert(loxc_decompress_file(NULL, embedded_path, restored_path) == LOXC_OK);
  read_file(restored_path, &restored, &restored_size);
  assert(restored_size == strlen(sample));
  assert(memcmp(restored, sample, restored_size) == 0);
  free(restored);
  remove_file_if_exists(input_path);
  remove_file_if_exists(embedded_path);
  remove_file_if_exists(restored_path);
}

static void test_simple_error_handling(void) {
  assert(strcmp(loxc_strerror(LOXC_OK), "success") == 0);
  assert(strcmp(loxc_strerror(LOXC_ERR_NULL), "null pointer argument") == 0);
  assert(strcmp(loxc_strerror(LOXC_ERR_INVALID_MAGIC), "invalid file magic") == 0);
  assert(strcmp(loxc_strerror(LOXC_ERR_SYMBOL_NOT_FOUND),
                "symbol not in module dictionary") == 0);
}

static void test_simple_invalid_file(void) {
  const char *bad_path = "tests/tmp_loxc_simple_invalid.bin";
  const uint8_t bogus[] = {0xFFu, 0x00u, 0xAAu, 0x55u};
  loxc_check_file_result_t info;

  remove_file_if_exists(bad_path);
  memset(&info, 0, sizeof(info));
  assert(loxc_check_file_ex("tests/tmp_loxc_does_not_exist.loxc", &info) != LOXC_OK);
  assert(info.rc != LOXC_OK);
  assert(info.os_errno != 0);
  assert(loxc_check_file("tests/tmp_loxc_does_not_exist.loxc") != LOXC_OK);
  write_file(bad_path, bogus, sizeof(bogus));
  assert(loxc_check_file(bad_path) != LOXC_OK);
  remove_file_if_exists(bad_path);
}

static void test_simple_check_file_ex(void) {
  const char *path = "tests/tmp_loxc_simple_check.loxc";
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
  loxc_buffer_t encoded;
  loxc_check_file_result_t info;

  assert(ctx != NULL);
  encoded = loxc_compress_buffer(ctx, "check file", strlen("check file"), 1);
  assert(encoded.error == LOXC_OK);
  write_file(path, encoded.data, encoded.size);

  memset(&info, 0, sizeof(info));
  assert(loxc_check_file_ex(path, &info) == LOXC_OK);
  assert(info.rc == LOXC_OK);
  assert(info.file_size == encoded.size);
  assert(info.header_size > 0);
  assert(info.version > 0);
  assert(info.embedded != 0);
  assert(info.payload_len > 0);

  loxc_buffer_free(&encoded);
  loxc_close(ctx);
  remove_file_if_exists(path);
}

static void test_simple_multiple_contexts(void) {
  loxc_ctx_t *ctx_a = loxc_open("modules/loxc_demo.loxctab");
  loxc_ctx_t *ctx_b = loxc_open("modules/loxc_demo.loxctab");

  assert(ctx_a != NULL);
  assert(ctx_b != NULL);

  loxc_close(ctx_b);
  loxc_close(ctx_a);
}

static void test_simple_open_ex_defaults(void) {
  loxc_simple_config_t cfg;
  loxc_ctx_t *ctx = NULL;

  memset(&cfg, 0, sizeof(cfg));
  assert(loxc_open_ex("modules/loxc_demo.loxctab", &cfg, &ctx) == LOXC_OK);
  assert(ctx != NULL);
  loxc_close(ctx);
}

static void test_simple_embedded_decode_without_ctx(void) {
  const char *sample = "Embedded round-trip";
  loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
  loxc_buffer_t encoded;
  loxc_buffer_t decoded;

  assert(ctx != NULL);
  encoded = loxc_compress_buffer(ctx, sample, strlen(sample), 1);
  assert(encoded.error == LOXC_OK);
  assert(encoded.data != NULL);
  loxc_close(ctx);

  decoded = loxc_decompress_buffer(NULL, encoded.data, encoded.size);
  assert(decoded.error == LOXC_OK);
  assert(decoded.size == strlen(sample));
  assert(memcmp(decoded.data, sample, decoded.size) == 0);

  loxc_buffer_free(&encoded);
  loxc_buffer_free(&decoded);
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

  test_simple_check_file_ex();
  puts("test_simple: PASS (check_file_ex)");

  test_simple_multiple_contexts();
  puts("test_simple: PASS (multiple contexts)");

  test_simple_open_ex_defaults();
  puts("test_simple: PASS (open_ex defaults)");

  test_simple_embedded_decode_without_ctx();
  puts("test_simple: PASS (embedded decode without ctx)");

  puts("test_simple: PASS (all)");
  return 0;
}
