#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "loxc_dict.h"
#include "loxc_stream.h"
#include "loxc_types.h"

static int dict_has_word(const loxc_dict_t *d, const char *w) {
  for (size_t i = 0; i < d->count; i++) {
    const loxc_dict_entry_t *e = &d->entries[i];
    if (e->ref_id == 0xFFFFu) continue;
    if (strcmp(e->word, w) == 0) return 1;
  }
  return 0;
}

static const loxc_dict_entry_t *dict_find_any(const loxc_dict_t *d,
                                             const char *w) {
  for (size_t i = 0; i < d->count; i++) {
    if (strcmp(d->entries[i].word, w) == 0) return &d->entries[i];
  }
  return NULL;
}

static void test_analyze_repeated_long_word(void) {
  const char *word = "getUserData";
  char buf[512];
  buf[0] = '\0';
  for (int i = 0; i < 10; i++) {
    strcat(buf, word);
    strcat(buf, " ");
  }

  loxc_dict_t d;
  memset(&d, 0, sizeof(d));
  assert(loxc_dict_analyze(buf, strlen(buf), &d) == LOXC_OK);

  assert(dict_has_word(&d, word));
  loxc_dict_free(&d);
}

static void test_analyze_short_word_not_worth_it(void) {
  char buf[1024];
  buf[0] = '\0';
  for (int i = 0; i < 50; i++) {
    strcat(buf, "if ");
  }

  loxc_dict_t d;
  memset(&d, 0, sizeof(d));
  assert(loxc_dict_analyze(buf, strlen(buf), &d) == LOXC_OK);

  assert(!dict_has_word(&d, "if"));
  loxc_dict_free(&d);
}

static void test_gain_formula_known(void) {
  const char *word = "abcdef";
  char buf[512];
  buf[0] = '\0';
  for (int i = 0; i < 3; i++) {
    strcat(buf, word);
    strcat(buf, " ");
  }

  loxc_dict_t d;
  memset(&d, 0, sizeof(d));
  assert(loxc_dict_analyze(buf, strlen(buf), &d) == LOXC_OK);

  const loxc_dict_entry_t *e = dict_find_any(&d, word);
  assert(e != NULL);
  /* avg_bits_per_char=8, overhead=8, ref_bits=8
     word_len=6 => bits_word=48, bits_in_dict=56
     count=3 => gain=3*48 - (3*8 + 56) = 144 - (24+56)=64
  */
  assert(e->gain == 64);
  loxc_dict_free(&d);
}

static void test_encode_decode_roundtrip(void) {
  const char *s = "getUserData getUserData getUserData getUserData "
                  "getUserData getUserData getUserData getUserData "
                  "getUserData getUserData ";

  loxc_dict_t d;
  memset(&d, 0, sizeof(d));
  assert(loxc_dict_analyze(s, strlen(s), &d) == LOXC_OK);

  uint8_t out[1024];
  loxc_writer_t w;
  assert(loxc_writer_init(&w, out, sizeof(out)) == LOXC_OK);
  assert(loxc_dict_encode(&d, 0x01, &w) == LOXC_OK);
  assert(loxc_writer_flush(&w) == LOXC_OK);

  loxc_dict_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  loxc_reader_t r;
  assert(loxc_reader_init(&r, out, loxc_writer_size(&w)) == LOXC_OK);
  assert(loxc_dict_decode(&r, 0x01, &decoded) == LOXC_OK);

  assert(dict_has_word(&decoded, "getUserData"));

  loxc_dict_free(&decoded);
  loxc_dict_free(&d);
}

int main(void) {
  test_analyze_repeated_long_word();
  puts("test_dict: PASS (repeated long word)");

  test_analyze_short_word_not_worth_it();
  puts("test_dict: PASS (short word excluded)");

  test_gain_formula_known();
  puts("test_dict: PASS (gain formula)");

  test_encode_decode_roundtrip();
  puts("test_dict: PASS (encode/decode)");

  puts("test_dict: PASS");
  return 0;
}
