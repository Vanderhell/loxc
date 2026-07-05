#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc.h"
#include "loxc_base.h"
#include "loxc_stream.h"
#include "loxc_tab.h"

#include "loxc_demo.h"

static const char *SAMPLE =
    "The quick brown fox jumps over the lazy dog. "
    "She and her sister were very happy that day. "
    "It was the best of times.";

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

static size_t compress_with_module(const char *module_name,
                                   const void *text,
                                   size_t text_len,
                                   uint8_t *out,
                                   size_t out_cap) {
  size_t cap = out_cap;
  size_t out_len = 0;
  assert(loxc_compress(module_name, (const char *)text, text_len, out, &cap, &out_len) ==
         LOXC_OK);
  return out_len;
}

static void expect_roundtrip_ok(const char *module_name, const char *text) {
  uint8_t encoded[8192];
  char decoded[8192];
  size_t encoded_len = compress_with_module(module_name, text, strlen(text),
                                            encoded, sizeof(encoded));
  size_t decoded_cap = sizeof(decoded);
  size_t decoded_len = 0;
  assert(loxc_decompress(encoded, encoded_len, decoded, &decoded_cap, &decoded_len) ==
         LOXC_OK);
  assert(decoded_len == strlen(text));
  assert(memcmp(decoded, text, decoded_len) == 0);
}

static void expect_decode_rc(const uint8_t *encoded,
                             size_t encoded_len,
                             const char *label,
                             int expected_rc) {
  char decoded[8192];
  size_t decoded_cap = sizeof(decoded);
  size_t decoded_len = 0;
  const int actual_rc =
      loxc_decompress(encoded, encoded_len, decoded, &decoded_cap, &decoded_len);
  (void)label;
  assert(actual_rc == expected_rc);
}

static uint32_t read_u32_le_at(const uint8_t *buf, size_t off) {
  return (uint32_t)buf[off] |
         ((uint32_t)buf[off + 1u] << 8u) |
         ((uint32_t)buf[off + 2u] << 16u) |
         ((uint32_t)buf[off + 3u] << 24u);
}

static uint16_t read_u16_le_at(const uint8_t *buf, size_t off) {
  return (uint16_t)buf[off] |
         (uint16_t)((uint16_t)buf[off + 1u] << 8u);
}

static void write_u32_le_at(uint8_t *buf, size_t off, uint32_t v) {
  buf[off] = (uint8_t)(v & 0xFFu);
  buf[off + 1u] = (uint8_t)((v >> 8u) & 0xFFu);
  buf[off + 2u] = (uint8_t)((v >> 16u) & 0xFFu);
  buf[off + 3u] = (uint8_t)((v >> 24u) & 0xFFu);
}

static void expect_roundtrip_contract(const char *module_name) {
  uint8_t encoded[8192];
  size_t encoded_len = compress_with_module(module_name, SAMPLE, strlen(SAMPLE),
                                            encoded, sizeof(encoded));

  expect_decode_rc(encoded, encoded_len, "roundtrip", LOXC_OK);
  expect_decode_rc(encoded, encoded_len - 1u, "payload_truncated",
                   LOXC_ERR_TRUNCATED);

  {
    uint8_t with_trailing[8193];
    memcpy(with_trailing, encoded, encoded_len);
    with_trailing[encoded_len] = 0u;
    expect_decode_rc(with_trailing, encoded_len + 1u, "trailing_byte",
                     LOXC_ERR_INVALID_FORMAT);
  }

  {
    uint8_t mismatch[8192];
    const uint32_t declared_len =
        read_u32_le_at(encoded, LOXC_HEADER_OFFSET_UNCOMPRESSED_LEN);

    assert(declared_len > 0u);
    memcpy(mismatch, encoded, encoded_len);
    write_u32_le_at(mismatch, LOXC_HEADER_OFFSET_UNCOMPRESSED_LEN,
                    declared_len - 1u);
    expect_decode_rc(mismatch, encoded_len, "declared_len_shorter",
                     LOXC_ERR_INVALID_FORMAT);
  }

  {
    char decoded[8];
    size_t decoded_cap = 8u;
    size_t decoded_len = 0;
    assert(loxc_decompress(encoded, encoded_len, decoded, &decoded_cap, &decoded_len) ==
           LOXC_ERR_OVERFLOW);
  }
}

static void expect_nonzero_padding_rejected(const char *module_name) {
  static const char *pad_samples[] = { "e", "a", "o", "t", " " };
  uint8_t encoded[256];
  size_t encoded_len = 0;
  int found = 0;

  for (size_t i = 0; i < sizeof(pad_samples) / sizeof(pad_samples[0]); i++) {
    encoded_len = compress_with_module(module_name, pad_samples[i],
                                       strlen(pad_samples[i]), encoded,
                                       sizeof(encoded));
    if (encoded_len > LOXC_HEADER_SIZE_V2 &&
        (encoded[encoded_len - 1u] & 0x0Fu) == 0u) {
      found = 1;
      break;
    }
  }

  assert(found != 0);
  encoded[encoded_len - 1u] |= 0x01u;
  expect_decode_rc(encoded, encoded_len, "nonzero_padding",
                   LOXC_ERR_INVALID_FORMAT);
}

static void test_direct_generated_roundtrip(void) {
  uint8_t buf[8192];
  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  const size_t in_len = strlen(SAMPLE);
  assert(loxc_mod_demo_encode(SAMPLE, in_len, &w) == LOXC_OK);
  assert(loxc_writer_flush(&w) == LOXC_OK);

  const size_t out_bytes = loxc_writer_size(&w);
  printf("test_train_demo: input=%zu bytes, encoded=%zu bytes (%.1f%%)\n",
         in_len, out_bytes,
         in_len ? (100.0 * (double)out_bytes / (double)in_len) : 0.0);

  /* Direct generated decode consumes the raw encoded payload stream only. */
  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, out_bytes) == LOXC_OK);

  char decoded[8192];
  size_t dec_cap = in_len;
  assert(loxc_mod_demo_decode(&r, decoded, &dec_cap) == LOXC_OK);
  decoded[dec_cap] = '\0';

  assert(dec_cap == in_len);
  assert(memcmp(SAMPLE, decoded, in_len) == 0);
}

static void test_generated_level_count_consistency(void) {
  uint8_t encoded[8192];
  size_t encoded_len =
      compress_with_module("demo", SAMPLE, strlen(SAMPLE), encoded, sizeof(encoded));
  assert(encoded_len >= LOXC_HEADER_SIZE_V2);
  assert(read_u16_le_at(encoded, LOXC_HEADER_OFFSET_LEVEL_COUNT) ==
         (uint16_t)LOXC_MOD_DEMO_LEVELS);
}

static void expect_binary_roundtrip(const char *module_name,
                                    const uint8_t *data,
                                    size_t data_len) {
  uint8_t encoded[8192];
  uint8_t decoded[8192];
  size_t encoded_len =
      compress_with_module(module_name, data, data_len, encoded, sizeof(encoded));
  size_t decoded_cap = sizeof(decoded);
  size_t decoded_len = 0;
  assert(loxc_decompress(encoded, encoded_len, (char *)decoded, &decoded_cap,
                         &decoded_len) == LOXC_OK);
  assert(decoded_len == data_len);
  assert(memcmp(decoded, data, data_len) == 0);
}

static void test_unseen_byte_roundtrip(const char *module_name) {
  static const uint8_t utf8_text[] =
      "Pr\xC3\xADli\xC5\xA1 \xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD k\xC5\xAF\xC5\x88 | "
      "P\xC5\x99\xC3\xADli\xC5\xA1 \xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD k\xC5\xAF\xC5\x88 | "
      "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85 ja\xC5\xBA\xC5\x84";
  static const uint8_t binary_blob[] = {
      0x00, 0x41, 0x00, 0xFF, 0x7F, 0xC4, 0x8D, 0x5C, 0x2F, 0x3A, 0x7C, 0x23
  };
  static const uint8_t json_blob[] =
      "{\"msg\":\"neo\xE2\x80\x94surprise\",\"path\":\"C:\\\\tmp\\\\x?.log\",\"nul\":\"A\x00B\"}";

  expect_binary_roundtrip(module_name, utf8_text, sizeof(utf8_text) - 1u);
  expect_binary_roundtrip(module_name, binary_blob, sizeof(binary_blob));
  expect_binary_roundtrip(module_name, json_blob, sizeof(json_blob) - 1u);
}

int main(void) {
  const char *runtime_table = "tests/runtime_train_demo.loxctab";
  remove_file_if_exists(runtime_table);
  copy_file("modules/loxc_demo.loxctab", runtime_table);

  test_direct_generated_roundtrip();
  puts("test_train_demo: PASS (generated direct round-trip)");

  assert(loxc_mod_demo_register() == LOXC_OK);
  expect_roundtrip_ok("demo", SAMPLE);
  puts("test_train_demo: PASS (generated registry round-trip)");
  test_generated_level_count_consistency();
  puts("test_train_demo: PASS (generated level-count consistency)");

  expect_roundtrip_contract("demo");
  puts("test_train_demo: PASS (generated decode contract)");

  expect_nonzero_padding_rejected("demo");
  puts("test_train_demo: PASS (generated non-zero padding rejection)");

  test_unseen_byte_roundtrip("demo");
  puts("test_train_demo: PASS (generated unseen-byte round-trip)");

  assert(loxc_module_unregister("demo") == LOXC_OK);

  loxc_module_t *runtime_module =
      loxc_module_load_from_file(runtime_table);
  assert(runtime_module != NULL);
  assert(strcmp(runtime_module->name, "runtime_train_demo") == 0);
  assert(loxc_module_register(runtime_module) == LOXC_OK);
  assert(runtime_module->level_count == (uint16_t)LOXC_MOD_DEMO_LEVELS);

  expect_roundtrip_ok("runtime_train_demo", SAMPLE);
  puts("test_train_demo: PASS (runtime-loaded round-trip)");

  expect_roundtrip_contract("runtime_train_demo");
  puts("test_train_demo: PASS (runtime-loaded decode contract)");

  expect_nonzero_padding_rejected("runtime_train_demo");
  puts("test_train_demo: PASS (runtime-loaded non-zero padding rejection)");

  test_unseen_byte_roundtrip("runtime_train_demo");
  puts("test_train_demo: PASS (runtime-loaded unseen-byte round-trip)");

  assert(loxc_module_unregister("runtime_train_demo") == LOXC_OK);
  assert(loxc_module_unload(runtime_module) == LOXC_OK);
  remove_file_if_exists(runtime_table);

  puts("test_train_demo: PASS (all)");
  return 0;
}
