#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc.h"
#include "loxc_base.h"
#include "loxc_tab.h"

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

int main(void) {
  copy_file("modules/loxc_demo.loxctab", "/tmp/loaded_test.loxctab");

  loxc_module_t *module = loxc_module_load_from_file("/tmp/loaded_test.loxctab");
  assert(module != NULL);
  assert(strcmp(module->name, "loaded_test") == 0);
  assert(module->strategy_id == LOXC_STRATEGY_HIERARCHICAL_4);
  assert(module->private_data != NULL);
  assert(module->encode != NULL);
  assert(module->decode != NULL);

  assert(loxc_module_register(module) == LOXC_OK);

  const char *sample = "Hello world";
  uint8_t encoded[4096];
  size_t enc_cap = sizeof(encoded);
  size_t enc_len = 0;
  assert(loxc_compress("loaded_test", sample, strlen(sample), encoded, &enc_cap,
                       &enc_len) == LOXC_OK);

  char decoded[4096];
  size_t dec_cap = sizeof(decoded);
  size_t dec_len = 0;
  assert(loxc_decompress(encoded, enc_len, decoded, &dec_cap, &dec_len) ==
         LOXC_OK);
  assert(dec_len == strlen(sample));
  assert(memcmp(decoded, sample, dec_len) == 0);

  uint8_t embedded[8192];
  size_t embedded_cap = sizeof(embedded);
  size_t embedded_len = 0;
  assert(loxc_compress_with_options("loaded_test", sample, strlen(sample),
                                    embedded, &embedded_cap, &embedded_len,
                                    1) == LOXC_OK);
  assert(embedded_len > enc_len);
  assert((embedded[5] & LOXC_FLAG_EMBEDDED_TABLE) != 0);

  loxc_module_unload(module);

  char embedded_decoded[4096];
  size_t embedded_dec_cap = sizeof(embedded_decoded);
  size_t embedded_dec_len = 0;
  assert(loxc_decompress(embedded, embedded_len, embedded_decoded,
                         &embedded_dec_cap, &embedded_dec_len) == LOXC_OK);
  assert(embedded_dec_len == strlen(sample));
  assert(memcmp(embedded_decoded, sample, embedded_dec_len) == 0);

  puts("test_loader: PASS (external and embedded loxctab round-trip)");
  return 0;
}
