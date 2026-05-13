#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc_plain.h"
#include "loxc_types.h"

static void test_plain_roundtrip(void) {
  const char *s = "the theatre is on the one end.\n";
  const size_t in_len = strlen(s);

  uint8_t encoded[512];
  size_t enc_len = 0;
  assert(loxc_plain_encode((const uint8_t *)s, in_len, encoded, sizeof(encoded),
                           &enc_len) == LOXC_OK);
  assert(enc_len > 8);

  uint8_t decoded[512];
  size_t dec_len = 0;
  assert(loxc_plain_decode(encoded, enc_len, decoded, sizeof(decoded),
                           &dec_len) == LOXC_OK);

  assert(dec_len == in_len);
  assert(memcmp(decoded, s, in_len) == 0);
}

static void test_plain_unknown_char(void) {
  const uint8_t in[] = {0x01, 0x02, 0x03};
  uint8_t out[64];
  size_t out_len = 0;
  assert(loxc_plain_encode(in, sizeof(in), out, sizeof(out), &out_len) ==
         LOXC_ERR_INVALID_MAGIC);
}

int main(void) {
  test_plain_roundtrip();
  puts("test_plain: PASS (roundtrip)");

  test_plain_unknown_char();
  puts("test_plain: PASS (unknown)");

  puts("test_plain: PASS");
  return 0;
}

