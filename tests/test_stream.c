#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc_stream.h"
#include "loxc_types.h"

static void test_write8_read8(void) {
  uint8_t buf[8];
  memset(buf, 0xAA, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
  for (int i = 0; i < 8; i++) {
    assert(loxc_write_bit(&w, (uint8_t)(i & 1)) == LOXC_OK);
  }
  assert(loxc_writer_flush(&w) == LOXC_OK);

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  for (int i = 0; i < 8; i++) {
    uint8_t b = 0xFF;
    assert(loxc_read_bit(&r, &b) == LOXC_OK);
    assert(b == (uint8_t)(i & 1));
  }
  assert(loxc_reader_eof(&r) != 0);
}

static void test_mixed_lengths(void) {
  uint8_t buf[8];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  assert(loxc_write_bits(&w, 0b101u, 3) == LOXC_OK);
  assert(loxc_write_bits(&w, 0b11001u, 5) == LOXC_OK);
  assert(loxc_write_bits(&w, 0b1010101u, 7) == LOXC_OK);
  assert(loxc_writer_flush(&w) == LOXC_OK);

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  uint32_t v = 0;
  assert(loxc_read_bits(&r, 3, &v) == LOXC_OK);
  assert(v == 0b101u);
  assert(loxc_read_bits(&r, 5, &v) == LOXC_OK);
  assert(v == 0b11001u);
  assert(loxc_read_bits(&r, 7, &v) == LOXC_OK);
  assert(v == 0b1010101u);

  /* We flushed to full bytes, so there is 1 padding bit remaining (0). */
  assert(loxc_reader_eof(&r) == 0);
  {
    uint8_t pad = 1;
    assert(loxc_read_bit(&r, &pad) == LOXC_OK);
    assert(pad == 0);
  }
  assert(loxc_reader_eof(&r) != 0);
}

static void test_boundary_exact_capacity(void) {
  uint8_t buf[1];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_write_bits(&w, 0xFFu, 8) == LOXC_OK);
  assert(loxc_writer_flush(&w) == LOXC_OK);
  assert(loxc_writer_size(&w) == 1);

  assert(loxc_write_bit(&w, 1) == LOXC_ERR_OVERFLOW);
}

static void test_eof_detection(void) {
  uint8_t buf[1];
  memset(buf, 0, sizeof(buf));

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, 1) == LOXC_OK);

  uint32_t v = 0;
  assert(loxc_read_bits(&r, 8, &v) == LOXC_OK);
  assert(loxc_reader_eof(&r) != 0);

  uint8_t b = 0;
  assert(loxc_read_bit(&r, &b) == LOXC_ERR_TRUNCATED);
}

static void test_write_bytes_read_bytes_roundtrip(void) {
  const uint8_t data[] = { 0x00, 0x01, 0x7F, 0x80, 0xFF, 0x42, 0x10 };

  uint8_t buf[64];
  memset(buf, 0, sizeof(buf));

  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_write_bytes(&w, data, sizeof(data)) == LOXC_OK);
  assert(loxc_writer_flush(&w) == LOXC_OK);

  loxc_reader_t r;
  assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

  uint8_t out[sizeof(data)];
  memset(out, 0, sizeof(out));
  assert(loxc_read_bytes(&r, out, sizeof(out)) == LOXC_OK);
  assert(memcmp(out, data, sizeof(data)) == 0);
  assert(loxc_reader_eof(&r) != 0);
}

int main(void) {
  test_write8_read8();
  puts("test_stream: PASS (write8/read8)");

  test_mixed_lengths();
  puts("test_stream: PASS (mixed lengths)");

  test_boundary_exact_capacity();
  puts("test_stream: PASS (boundary)");

  test_eof_detection();
  puts("test_stream: PASS (eof)");

  test_write_bytes_read_bytes_roundtrip();
  puts("test_stream: PASS (write_bytes/read_bytes)");

  puts("test_stream: PASS");
  return 0;
}
