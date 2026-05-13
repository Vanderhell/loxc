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

int main(void) {
  test_roundtrip();
  test_unknown_codepoint();
  puts("test_matrix: PASS");
  return 0;
}
