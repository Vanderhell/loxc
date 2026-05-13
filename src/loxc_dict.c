#include "loxc_dict.h"

#include <stdlib.h>
#include <string.h>

#include "loxc_types.h"

enum {
  LOXC__DICT_ENTRY_OVERHEAD_BITS = 8,
  LOXC__MAX_WORD_LEN = 255,
  LOXC__MIN_DICT_WORD_LEN = 3
};

static int loxc__is_word_char(unsigned char c) {
  /* Keep it simple for now: ASCII letters/digits/_ only. */
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  if (c >= '0' && c <= '9') return 1;
  if (c == '_') return 1;
  return 0;
}

static int loxc__dict_clear(loxc_dict_t *dict) {
  if (dict == NULL) return LOXC_ERR_NULL;
  dict->entries = NULL;
  dict->count = 0;
  dict->ref_bytes = 1;
  return LOXC_OK;
}

void loxc_dict_free(loxc_dict_t *dict) {
  if (dict == NULL) return;
  if (dict->entries != NULL) {
    for (size_t i = 0; i < dict->count; i++) {
      free(dict->entries[i].word);
      dict->entries[i].word = NULL;
    }
    free(dict->entries);
  }
  dict->entries = NULL;
  dict->count = 0;
  dict->ref_bytes = 1;
}

static int loxc__dict_find_or_add(loxc_dict_t *dict, const char *word,
                                 size_t word_len, loxc_dict_entry_t **out_e) {
  if (dict == NULL || word == NULL || out_e == NULL) return LOXC_ERR_NULL;

  for (size_t i = 0; i < dict->count; i++) {
    if (dict->entries[i].word_len != word_len) continue;
    if (memcmp(dict->entries[i].word, word, word_len) == 0) {
      *out_e = &dict->entries[i];
      return LOXC_OK;
    }
  }

  loxc_dict_entry_t *new_entries =
      (loxc_dict_entry_t *)realloc(dict->entries,
                                   (dict->count + 1) * sizeof(loxc_dict_entry_t));
  if (new_entries == NULL) return LOXC_ERR_OVERFLOW;
  dict->entries = new_entries;

  loxc_dict_entry_t *e = &dict->entries[dict->count];
  e->word = (char *)malloc(word_len + 1);
  if (e->word == NULL) return LOXC_ERR_OVERFLOW;
  memcpy(e->word, word, word_len);
  e->word[word_len] = '\0';
  e->word_len = word_len;
  e->count = 0;
  e->gain = 0;
  e->ref_id = 0;

  dict->count++;
  *out_e = e;
  return LOXC_OK;
}

static int32_t loxc__dict_gain_bits(size_t word_len, uint32_t count,
                                   uint32_t avg_bits_per_char,
                                   uint8_t ref_bytes) {
  uint32_t bits_word = (uint32_t)word_len * avg_bits_per_char;
  uint32_t bits_in_dict = bits_word + LOXC__DICT_ENTRY_OVERHEAD_BITS;
  uint32_t bits_ref = (ref_bytes == 1) ? 8u : 16u;

  int32_t gain =
      (int32_t)((uint64_t)count * (uint64_t)bits_word) -
      (int32_t)((uint64_t)count * (uint64_t)bits_ref + (uint64_t)bits_in_dict);
  return gain;
}

static int loxc__write_u8(loxc_writer_t *w, uint8_t v) {
  return loxc_write_bits(w, (uint32_t)v, 8);
}

static int loxc__read_u8(loxc_reader_t *r, uint8_t *out) {
  uint32_t v = 0;
  int rc = loxc_read_bits(r, 8, &v);
  if (rc != LOXC_OK) return rc;
  *out = (uint8_t)v;
  return LOXC_OK;
}

static int loxc__write_u16_be(loxc_writer_t *w, uint16_t v) {
  int rc = loxc__write_u8(w, (uint8_t)((v >> 8) & 0xFFu));
  if (rc != LOXC_OK) return rc;
  return loxc__write_u8(w, (uint8_t)(v & 0xFFu));
}

static int loxc__read_u16_be(loxc_reader_t *r, uint16_t *out) {
  uint8_t hi = 0, lo = 0;
  int rc = loxc__read_u8(r, &hi);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &lo);
  if (rc != LOXC_OK) return rc;
  *out = (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
  return LOXC_OK;
}

int loxc_dict_analyze(const char *input, size_t len, loxc_dict_t *dict) {
  if (input == NULL || dict == NULL) return LOXC_ERR_NULL;
  loxc_dict_free(dict);
  loxc__dict_clear(dict);

  const uint32_t avg_bits_per_char = 8; /* conservative default for now */

  size_t i = 0;
  while (i < len) {
    unsigned char c = (unsigned char)input[i];
    if (!loxc__is_word_char(c)) {
      i++;
      continue;
    }

    size_t start = i;
    while (i < len && loxc__is_word_char((unsigned char)input[i])) {
      i++;
    }
    size_t wlen = i - start;
    if (wlen == 0 || wlen > LOXC__MAX_WORD_LEN) continue;

    loxc_dict_entry_t *e = NULL;
    int rc = loxc__dict_find_or_add(dict, input + start, wlen, &e);
    if (rc != LOXC_OK) return rc;
    e->count++;
  }

  /* First pass: assume 1-byte refs unless dict grows beyond 256. */
  dict->ref_bytes = (dict->count > 256) ? 2 : 1;

  /* Compute gain and assign ref ids only to positive-gain entries. */
  uint16_t next_ref = 0;
  for (size_t j = 0; j < dict->count; j++) {
    loxc_dict_entry_t *e = &dict->entries[j];
    e->gain = loxc__dict_gain_bits(e->word_len, e->count, avg_bits_per_char,
                                  dict->ref_bytes);
    if (e->word_len < LOXC__MIN_DICT_WORD_LEN) {
      e->ref_id = 0xFFFFu;
    } else if (e->gain > 0) {
      e->ref_id = next_ref++;
    } else {
      e->ref_id = 0xFFFFu;
    }
  }

  return LOXC_OK;
}

int loxc_dict_encode(const loxc_dict_t *dict, uint8_t module_id,
                     loxc_writer_t *w) {
  if (dict == NULL || w == NULL) return LOXC_ERR_NULL;

  /* Format:
     u8  module_id
     u8  ref_bytes
     u16 entry_count (number of stored entries)
     repeated:
       u16 ref_id
       u8  word_len
       bytes[word_len]
  */
  int rc = loxc__write_u8(w, module_id);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, dict->ref_bytes);
  if (rc != LOXC_OK) return rc;

  /* Count only positive-gain entries */
  uint16_t stored = 0;
  for (size_t i = 0; i < dict->count; i++) {
    if (dict->entries[i].ref_id != 0xFFFFu) stored++;
  }

  rc = loxc__write_u16_be(w, stored);
  if (rc != LOXC_OK) return rc;

  for (size_t i = 0; i < dict->count; i++) {
    const loxc_dict_entry_t *e = &dict->entries[i];
    if (e->ref_id == 0xFFFFu) continue;

    if (e->word_len == 0 || e->word_len > LOXC__MAX_WORD_LEN)
      return LOXC_ERR_INVALID_MAGIC;

    rc = loxc__write_u16_be(w, e->ref_id);
    if (rc != LOXC_OK) return rc;
    rc = loxc__write_u8(w, (uint8_t)e->word_len);
    if (rc != LOXC_OK) return rc;

    for (size_t k = 0; k < e->word_len; k++) {
      rc = loxc__write_u8(w, (uint8_t)e->word[k]);
      if (rc != LOXC_OK) return rc;
    }
  }

  return LOXC_OK;
}

int loxc_dict_decode(loxc_reader_t *r, uint8_t module_id, loxc_dict_t *dict) {
  if (r == NULL || dict == NULL) return LOXC_ERR_NULL;
  loxc_dict_free(dict);
  loxc__dict_clear(dict);

  uint8_t got_module = 0;
  int rc = loxc__read_u8(r, &got_module);
  if (rc != LOXC_OK) return rc;
  if (got_module != module_id) return LOXC_ERR_INVALID_MAGIC;

  rc = loxc__read_u8(r, &dict->ref_bytes);
  if (rc != LOXC_OK) return rc;
  if (dict->ref_bytes != 1 && dict->ref_bytes != 2) return LOXC_ERR_INVALID_MAGIC;

  uint16_t stored = 0;
  rc = loxc__read_u16_be(r, &stored);
  if (rc != LOXC_OK) return rc;

  if (stored == 0) return LOXC_OK;

  dict->entries = (loxc_dict_entry_t *)calloc(stored, sizeof(loxc_dict_entry_t));
  if (dict->entries == NULL) return LOXC_ERR_OVERFLOW;
  dict->count = stored;

  for (uint16_t i = 0; i < stored; i++) {
    loxc_dict_entry_t *e = &dict->entries[i];
    rc = loxc__read_u16_be(r, &e->ref_id);
    if (rc != LOXC_OK) return rc;

    uint8_t wlen = 0;
    rc = loxc__read_u8(r, &wlen);
    if (rc != LOXC_OK) return rc;
    if (wlen == 0) return LOXC_ERR_INVALID_MAGIC;

    e->word = (char *)malloc((size_t)wlen + 1);
    if (e->word == NULL) return LOXC_ERR_OVERFLOW;
    e->word_len = (size_t)wlen;

    for (uint8_t k = 0; k < wlen; k++) {
      uint8_t b = 0;
      rc = loxc__read_u8(r, &b);
      if (rc != LOXC_OK) return rc;
      e->word[k] = (char)b;
    }
    e->word[wlen] = '\0';
    e->count = 0;
    e->gain = 0;
  }

  return LOXC_OK;
}
