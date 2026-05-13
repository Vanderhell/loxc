#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc_base.h"

static void test_roundtrip_no_crc(void) {
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  loxc_header_t h;
  h.module_id = 0x01;
  h.version = 2;  /* v2 format */
  h.flags = 0;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.data_len = 0x1234;
  h.level_count = 0;
  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;
  h.crc32 = 0;

  assert(loxc_header_write(&w, &h) == LOXC_OK);
  assert(loxc_writer_size(&w) == 15);  /* v2 header without CRC */

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_OK);
  assert(out.module_id == h.module_id);
  assert(out.version == 2);  /* v2 */
  assert(out.flags == h.flags);
  assert(out.strategy_id == LOXC_STRATEGY_FLAT_FIXED_WIDTH);
  assert(out.data_len == h.data_len);
  assert(out.level_count == 0);
  assert(out.crc32 == 0);
  assert(loxc_reader_eof(&r) != 0);
}

static void test_roundtrip_with_crc(void) {
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  const uint8_t data[] = { 'a', 'b', 'c', 'd' };
  const uint32_t crc = loxc_crc32(data, sizeof(data));

  loxc_header_t h;
  h.module_id = 0x02;
  h.version = 2;  /* v2 format */
  h.flags = LOXC_FLAG_CRC;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.data_len = (uint16_t)sizeof(data);
  h.level_count = 0;
  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;
  h.crc32 = crc;

  assert(loxc_header_write(&w, &h) == LOXC_OK);
  assert(loxc_writer_size(&w) == 19);  /* v2 header with CRC: 15 + 4 */

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_OK);
  assert(out.module_id == h.module_id);
  assert(out.version == 2);  /* v2 */
  assert(out.flags == h.flags);
  assert(out.strategy_id == LOXC_STRATEGY_FLAT_FIXED_WIDTH);
  assert(out.data_len == h.data_len);
  assert(out.level_count == 0);
  assert(out.crc32 == h.crc32);
  assert(loxc_reader_eof(&r) != 0);
}

static void test_invalid_magic(void) {
  uint8_t buf[8];
  memset(buf, 0, sizeof(buf));
  buf[0] = 'B';
  buf[1] = 'A';
  buf[2] = 'D';
  buf[3] = 0x01;
  buf[4] = 0x01;
  buf[5] = 0;
  buf[6] = 0;
  buf[7] = 0;

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_ERR_INVALID_MAGIC);
}

static void test_truncated(void) {
  uint8_t buf[14];  /* one byte short of v2 header (15 bytes) */
  memset(buf, 0, sizeof(buf));
  buf[0] = 'L';
  buf[1] = 'X';
  buf[2] = 'C';
  buf[3] = 0x01;
  buf[4] = 2;   /* version 2 */
  buf[5] = 0;   /* flags */
  buf[6] = 0;   /* strategy_id */
  buf[7] = 0;   /* data_len high byte */
  buf[8] = 0;   /* data_len low byte */
  buf[9] = 0;   /* level_count high byte */
  buf[10] = 0;  /* level_count low byte */
  buf[11] = 0;  /* reserved[0] */
  buf[12] = 0;  /* reserved[1] */
  buf[13] = 0;  /* reserved[2] — missing reserved[3] */

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_ERR_TRUNCATED);
}

static void test_strategy_hier8(void) {
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  loxc_header_t h;
  h.module_id = 0x03;
  h.version = 2;
  h.flags = 0;
  h.strategy_id = LOXC_STRATEGY_HIERARCHICAL_8;
  h.data_len = 1000;
  h.level_count = 3;
  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;
  h.crc32 = 0;

  assert(loxc_header_write(&w, &h) == LOXC_OK);

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_OK);
  assert(out.module_id == 0x03);
  assert(out.strategy_id == LOXC_STRATEGY_HIERARCHICAL_8);
  assert(out.level_count == 3);
  assert(out.data_len == 1000);
}

static void test_endianness(void) {
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  loxc_header_t h;
  h.module_id = 0x42;
  h.version = 2;
  h.flags = LOXC_FLAG_CRC;
  h.strategy_id = LOXC_STRATEGY_HIERARCHICAL_4;
  h.data_len = 0xABCD;
  h.level_count = 0x1234;
  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;
  h.crc32 = 0x12345678;

  assert(loxc_header_write(&w, &h) == LOXC_OK);

  /* Manual byte verification for little-endian encoding */
  assert(buf[0] == 'L');
  assert(buf[1] == 'X');
  assert(buf[2] == 'C');
  assert(buf[3] == 0x42);  /* module_id */
  assert(buf[4] == 2);     /* version */
  assert(buf[5] == LOXC_FLAG_CRC);  /* flags */
  assert(buf[6] == LOXC_STRATEGY_HIERARCHICAL_4);  /* strategy_id */
  assert(buf[7] == 0xCD && buf[8] == 0xAB);  /* data_len 0xABCD little-endian: CD, AB */
  assert(buf[9] == 0x34 && buf[10] == 0x12);  /* level_count 0x1234 little-endian: 34, 12 */
  assert(buf[11] == 0 && buf[12] == 0 && buf[13] == 0 && buf[14] == 0);  /* reserved[4] */
  assert(buf[15] == 0x78 && buf[16] == 0x56 && buf[17] == 0x34 && buf[18] == 0x12);  /* crc32 0x12345678 little-endian: 78, 56, 34, 12 */
}

static void test_reserved_ignored(void) {
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  loxc_header_t h;
  h.module_id = 0x05;
  h.version = 2;
  h.flags = 0;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.data_len = 512;
  h.level_count = 0;
  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;
  h.crc32 = 0;

  assert(loxc_header_write(&w, &h) == LOXC_OK);

  /* Manually corrupt reserved bytes in buffer */
  buf[11] = 0xAA;
  buf[12] = 0xBB;
  buf[13] = 0xCC;
  buf[14] = 0xDD;

  /* Read should still succeed, reserved bytes are ignored */
  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  loxc_header_t out;
  assert(loxc_header_read(&r, &out) == LOXC_OK);
  assert(out.module_id == 0x05);
  assert(out.data_len == 512);
  /* Verify that reserved bytes were stored but don't affect validity */
  assert(out.reserved[0] == 0xAA);
  assert(out.reserved[1] == 0xBB);
  assert(out.reserved[2] == 0xCC);
  assert(out.reserved[3] == 0xDD);
}

int main(void) {
  test_roundtrip_no_crc();
  puts("test_header: PASS (no crc)");

  test_roundtrip_with_crc();
  puts("test_header: PASS (with crc)");

  test_invalid_magic();
  puts("test_header: PASS (invalid magic)");

  test_truncated();
  puts("test_header: PASS (truncated)");

  test_strategy_hier8();
  puts("test_header: PASS (strategy hierarchical_8)");

  test_endianness();
  puts("test_header: PASS (endianness)");

  test_reserved_ignored();
  puts("test_header: PASS (reserved ignored)");

  puts("test_header: PASS (all)");
  return 0;
}

