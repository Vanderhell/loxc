#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc.h"
#include "loxc_base.h"
#include "loxc_stream.h"

static int demo_encode(const uint8_t *in, size_t in_len, uint8_t *out,
                       size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;

  loxc_writer_t w;
  int rc = loxc_writer_init(&w, out, out_cap);
  if (rc != LOXC_OK) return rc;

  if (in_len > 0xFFFFu) return LOXC_ERR_OVERFLOW;

  loxc_header_t h;
  h.module_id = 0x10;
  h.version = 2;
  h.flags = 0;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.data_len = (uint16_t)in_len;
  h.level_count = 0;
  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;
  h.crc32 = 0;

  rc = loxc_header_write(&w, &h);
  if (rc != LOXC_OK) return rc;

  rc = loxc_write_bytes(&w, in, in_len);
  if (rc != LOXC_OK) return rc;

  *out_len = loxc_writer_size(&w);
  return LOXC_OK;
}

static int demo_decode(const uint8_t *in, size_t in_len, uint8_t *out,
                       size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;

  loxc_reader_t r;
  int rc = loxc_reader_init(&r, in, in_len);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) return rc;
  if (h.module_id != 0x10) return LOXC_ERR_INVALID_MAGIC;
  if (h.version != 2) return LOXC_ERR_INVALID_MAGIC;

  size_t header_bytes = 15;
  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;
  if ((size_t)h.data_len > (in_len - header_bytes)) return LOXC_ERR_TRUNCATED;
  if ((size_t)h.data_len > out_cap) return LOXC_ERR_OVERFLOW;

  memcpy(out, in + header_bytes, (size_t)h.data_len);
  *out_len = (size_t)h.data_len;
  return LOXC_OK;
}

static const loxc_module_t DEMO_MODULE = {
  .name = "demo",
  .module_id = 0x10,
  .version = 2,
  .strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH,
  .encode = demo_encode,
  .decode = demo_decode,
};

static void test_register_and_compress_decompress(void) {
  assert(loxc_module_register(&DEMO_MODULE) == LOXC_OK);

  const char *msg = "hello registry";
  uint8_t compressed[256];
  size_t cap = sizeof(compressed);
  size_t actual = 0;

  assert(loxc_compress("demo", msg, strlen(msg), compressed, &cap, &actual) == LOXC_OK);
  assert(actual > 15);

  char out[256];
  size_t out_cap = sizeof(out);
  size_t out_actual = 0;

  assert(loxc_decompress(compressed, actual, out, &out_cap, &out_actual) == LOXC_OK);
  assert(out_actual == strlen(msg));
  assert(memcmp(out, msg, out_actual) == 0);
}

static void test_unknown_module_name(void) {
  uint8_t compressed[64];
  size_t cap = sizeof(compressed);
  size_t actual = 0;
  assert(loxc_compress("does_not_exist", "x", 1, compressed, &cap, &actual) == LOXC_ERR_MODULE_NOT_FOUND);
}

static void test_duplicate_register_fails(void) {
  /* Already registered in prior test */
  assert(loxc_module_register(&DEMO_MODULE) == LOXC_ERR_DUPLICATE_MODULE);
}

int main(void) {
  test_register_and_compress_decompress();
  puts("test_registry: PASS (register+dispatch)");

  test_unknown_module_name();
  puts("test_registry: PASS (unknown module name)");

  test_duplicate_register_fails();
  puts("test_registry: PASS (duplicate register)");

  puts("test_registry: PASS (all)");
  return 0;
}

