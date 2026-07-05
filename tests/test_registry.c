#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc.h"
#include "loxc_base.h"
#include "loxc_stream.h"
#include "loxc_tab.h"

static int demo_encode(const uint8_t *in, size_t in_len, uint8_t *out,
                       size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;

  loxc_writer_t w;
  int rc = loxc_writer_init(&w, out, out_cap);
  if (rc != LOXC_OK) return rc;

  if (in_len > LOXC_HEADER_MAX_EXACT_PAYLOAD_LEN) return LOXC_ERR_OVERFLOW;

  loxc_header_t h;
  h.module_id = 0x10;
  h.version = LOXC_HEADER_VERSION_V2;
  h.flags = 0;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.payload_len = (uint16_t)in_len;
  h.level_count = 0;
  h.uncompressed_len = (uint32_t)in_len;
  h.table_fingerprint = 0u;
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
  if (h.version != LOXC_HEADER_VERSION_V2) return LOXC_ERR_INVALID_MAGIC;

  size_t header_bytes = loxc_header_size(&h);
  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;
  size_t payload_len = 0;
  rc = loxc_header_resolve_payload_len(&h, in_len - header_bytes, &payload_len);
  if (rc != LOXC_OK) return rc;
  if ((size_t)h.uncompressed_len > out_cap) return LOXC_ERR_OVERFLOW;

  if (payload_len != (size_t)h.uncompressed_len) return LOXC_ERR_INVALID_FORMAT;
  memcpy(out, in + header_bytes, payload_len);
  *out_len = payload_len;
  return LOXC_OK;
}

static int busy_encode(const uint8_t *in, size_t in_len, uint8_t *out,
                       size_t out_cap, size_t *out_len) {
  (void)in;
  (void)in_len;
  (void)out;
  (void)out_cap;

  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;

  assert(loxc_module_unregister("busy") == LOXC_ERR_BUSY);
  return LOXC_ERR_INVALID_FORMAT;
}

static int busy_decode(const uint8_t *in, size_t in_len, uint8_t *out,
                       size_t out_cap, size_t *out_len) {
  (void)in;
  (void)in_len;
  (void)out;
  (void)out_cap;
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  return LOXC_ERR_INVALID_FORMAT;
}

static const loxc_module_t DEMO_MODULE = {
  .name = "demo",
  .module_id = 0x10,
  .version = 2,
  .strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH,
  .encode = demo_encode,
  .decode = demo_decode,
};

static const loxc_module_t BUSY_MODULE = {
  .name = "busy",
  .module_id = 0x11,
  .version = 2,
  .strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH,
  .encode = busy_encode,
  .decode = busy_decode,
};

static void copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  assert(in != NULL);
  FILE *out = fopen(dst, "wb");
  assert(out != NULL);

  unsigned char buf[4096];
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    assert(fwrite(buf, 1, n, out) == n);
  }

  assert(fclose(in) == 0);
  assert(fclose(out) == 0);
}

static void remove_file_if_exists(const char *path) {
  (void)remove(path);
}

static void test_register_and_compress_decompress(void) {
  assert(loxc_module_register(&DEMO_MODULE) == LOXC_OK);

  const char *msg = "hello registry";
  uint8_t compressed[256];
  size_t cap = sizeof(compressed);
  size_t actual = 0;

  assert(loxc_compress("demo", msg, strlen(msg), compressed, &cap, &actual) ==
         LOXC_OK);
  assert(actual > LOXC_HEADER_SIZE_V2);

  char out[256];
  size_t out_cap = sizeof(out);
  size_t out_actual = 0;

  assert(loxc_decompress(compressed, actual, out, &out_cap, &out_actual) ==
         LOXC_OK);
  assert(out_actual == strlen(msg));
  assert(memcmp(out, msg, out_actual) == 0);
}

static void test_unknown_module_name(void) {
  uint8_t compressed[64];
  size_t cap = sizeof(compressed);
  size_t actual = 0;
  assert(loxc_compress("does_not_exist", "x", 1u, compressed, &cap, &actual) ==
         LOXC_ERR_MODULE_NOT_FOUND);
}

static void test_duplicate_register_fails(void) {
  assert(loxc_module_register(&DEMO_MODULE) == LOXC_ERR_DUPLICATE_MODULE);
}

static void test_unregister_removes_lookup(void) {
  assert(loxc_module_unregister("demo") == LOXC_OK);

  uint8_t compressed[64];
  size_t cap = sizeof(compressed);
  size_t actual = 0;
  assert(loxc_compress("demo", "x", 1u, compressed, &cap, &actual) ==
         LOXC_ERR_MODULE_NOT_FOUND);
}

static void test_static_module_cannot_unload(void) {
  loxc_module_t copy = DEMO_MODULE;
  assert(loxc_module_unload(&copy) == LOXC_ERR_INVALID_MODULE);
}

static void test_busy_unregister_is_rejected(void) {
  assert(loxc_module_register(&BUSY_MODULE) == LOXC_OK);

  uint8_t compressed[32];
  size_t cap = sizeof(compressed);
  size_t actual = 0;
  assert(loxc_compress("busy", "x", 1u, compressed, &cap, &actual) ==
         LOXC_ERR_INVALID_FORMAT);

  assert(loxc_module_unregister("busy") == LOXC_OK);
}

static void test_runtime_module_lifetime(void) {
  const char *table_path = "tests/runtime_registry_demo.loxctab";
  remove_file_if_exists(table_path);
  copy_file("modules/loxc_demo.loxctab", table_path);

  loxc_module_t *module = loxc_module_load_from_file(table_path);
  assert(module != NULL);
  assert(strcmp(module->name, "runtime_registry_demo") == 0);

  assert(loxc_module_register(module) == LOXC_OK);
  assert(loxc_module_unload(module) == LOXC_ERR_BUSY);

  const char *sample = "Hello world";
  uint8_t encoded[4096];
  size_t enc_cap = sizeof(encoded);
  size_t enc_len = 0;
  assert(loxc_compress("runtime_registry_demo", sample, strlen(sample), encoded,
                       &enc_cap, &enc_len) == LOXC_OK);

  assert(loxc_module_unregister("runtime_registry_demo") == LOXC_OK);
  assert(loxc_compress("runtime_registry_demo", sample, strlen(sample), encoded,
                       &enc_cap, &enc_len) == LOXC_ERR_MODULE_NOT_FOUND);
  assert(loxc_module_unload(module) == LOXC_OK);

  remove_file_if_exists(table_path);
}

int main(void) {
  test_register_and_compress_decompress();
  puts("test_registry: PASS (register+dispatch)");

  test_unknown_module_name();
  puts("test_registry: PASS (unknown module name)");

  test_duplicate_register_fails();
  puts("test_registry: PASS (duplicate register)");

  test_unregister_removes_lookup();
  puts("test_registry: PASS (unregister removes lookup)");

  test_static_module_cannot_unload();
  puts("test_registry: PASS (static module unload rejected)");

  test_busy_unregister_is_rejected();
  puts("test_registry: PASS (busy unregister rejected)");

  test_runtime_module_lifetime();
  puts("test_registry: PASS (runtime module lifetime)");

  puts("test_registry: PASS (all)");
  return 0;
}
