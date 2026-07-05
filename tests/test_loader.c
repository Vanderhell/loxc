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

static void remove_file_if_exists(const char *path) {
  (void)remove(path);
}

static void write_u16_le(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8u) & 0xFFu);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8u) & 0xFFu);
  p[2] = (uint8_t)((v >> 16u) & 0xFFu);
  p[3] = (uint8_t)((v >> 24u) & 0xFFu);
}

static void init_test_table_header(uint8_t *buf,
                                   uint8_t module_id,
                                   uint8_t strategy_id,
                                   uint8_t base_size,
                                   uint8_t bits_per_level,
                                   uint16_t level_count,
                                   uint32_t symbol_count,
                                   uint32_t dict_count,
                                   uint32_t data_size,
                                   uint32_t table_fingerprint,
                                   const char *table_name) {
  memset(buf, 0, LOXC_TAB_HEADER_SIZE);
  memcpy(buf, LOXC_TAB_MAGIC, 4u);
  buf[4] = (uint8_t)LOXC_TAB_VERSION;
  buf[5] = 2u;
  buf[6] = module_id;
  buf[7] = strategy_id;
  buf[8] = base_size;
  buf[9] = bits_per_level;
  write_u16_le(buf + 10u, level_count);
  write_u32_le(buf + 12u, symbol_count);
  write_u32_le(buf + 16u, dict_count);
  write_u32_le(buf + 20u, data_size);
  write_u32_le(buf + 24u, table_fingerprint);
  buf[28] = (uint8_t)strlen(table_name);
  memcpy(buf + 29u, table_name, strlen(table_name));
}

static size_t build_minimal_flat_table(uint8_t *buf, size_t cap) {
  const uint32_t symbol_count = 1u;
  const uint32_t dict_count = 0u;
  const uint32_t data_size = 1024u + 5u + 4u + 4u;
  const size_t total_size =
      (size_t)LOXC_TAB_HEADER_SIZE + (size_t)data_size + LOXC_TAB_TRAILER_SIZE;
  size_t pos = 0u;

  assert(cap >= total_size);
  memset(buf, 0, total_size);
  init_test_table_header(buf, 33u, (uint8_t)LOXC_STRATEGY_FLAT_FIXED_WIDTH,
                         0u, 0u, 0u, symbol_count, dict_count, data_size,
                         0x11111111u, "flat_a");

  pos = LOXC_TAB_HEADER_SIZE;
  for (size_t i = 0u; i < 256u; i++) {
    write_u32_le(buf + pos, (i == (uint8_t)'A') ? 0u : 0xFFFFFFFFu);
    pos += 4u;
  }

  buf[pos++] = 0u;
  write_u32_le(buf + pos, (uint32_t)'A');
  pos += 4u;

  write_u32_le(buf + pos, 0u);
  pos += 4u;
  write_u32_le(buf + pos, 0u);
  pos += 4u;

  assert(pos == LOXC_TAB_HEADER_SIZE + data_size);
  write_u32_le(buf + pos, loxc_crc32(buf, LOXC_TAB_HEADER_SIZE + data_size));
  pos += 4u;
  assert(pos == total_size);
  return total_size;
}

static size_t build_minimal_dict_table(uint8_t *buf, size_t cap) {
  const uint32_t symbol_count = 2u;
  const uint32_t dict_count = 1u;
  const uint32_t dict_data_size = 1u;
  const uint32_t data_size = 1024u + (symbol_count * 5u) +
                             ((dict_count + 1u) * 4u) + 4u + dict_data_size;
  const size_t total_size =
      (size_t)LOXC_TAB_HEADER_SIZE + (size_t)data_size + LOXC_TAB_TRAILER_SIZE;
  size_t pos = 0u;

  assert(cap >= total_size);
  memset(buf, 0, total_size);
  init_test_table_header(buf, 34u, (uint8_t)LOXC_STRATEGY_FLAT_FIXED_WIDTH,
                         0u, 0u, 0u, symbol_count, dict_count, data_size,
                         0x22222222u, "dict_ab");

  pos = LOXC_TAB_HEADER_SIZE;
  for (size_t i = 0u; i < 256u; i++) {
    write_u32_le(buf + pos, (i == (uint8_t)'A') ? 0u : 0xFFFFFFFFu);
    pos += 4u;
  }

  buf[pos++] = 0u;
  write_u32_le(buf + pos, (uint32_t)'A');
  pos += 4u;

  buf[pos++] = 1u;
  write_u32_le(buf + pos, 0u);
  pos += 4u;

  write_u32_le(buf + pos, 0u);
  pos += 4u;
  write_u32_le(buf + pos, 1u);
  pos += 4u;
  write_u32_le(buf + pos, dict_data_size);
  pos += 4u;
  buf[pos++] = (uint8_t)'B';

  assert(pos == LOXC_TAB_HEADER_SIZE + data_size);
  write_u32_le(buf + pos, loxc_crc32(buf, LOXC_TAB_HEADER_SIZE + data_size));
  pos += 4u;
  assert(pos == total_size);
  return total_size;
}

static void expect_load_memory_rc(const uint8_t *buf,
                                  size_t len,
                                  int expected_rc) {
  loxc_module_t *module = NULL;
  loxc_tab_error_t err;
  memset(&err, 0, sizeof(err));
  assert(loxc_module_load_from_memory_ex(buf, len, "memory_test", &module, &err) ==
         expected_rc);
  if (expected_rc == LOXC_OK) {
    assert(module != NULL);
    assert(loxc_module_unload(module) == LOXC_OK);
  } else {
    assert(module == NULL);
    assert(err.code == expected_rc);
    assert(err.message != NULL);
  }
}

static void test_runtime_roundtrip(void) {
  const char *table_a = "tests/loaded_test_a.loxctab";
  remove_file_if_exists(table_a);
  copy_file("modules/loxc_demo.loxctab", table_a);

  loxc_module_t *module = loxc_module_load_from_file(table_a);
  assert(module != NULL);
  assert(strcmp(module->name, "loaded_test_a") == 0);
  assert(module->strategy_id == LOXC_STRATEGY_HIERARCHICAL_4);
  assert(module->private_data != NULL);
  assert(module->encode != NULL);
  assert(module->decode != NULL);

  assert(loxc_module_register(module) == LOXC_OK);

  const char *sample = "Hello world";
  uint8_t encoded[4096];
  size_t enc_cap = sizeof(encoded);
  size_t enc_len = 0;
  assert(loxc_compress("loaded_test_a", sample, strlen(sample), encoded, &enc_cap,
                       &enc_len) == LOXC_OK);

  char decoded[4096];
  size_t dec_cap = sizeof(decoded);
  size_t dec_len = 0;
  assert(loxc_decompress(encoded, enc_len, decoded, &dec_cap, &dec_len) ==
         LOXC_OK);
  assert(dec_len == strlen(sample));
  assert(memcmp(decoded, sample, dec_len) == 0);

  {
    uint8_t encoded_with_trailing[4097];
    memcpy(encoded_with_trailing, encoded, enc_len);
    encoded_with_trailing[enc_len] = 0u;
    dec_cap = sizeof(decoded);
    dec_len = 0;
    assert(loxc_decompress(encoded_with_trailing, enc_len + 1u, decoded,
                           &dec_cap, &dec_len) == LOXC_ERR_INVALID_FORMAT);
  }

  {
    const char *pad_samples[] = { "e", "a", "o", "t", " " };
    size_t pad_len = 0;
    int found = 0;
    for (size_t i = 0; i < sizeof(pad_samples) / sizeof(pad_samples[0]); i++) {
      size_t cap_try = sizeof(encoded);
      size_t len_try = 0;
      assert(loxc_compress("loaded_test_a", pad_samples[i], strlen(pad_samples[i]),
                           encoded, &cap_try, &len_try) == LOXC_OK);
      if (len_try > LOXC_HEADER_SIZE_V2 && (encoded[len_try - 1u] & 0x0Fu) == 0u) {
        pad_len = len_try;
        found = 1;
        break;
      }
    }
    assert(found != 0);
    encoded[pad_len - 1u] |= 0x01u;
    dec_cap = sizeof(decoded);
    dec_len = 0;
    assert(loxc_decompress(encoded, pad_len, decoded, &dec_cap, &dec_len) ==
           LOXC_ERR_INVALID_FORMAT);
  }

  uint8_t embedded[8192];
  size_t embedded_cap = sizeof(embedded);
  size_t embedded_len = 0;
  assert(loxc_compress_with_options("loaded_test_a", sample, strlen(sample),
                                    embedded, &embedded_cap, &embedded_len,
                                    1) == LOXC_OK);
  assert(embedded_len > enc_len);
  assert((embedded[LOXC_HEADER_OFFSET_FLAGS] & LOXC_FLAG_EMBEDDED_TABLE) != 0);

  assert(loxc_module_unregister("loaded_test_a") == LOXC_OK);
  assert(loxc_module_unload(module) == LOXC_OK);

  char embedded_decoded[4096];
  size_t embedded_dec_cap = sizeof(embedded_decoded);
  size_t embedded_dec_len = 0;
  assert(loxc_decompress(embedded, embedded_len, embedded_decoded,
                         &embedded_dec_cap, &embedded_dec_len) == LOXC_OK);
  assert(embedded_dec_len == strlen(sample));
  assert(memcmp(embedded_decoded, sample, embedded_dec_len) == 0);

  {
    uint8_t embedded_with_trailing[8193];
    memcpy(embedded_with_trailing, embedded, embedded_len);
    embedded_with_trailing[embedded_len] = 0u;
    embedded_dec_cap = sizeof(embedded_decoded);
    embedded_dec_len = 0;
    assert(loxc_decompress(embedded_with_trailing, embedded_len + 1u,
                           embedded_decoded, &embedded_dec_cap,
                           &embedded_dec_len) == LOXC_ERR_INVALID_FORMAT);
  }
  remove_file_if_exists(table_a);
}

static void test_hostile_loader_inputs(void) {
  uint8_t table[2048];
  uint8_t mutated[2048];
  size_t len = build_minimal_flat_table(table, sizeof(table));

  expect_load_memory_rc(table, len, LOXC_OK);

  memcpy(mutated, table, len);
  mutated[0] ^= 0x01u;
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_MAGIC);

  memcpy(mutated, table, len);
  mutated[4] = 3u;
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  mutated[7] = 99u;
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  mutated[8] = 4u;
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  mutated[61] = 1u;
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  write_u32_le(mutated + 20u, (uint32_t)(len - LOXC_TAB_HEADER_SIZE -
                                          LOXC_TAB_TRAILER_SIZE + 1u));
  write_u32_le(mutated + len - LOXC_TAB_TRAILER_SIZE,
               loxc_crc32(mutated, len - LOXC_TAB_TRAILER_SIZE));
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  write_u32_le(mutated + LOXC_TAB_HEADER_SIZE, 7u);
  write_u32_le(mutated + len - LOXC_TAB_TRAILER_SIZE,
               loxc_crc32(mutated, len - LOXC_TAB_TRAILER_SIZE));
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  mutated[LOXC_TAB_HEADER_SIZE + 1024u] = 2u;
  write_u32_le(mutated + len - LOXC_TAB_TRAILER_SIZE,
               loxc_crc32(mutated, len - LOXC_TAB_TRAILER_SIZE));
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  write_u32_le(mutated + LOXC_TAB_HEADER_SIZE + 1024u + 1u, 0x1FFu);
  write_u32_le(mutated + len - LOXC_TAB_TRAILER_SIZE,
               loxc_crc32(mutated, len - LOXC_TAB_TRAILER_SIZE));
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  len = build_minimal_dict_table(table, sizeof(table));
  expect_load_memory_rc(table, len, LOXC_OK);

  memcpy(mutated, table, len);
  write_u32_le(mutated + LOXC_TAB_HEADER_SIZE + 1024u + 6u, 3u);
  write_u32_le(mutated + len - LOXC_TAB_TRAILER_SIZE,
               loxc_crc32(mutated, len - LOXC_TAB_TRAILER_SIZE));
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  write_u32_le(mutated + LOXC_TAB_HEADER_SIZE + 1024u + 10u, 1u);
  write_u32_le(mutated + len - LOXC_TAB_TRAILER_SIZE,
               loxc_crc32(mutated, len - LOXC_TAB_TRAILER_SIZE));
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  write_u32_le(mutated + LOXC_TAB_HEADER_SIZE + 1024u + 14u, 0u);
  write_u32_le(mutated + len - LOXC_TAB_TRAILER_SIZE,
               loxc_crc32(mutated, len - LOXC_TAB_TRAILER_SIZE));
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  memcpy(mutated, table, len);
  mutated[len - 1u] ^= 0x01u;
  expect_load_memory_rc(mutated, len, LOXC_ERR_INVALID_FORMAT);

  expect_load_memory_rc(table, len - 1u, LOXC_ERR_INVALID_FORMAT);
  expect_load_memory_rc(table, LOXC_TAB_HEADER_SIZE + LOXC_TAB_TRAILER_SIZE - 1u,
                        LOXC_ERR_TRUNCATED);
}

int main(void) {
  test_runtime_roundtrip();
  test_hostile_loader_inputs();

  puts("test_loader: PASS (external and embedded loxctab round-trip)");
  puts("test_loader: PASS (hostile loxctab parser)");
  return 0;
}
