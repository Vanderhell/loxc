#include "loxc_dict.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "loxc_types.h"

enum {
  LOXC__DICT_ENTRY_OVERHEAD_BITS = 8,
  LOXC__MAX_WORD_LEN = 255,
  LOXC__MIN_DICT_WORD_LEN = 3
};

static int loxc__is_ascii_word_char(unsigned char c) {
  /* Trainer-side heuristic only: dictionary candidates are limited to ASCII
   * identifier-like runs so the codec format itself does not depend on this. */
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  if (c >= '0' && c <= '9') return 1;
  if (c == '_') return 1;
  return 0;
}

static void loxc__dict_clear(loxc_dict_t *dict) {
  if (dict == NULL) return;
  memset(dict, 0, sizeof(*dict));
  dict->ref_bytes = 1u;
}

static int loxc__checked_add_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a > SIZE_MAX - b) return LOXC_ERR_OVERFLOW;
  *out = a + b;
  return LOXC_OK;
}

static int loxc__checked_mul_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a != 0u && b > SIZE_MAX / a) return LOXC_ERR_OVERFLOW;
  *out = a * b;
  return LOXC_OK;
}

static int loxc__checked_u64_mul(uint64_t a, uint64_t b, uint64_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a != 0u && b > UINT64_MAX / a) return LOXC_ERR_OVERFLOW;
  *out = a * b;
  return LOXC_OK;
}

static int64_t loxc__sat_i64_from_u64(uint64_t v) {
  if (v > (uint64_t)INT64_MAX) return INT64_MAX;
  return (int64_t)v;
}

static int loxc__dict_entry_equal(const loxc_dict_entry_t *entry,
                                  const char *word,
                                  size_t word_len) {
  if (entry == NULL || word == NULL) return 0;
  if (entry->word == NULL || entry->word_len != word_len) return 0;
  return memcmp(entry->word, word, word_len) == 0;
}

static int loxc__dict_has_duplicate_word(const loxc_dict_t *dict,
                                         const char *word,
                                         size_t word_len,
                                         size_t skip_idx) {
  if (dict == NULL || word == NULL) return 0;
  for (size_t i = 0; i < dict->count; i++) {
    if (i == skip_idx) continue;
    if (loxc__dict_entry_equal(&dict->entries[i], word, word_len)) return 1;
  }
  return 0;
}

static int loxc__dict_validate_refs(const loxc_dict_t *dict,
                                    uint16_t *out_stored) {
  if (dict == NULL) return LOXC_ERR_NULL;
  const uint32_t max_refs = (dict->ref_bytes == 1u) ? 256u : 65536u;
  uint8_t *seen = NULL;
  uint16_t stored = 0u;

  if (dict->ref_bytes != 1u && dict->ref_bytes != 2u) return LOXC_ERR_INVALID_FORMAT;
  if (dict->count > 0u && dict->entries == NULL) return LOXC_ERR_INVALID_FORMAT;

  seen = (uint8_t *)calloc((size_t)max_refs, sizeof(uint8_t));
  if (seen == NULL) return LOXC_ERR_OVERFLOW;

  for (size_t i = 0; i < dict->count; i++) {
    const loxc_dict_entry_t *e = &dict->entries[i];
    if (e->ref_id == 0xFFFFu) continue;
    if (e->word == NULL || e->word_len == 0u || e->word_len > LOXC__MAX_WORD_LEN) {
      free(seen);
      return LOXC_ERR_INVALID_FORMAT;
    }
    if ((uint32_t)e->ref_id >= max_refs || seen[e->ref_id] != 0u) {
      free(seen);
      return LOXC_ERR_INVALID_FORMAT;
    }
    if (loxc__dict_has_duplicate_word(dict, e->word, e->word_len, i)) {
      free(seen);
      return LOXC_ERR_INVALID_FORMAT;
    }
    seen[e->ref_id] = 1u;
    if (stored == UINT16_MAX) {
      free(seen);
      return LOXC_ERR_OVERFLOW;
    }
    stored++;
  }

  free(seen);
  if (out_stored != NULL) *out_stored = stored;
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
  loxc__dict_clear(dict);
}

static int loxc__dict_find_or_add(loxc_dict_t *dict, const char *word,
                                  size_t word_len, loxc_dict_entry_t **out_e) {
  char *new_word = NULL;
  loxc_dict_entry_t *new_entries = NULL;
  size_t alloc_len = 0u;
  size_t alloc_size = 0u;

  if (dict == NULL || word == NULL || out_e == NULL) return LOXC_ERR_NULL;
  if (word_len == 0u || word_len > LOXC__MAX_WORD_LEN) return LOXC_ERR_INVALID_FORMAT;
  if (dict->count == UINT16_MAX) return LOXC_ERR_OVERFLOW;

  for (size_t i = 0; i < dict->count; i++) {
    if (loxc__dict_entry_equal(&dict->entries[i], word, word_len)) {
      *out_e = &dict->entries[i];
      return LOXC_OK;
    }
  }

  if (loxc__checked_add_size(word_len, 1u, &alloc_len) != LOXC_OK) {
    return LOXC_ERR_OVERFLOW;
  }
  new_word = (char *)malloc(alloc_len);
  if (new_word == NULL) return LOXC_ERR_OVERFLOW;
  memcpy(new_word, word, word_len);
  new_word[word_len] = '\0';

  if (loxc__checked_mul_size(dict->count + 1u, sizeof(loxc_dict_entry_t),
                             &alloc_size) != LOXC_OK) {
    free(new_word);
    return LOXC_ERR_OVERFLOW;
  }
  new_entries = (loxc_dict_entry_t *)realloc(dict->entries, alloc_size);
  if (new_entries == NULL) {
    free(new_word);
    return LOXC_ERR_OVERFLOW;
  }
  dict->entries = new_entries;

  *out_e = &dict->entries[dict->count];
  memset(*out_e, 0, sizeof(**out_e));
  (*out_e)->word = new_word;
  (*out_e)->word_len = word_len;
  (*out_e)->ref_id = 0xFFFFu;
  dict->count++;
  return LOXC_OK;
}

static int64_t loxc__dict_gain_bits(size_t word_len, uint32_t count,
                                    uint32_t avg_bits_per_char,
                                    uint8_t ref_bytes) {
  uint64_t bits_word = (uint64_t)word_len * (uint64_t)avg_bits_per_char;
  uint64_t replaced_bits = 0u;
  uint64_t ref_bits_total = 0u;
  uint64_t kept_bits = 0u;

  if (ref_bytes != 1u && ref_bytes != 2u) return INT64_MIN;
  if (loxc__checked_u64_mul((uint64_t)count, bits_word, &replaced_bits) != LOXC_OK ||
      loxc__checked_u64_mul((uint64_t)count, (ref_bytes == 1u) ? 8u : 16u,
                            &ref_bits_total) != LOXC_OK) {
    return INT64_MAX;
  }
  if (ref_bits_total > UINT64_MAX - (bits_word + LOXC__DICT_ENTRY_OVERHEAD_BITS)) {
    kept_bits = UINT64_MAX;
  } else {
    kept_bits = ref_bits_total + bits_word + LOXC__DICT_ENTRY_OVERHEAD_BITS;
  }

  if (replaced_bits >= kept_bits) {
    return loxc__sat_i64_from_u64(replaced_bits - kept_bits);
  }
  return -loxc__sat_i64_from_u64(kept_bits - replaced_bits);
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
  loxc_dict_t tmp;
  const uint32_t avg_bits_per_char = 8u;
  uint16_t next_ref = 0u;
  size_t i = 0u;

  if (input == NULL || dict == NULL) return LOXC_ERR_NULL;
  loxc_dict_free(dict);
  loxc__dict_clear(&tmp);

  while (i < len) {
    unsigned char c = (unsigned char)input[i];
    if (!loxc__is_ascii_word_char(c)) {
      i++;
      continue;
    }

    {
      size_t start = i;
      size_t wlen = 0u;
      loxc_dict_entry_t *e = NULL;
      int rc = LOXC_OK;

      while (i < len && loxc__is_ascii_word_char((unsigned char)input[i])) i++;
      wlen = i - start;
      if (wlen == 0u || wlen > LOXC__MAX_WORD_LEN) continue;

      rc = loxc__dict_find_or_add(&tmp, input + start, wlen, &e);
      if (rc != LOXC_OK) {
        loxc_dict_free(&tmp);
        return rc;
      }
      if (e->count == UINT32_MAX) {
        loxc_dict_free(&tmp);
        return LOXC_ERR_OVERFLOW;
      }
      e->count++;
    }
  }

  tmp.ref_bytes = (tmp.count > 256u) ? 2u : 1u;
  for (size_t j = 0; j < tmp.count; j++) {
    loxc_dict_entry_t *e = &tmp.entries[j];
    e->gain = loxc__dict_gain_bits(e->word_len, e->count, avg_bits_per_char,
                                   tmp.ref_bytes);
    if (e->word_len < LOXC__MIN_DICT_WORD_LEN) {
      e->ref_id = 0xFFFFu;
    } else if (e->gain > 0) {
      if ((tmp.ref_bytes == 1u && next_ref >= 256u) || next_ref == UINT16_MAX) {
        loxc_dict_free(&tmp);
        return LOXC_ERR_OVERFLOW;
      }
      e->ref_id = next_ref++;
    } else {
      e->ref_id = 0xFFFFu;
    }
  }

  *dict = tmp;
  return LOXC_OK;
}

int loxc_dict_encode(const loxc_dict_t *dict, uint8_t module_id,
                     loxc_writer_t *w) {
  int rc = LOXC_OK;
  uint16_t stored = 0u;

  if (dict == NULL || w == NULL) return LOXC_ERR_NULL;
  rc = loxc__dict_validate_refs(dict, &stored);
  if (rc != LOXC_OK) return rc;

  rc = loxc__write_u8(w, module_id);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, dict->ref_bytes);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u16_be(w, stored);
  if (rc != LOXC_OK) return rc;

  for (size_t i = 0; i < dict->count; i++) {
    const loxc_dict_entry_t *e = &dict->entries[i];
    if (e->ref_id == 0xFFFFu) continue;

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
  loxc_dict_t tmp;
  uint8_t *seen_refs = NULL;
  uint8_t got_module = 0;
  uint16_t stored = 0u;
  int rc = LOXC_OK;

  if (r == NULL || dict == NULL) return LOXC_ERR_NULL;
  loxc_dict_free(dict);
  loxc__dict_clear(&tmp);

  rc = loxc__read_u8(r, &got_module);
  if (rc != LOXC_OK) return rc;
  if (got_module != module_id) return LOXC_ERR_INVALID_MAGIC;

  rc = loxc__read_u8(r, &tmp.ref_bytes);
  if (rc != LOXC_OK) return rc;
  if (tmp.ref_bytes != 1u && tmp.ref_bytes != 2u) return LOXC_ERR_INVALID_MAGIC;

  rc = loxc__read_u16_be(r, &stored);
  if (rc != LOXC_OK) return rc;
  if (stored == 0u) {
    dict->ref_bytes = tmp.ref_bytes;
    return LOXC_OK;
  }

  tmp.entries = (loxc_dict_entry_t *)calloc((size_t)stored, sizeof(loxc_dict_entry_t));
  if (tmp.entries == NULL) return LOXC_ERR_OVERFLOW;
  tmp.count = stored;

  seen_refs = (uint8_t *)calloc((tmp.ref_bytes == 1u) ? 256u : 65536u, sizeof(uint8_t));
  if (seen_refs == NULL) {
    loxc_dict_free(&tmp);
    return LOXC_ERR_OVERFLOW;
  }

  for (uint16_t i = 0; i < stored; i++) {
    loxc_dict_entry_t *e = &tmp.entries[i];
    uint8_t wlen = 0u;

    rc = loxc__read_u16_be(r, &e->ref_id);
    if (rc != LOXC_OK) goto fail;
    if ((tmp.ref_bytes == 1u && e->ref_id >= 256u) || seen_refs[e->ref_id] != 0u) {
      rc = LOXC_ERR_INVALID_MAGIC;
      goto fail;
    }
    seen_refs[e->ref_id] = 1u;

    rc = loxc__read_u8(r, &wlen);
    if (rc != LOXC_OK) goto fail;
    if (wlen == 0u) {
      rc = LOXC_ERR_INVALID_MAGIC;
      goto fail;
    }

    e->word = (char *)malloc((size_t)wlen + 1u);
    if (e->word == NULL) {
      rc = LOXC_ERR_OVERFLOW;
      goto fail;
    }
    e->word_len = (size_t)wlen;

    for (uint8_t k = 0; k < wlen; k++) {
      uint8_t b = 0u;
      rc = loxc__read_u8(r, &b);
      if (rc != LOXC_OK) goto fail;
      e->word[k] = (char)b;
    }
    e->word[wlen] = '\0';
    if (loxc__dict_has_duplicate_word(&tmp, e->word, e->word_len, (size_t)i)) {
      rc = LOXC_ERR_INVALID_MAGIC;
      goto fail;
    }
  }

  free(seen_refs);
  *dict = tmp;
  return LOXC_OK;

fail:
  free(seen_refs);
  loxc_dict_free(&tmp);
  return rc;
}
