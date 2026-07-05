#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc_base.h"
#include "loxc_plain.h"

static void write_u16_le_at(uint8_t *buf, size_t off, uint16_t value) {
  buf[off + 0u] = (uint8_t)(value & 0xFFu);
  buf[off + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void write_u32_le_at(uint8_t *buf, size_t off, uint32_t value) {
  buf[off + 0u] = (uint8_t)(value & 0xFFu);
  buf[off + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
  buf[off + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
  buf[off + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
}

static size_t encode_plain_sample(const char *text, uint8_t *out, size_t out_cap) {
  size_t out_len = 0;
  assert(loxc_plain_encode((const uint8_t *)text, strlen(text), out, out_cap,
                           &out_len) == LOXC_OK);
  return out_len;
}

static int decode_plain_sample(const uint8_t *in, size_t in_len) {
  uint8_t out[128];
  size_t out_len = 0;
  return loxc_plain_decode(in, in_len, out, sizeof(out), &out_len);
}

static void test_roundtrip_no_crc(void) {
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  loxc_header_t h;
  h.module_id = 0x01;
  h.version = LOXC_HEADER_VERSION_V2;
  h.flags = 0;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.payload_len = 0x1234;
  h.level_count = 0;
  h.uncompressed_len = 0xA3A2A1A0u;
  h.table_fingerprint = 0x11223344u;
  h.crc32 = 0;

  assert(loxc_header_write(&w, &h) == LOXC_OK);
  assert(loxc_writer_size(&w) == LOXC_HEADER_SIZE_V2);

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_OK);
  assert(out.module_id == h.module_id);
  assert(out.version == LOXC_HEADER_VERSION_V2);
  assert(out.flags == h.flags);
  assert(out.strategy_id == h.strategy_id);
  assert(out.payload_len == h.payload_len);
  assert(out.level_count == h.level_count);
  assert(out.uncompressed_len == h.uncompressed_len);
  assert(out.table_fingerprint == h.table_fingerprint);
  assert(out.crc32 == 0u);
  assert(loxc_reader_eof(&r) != 0);
}

static void test_invalid_magic(void) {
  uint8_t buf[LOXC_HEADER_SIZE_V2];
  memset(buf, 0, sizeof(buf));
  buf[0] = 'B';
  buf[1] = 'A';
  buf[2] = 'D';
  buf[LOXC_HEADER_OFFSET_VERSION] = LOXC_HEADER_VERSION_V2;

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_ERR_INVALID_MAGIC);
}

static void test_unsupported_version(void) {
  uint8_t buf[LOXC_HEADER_SIZE_V2];
  memset(buf, 0, sizeof(buf));

  loxc_header_t h;
  h.module_id = 0x01;
  h.version = 3u;
  h.flags = 0u;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.payload_len = 1u;
  h.level_count = 0u;
  h.uncompressed_len = 1u;
  h.table_fingerprint = 0u;
  h.crc32 = 0u;
  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_write(&w, &h) == LOXC_ERR_INVALID_FORMAT);

  buf[0] = 'L';
  buf[1] = 'X';
  buf[2] = 'C';
  buf[LOXC_HEADER_OFFSET_MODULE_ID] = 0x01;
  buf[LOXC_HEADER_OFFSET_VERSION] = 3u;
  buf[LOXC_HEADER_OFFSET_FLAGS] = 0u;
  buf[LOXC_HEADER_OFFSET_STRATEGY_ID] = LOXC_STRATEGY_FLAT_FIXED_WIDTH;

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_read(&r, &h) == LOXC_ERR_INVALID_FORMAT);
}

static void test_invalid_strategy(void) {
  uint8_t buf[LOXC_HEADER_SIZE_V2];
  memset(buf, 0, sizeof(buf));
  buf[0] = 'L';
  buf[1] = 'X';
  buf[2] = 'C';
  buf[LOXC_HEADER_OFFSET_MODULE_ID] = 0x01;
  buf[LOXC_HEADER_OFFSET_VERSION] = LOXC_HEADER_VERSION_V2;
  buf[LOXC_HEADER_OFFSET_FLAGS] = 0u;
  buf[LOXC_HEADER_OFFSET_STRATEGY_ID] = 99u;

  loxc_reader_t r;
  loxc_header_t h;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_read(&r, &h) == LOXC_ERR_INVALID_FORMAT);
}

static void test_invalid_flags(void) {
  uint8_t buf[LOXC_HEADER_SIZE_V2];
  memset(buf, 0, sizeof(buf));
  buf[0] = 'L';
  buf[1] = 'X';
  buf[2] = 'C';
  buf[LOXC_HEADER_OFFSET_MODULE_ID] = LOXC_MODULE_PLAIN;
  buf[LOXC_HEADER_OFFSET_VERSION] = LOXC_HEADER_VERSION_V2;
  buf[LOXC_HEADER_OFFSET_FLAGS] = LOXC_FLAG_CRC;
  buf[LOXC_HEADER_OFFSET_STRATEGY_ID] = LOXC_STRATEGY_FLAT_FIXED_WIDTH;

  loxc_reader_t r;
  loxc_header_t h;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_read(&r, &h) == LOXC_ERR_INVALID_FORMAT);
}

static void test_invalid_strategy_level_combo(void) {
  uint8_t buf[LOXC_HEADER_SIZE_V2];
  memset(buf, 0, sizeof(buf));
  buf[0] = 'L';
  buf[1] = 'X';
  buf[2] = 'C';
  buf[LOXC_HEADER_OFFSET_MODULE_ID] = 0x01;
  buf[LOXC_HEADER_OFFSET_VERSION] = LOXC_HEADER_VERSION_V2;
  buf[LOXC_HEADER_OFFSET_FLAGS] = 0u;
  buf[LOXC_HEADER_OFFSET_STRATEGY_ID] = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  write_u16_le_at(buf, LOXC_HEADER_OFFSET_LEVEL_COUNT, 1u);

  loxc_reader_t r;
  loxc_header_t h;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_read(&r, &h) == LOXC_ERR_INVALID_FORMAT);
}

static void test_truncated_header(void) {
  uint8_t buf[LOXC_HEADER_SIZE_V2 - 1u];
  memset(buf, 0, sizeof(buf));
  buf[0] = 'L';
  buf[1] = 'X';
  buf[2] = 'C';

  loxc_reader_t r;
  loxc_header_t out;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_read(&r, &out) == LOXC_ERR_TRUNCATED);
}

static void test_crc_flag_rejected(void) {
  uint8_t buf[LOXC_HEADER_SIZE_V2];
  memset(buf, 0, sizeof(buf));

  loxc_header_t h;
  h.module_id = 0x02;
  h.version = LOXC_HEADER_VERSION_V2;
  h.flags = LOXC_FLAG_CRC;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.payload_len = 4u;
  h.level_count = 0u;
  h.uncompressed_len = 4u;
  h.table_fingerprint = 0u;
  h.crc32 = 0x12345678u;

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_write(&w, &h) == LOXC_ERR_INVALID_FORMAT);

  buf[0] = 'L';
  buf[1] = 'X';
  buf[2] = 'C';
  buf[LOXC_HEADER_OFFSET_MODULE_ID] = 0x02;
  buf[LOXC_HEADER_OFFSET_VERSION] = LOXC_HEADER_VERSION_V2;
  buf[LOXC_HEADER_OFFSET_FLAGS] = LOXC_FLAG_CRC;
  buf[LOXC_HEADER_OFFSET_STRATEGY_ID] = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  write_u16_le_at(buf, LOXC_HEADER_OFFSET_PAYLOAD_LEN, 4u);
  write_u32_le_at(buf, LOXC_HEADER_OFFSET_UNCOMPRESSED_LEN, 4u);

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_header_read(&r, &h) == LOXC_ERR_INVALID_FORMAT);
}

static void test_declared_payload_shorter_than_actual(void) {
  uint8_t buf[128];
  size_t len = encode_plain_sample("hello world", buf, sizeof(buf));
  assert(len > LOXC_HEADER_SIZE_V2 + 1u);

  write_u16_le_at(buf, LOXC_HEADER_OFFSET_PAYLOAD_LEN,
                  (uint16_t)((len - LOXC_HEADER_SIZE_V2) - 1u));
  assert(decode_plain_sample(buf, len) == LOXC_ERR_INVALID_FORMAT);
}

static void test_declared_payload_longer_than_actual(void) {
  uint8_t buf[128];
  size_t len = encode_plain_sample("hello world", buf, sizeof(buf));
  write_u16_le_at(buf, LOXC_HEADER_OFFSET_PAYLOAD_LEN,
                  (uint16_t)((len - LOXC_HEADER_SIZE_V2) + 1u));
  assert(decode_plain_sample(buf, len) == LOXC_ERR_TRUNCATED);
}

static void test_declared_uncompressed_length_mismatch(void) {
  uint8_t buf[128];
  size_t len = encode_plain_sample("hello world", buf, sizeof(buf));
  write_u32_le_at(buf, LOXC_HEADER_OFFSET_UNCOMPRESSED_LEN, 99u);
  assert(decode_plain_sample(buf, len) == LOXC_ERR_TRUNCATED);
}

static void test_trailing_bytes_rejected(void) {
  uint8_t buf[128];
  size_t len = encode_plain_sample("hello", buf, sizeof(buf));
  buf[len] = 0u;
  assert(decode_plain_sample(buf, len + 1u) == LOXC_ERR_INVALID_FORMAT);
}

int main(void) {
  test_roundtrip_no_crc();
  puts("test_header: PASS (roundtrip)");

  test_invalid_magic();
  puts("test_header: PASS (invalid magic)");

  test_unsupported_version();
  puts("test_header: PASS (unsupported version)");

  test_invalid_strategy();
  puts("test_header: PASS (invalid strategy)");

  test_invalid_flags();
  puts("test_header: PASS (invalid flags)");

  test_invalid_strategy_level_combo();
  puts("test_header: PASS (invalid strategy/level)");

  test_truncated_header();
  puts("test_header: PASS (truncated header)");

  test_crc_flag_rejected();
  puts("test_header: PASS (crc rejected)");

  test_declared_payload_shorter_than_actual();
  puts("test_header: PASS (declared payload shorter than actual)");

  test_declared_payload_longer_than_actual();
  puts("test_header: PASS (declared payload longer than actual)");

  test_declared_uncompressed_length_mismatch();
  puts("test_header: PASS (uncompressed length mismatch)");

  test_trailing_bytes_rejected();
  puts("test_header: PASS (trailing bytes rejected)");

  puts("test_header: PASS (all)");
  return 0;
}
