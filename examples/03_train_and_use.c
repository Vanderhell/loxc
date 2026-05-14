#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc.h"
#include "loxc_tab.h"

/* SOURCE_EXCERPT_BEGIN
This example keeps a small prose block inside its own source file.
The block is intentionally plain English so a text-trained module can compress it.
The rest of the file demonstrates loading a runtime module, compressing text,
and verifying the round-trip.
SOURCE_EXCERPT_END */

static int read_self_source(uint8_t **out, size_t *out_len) {
  FILE *f = fopen(__FILE__, "rb");
  if (f == NULL) return 1;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
  long sz = ftell(f);
  if (sz < 0) { fclose(f); return 1; }
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 1; }
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (buf == NULL) { fclose(f); return 1; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return 1;
  }
  fclose(f);
  *out = buf;
  *out_len = (size_t)sz;
  return 0;
}

static int extract_excerpt(const uint8_t *source, size_t source_len,
                           const uint8_t **out_ptr, size_t *out_len) {
  const char *begin_tag = "SOURCE_EXCERPT_BEGIN";
  const char *end_tag = "SOURCE_EXCERPT_END";
  const uint8_t *begin = NULL;
  const uint8_t *end = NULL;

  for (size_t i = 0; i + strlen(begin_tag) < source_len; i++) {
    if (memcmp(source + i, begin_tag, strlen(begin_tag)) == 0) {
      begin = source + i + strlen(begin_tag);
      break;
    }
  }
  if (begin == NULL) return 1;

  for (size_t i = (size_t)(begin - source); i + strlen(end_tag) < source_len; i++) {
    if (memcmp(source + i, end_tag, strlen(end_tag)) == 0) {
      end = source + i;
      break;
    }
  }
  if (end == NULL || end <= begin) return 1;

  while (begin < end && (*begin == '\n' || *begin == '\r' || *begin == ' ' || *begin == '\t')) {
    begin++;
  }
  while (end > begin && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
    end--;
  }

  *out_ptr = begin;
  *out_len = (size_t)(end - begin);
  return 0;
}

int main(void) {
  uint8_t *source = NULL;
  size_t source_len = 0;
  assert(read_self_source(&source, &source_len) == 0);

  const uint8_t *excerpt = NULL;
  size_t excerpt_len = 0;
  assert(extract_excerpt(source, source_len, &excerpt, &excerpt_len) == 0);

  loxc_module_t *module = loxc_module_load_from_file("modules/loxc_demo.loxctab");
  assert(module != NULL);
  assert(loxc_module_register(module) == LOXC_OK);

  size_t encoded_cap = excerpt_len * 2u + 4096u;
  if (encoded_cap < excerpt_len) encoded_cap = excerpt_len + 4096u;
  uint8_t *encoded = (uint8_t *)malloc(encoded_cap);
  assert(encoded != NULL);

  size_t encoded_len = 0;
  int rc = LOXC_ERR_OVERFLOW;
  for (int attempt = 0; attempt < 8; attempt++) {
    size_t cap_arg = encoded_cap;
    encoded_len = 0;
    rc = loxc_compress("demo", (const char *)excerpt, excerpt_len, encoded,
                       &cap_arg, &encoded_len);
    if (rc != LOXC_ERR_OVERFLOW) break;
    size_t next_cap = encoded_cap * 2u + 4096u;
    if (next_cap < encoded_cap) break;
    uint8_t *grown = (uint8_t *)realloc(encoded, next_cap);
    assert(grown != NULL);
    encoded = grown;
    encoded_cap = next_cap;
  }
  assert(rc == LOXC_OK);

  char decoded[65536];
  size_t decoded_cap = sizeof(decoded);
  size_t decoded_len = 0;
  assert(loxc_decompress(encoded, encoded_len, decoded, &decoded_cap,
                         &decoded_len) == LOXC_OK);
  assert(decoded_len == excerpt_len);
  assert(memcmp(decoded, excerpt, excerpt_len) == 0);

  printf("03_train_and_use: source=%zu excerpt=%zu encoded=%zu decoded=%zu\n",
         source_len, excerpt_len, encoded_len, decoded_len);

  free(source);
  free(encoded);
  loxc_module_unload(module);
  return 0;
}
