#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc_matrix.h"

static void fill_test_matrix(loxc_matrix_t *m, loxc_matrix_value_t *data) {
  m->levels = 2;
  m->level[0].bits = 2;
  m->level[0].dims = 1;
  m->level[0].size[0] = 4;
  m->level[0].size[1] = 0;
  m->level[0].size[2] = 0;

  m->level[1].bits = 2;
  m->level[1].dims = 1;
  m->level[1].size[0] = 4;
  m->level[1].size[1] = 0;
  m->level[1].size[2] = 0;

  /* 16 leaves, row-major: (idx0 * 4 + idx1). */
  for (int i = 0; i < 16; i++) {
    data[i].codepoint = (uint32_t)('a' + i);
    data[i].type = LOXC_TYPE_CHAR;
  }
  m->data = data;
  m->data_len = 16;
}

static void test_roundtrip(void) {
  loxc_matrix_value_t data[16];
  loxc_matrix_t m;
  fill_test_matrix(&m, data);

  for (int i = 0; i < 16; i++) {
    uint8_t buf[1];
    memset(buf, 0, sizeof(buf));

    loxc_writer_t w;
    assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
    assert(loxc_matrix_encode(&m, (uint32_t)('a' + i), &w) == LOXC_OK);
    assert(loxc_writer_flush(&w) == LOXC_OK);

    loxc_reader_t r;
    assert(loxc_reader_init(&r, buf, loxc_writer_size(&w)) == LOXC_OK);

    loxc_matrix_value_t out;
    assert(loxc_matrix_decode(&m, &r, &out) == LOXC_OK);
    assert(out.codepoint == (uint32_t)('a' + i));
    assert(out.type == LOXC_TYPE_CHAR);
  }
}

static void test_unknown_codepoint(void) {
  loxc_matrix_value_t data[16];
  loxc_matrix_t m;
  fill_test_matrix(&m, data);

  uint8_t buf[1];
  memset(buf, 0, sizeof(buf));
  loxc_writer_t w;
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);

  assert(loxc_matrix_encode(&m, (uint32_t)'Z', &w) == LOXC_ERR_INVALID_MAGIC);
}

static void test_reject_data_len_exceeds_capacity(void) {
  loxc_matrix_value_t data[17];
  loxc_matrix_t m;
  uint8_t buf[1];
  loxc_writer_t w;
  loxc_reader_t r;
  loxc_matrix_value_t out;

  fill_test_matrix(&m, data);
  m.data_len = 17u;

  memset(buf, 0, sizeof(buf));
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_matrix_encode(&m, (uint32_t)'a', &w) == LOXC_ERR_INVALID_FORMAT);
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_matrix_decode(&m, &r, &out) == LOXC_ERR_INVALID_FORMAT);
}

static void test_reject_level_multiplication_overflow(void) {
  loxc_matrix_t m;
  loxc_matrix_value_t data[1];
  uint8_t buf[1];
  loxc_writer_t w;
  loxc_reader_t r;
  loxc_matrix_value_t out;

  memset(&m, 0, sizeof(m));
  m.levels = 8;
  for (uint8_t i = 0; i < m.levels; i++) {
    m.level[i].bits = 21;
    m.level[i].dims = 3;
    m.level[i].size[0] = 128;
    m.level[i].size[1] = 128;
    m.level[i].size[2] = 128;
  }
  data[0].codepoint = 'a';
  data[0].type = LOXC_TYPE_CHAR;
  m.data = data;
  m.data_len = 1u;

  memset(buf, 0, sizeof(buf));
  assert(loxc_writer_init(&w, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_matrix_encode(&m, (uint32_t)'a', &w) == LOXC_ERR_OVERFLOW);
  assert(loxc_reader_init(&r, buf, sizeof(buf)) == LOXC_OK);
  assert(loxc_matrix_decode(&m, &r, &out) == LOXC_ERR_OVERFLOW);
}

int main(void) {
  test_roundtrip();
  test_unknown_codepoint();
  test_reject_data_len_exceeds_capacity();
  test_reject_level_multiplication_overflow();
  puts("test_matrix: PASS");
  return 0;
}
