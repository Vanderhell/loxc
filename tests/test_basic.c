#include <assert.h>
#include <stdio.h>

#include "loxc_base.h"

static void test_magic(void) {
  const uint8_t *m = loxc_magic_bytes();
  assert(m != NULL);
  assert(m[0] == 'L');
  assert(m[1] == 'X');
  assert(m[2] == 'C');
  assert(m[3] == 0x00);
}

static void test_err_codes(void) {
  assert(LOXC_OK == 0);
  assert(LOXC_ERR_NULL != LOXC_OK);
  assert(LOXC_ERR_TRUNCATED != LOXC_OK);
  assert(LOXC_ERR_OVERFLOW != LOXC_OK);
  assert(LOXC_ERR_INVALID_MAGIC != LOXC_OK);
}

int main(void) {
  test_magic();
  test_err_codes();
  puts("test_basic: PASS");
  return 0;
}

