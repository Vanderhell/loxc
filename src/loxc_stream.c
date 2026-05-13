#include "loxc_stream.h"

#include "loxc_types.h"

static uint8_t loxc__bit_to_u1(uint8_t bit) {
  return (uint8_t)(bit ? 1u : 0u);
}

int loxc_writer_init(loxc_writer_t *w, uint8_t *buf, size_t cap) {
  if (w == NULL || buf == NULL) return LOXC_ERR_NULL;

  w->buf = buf;
  w->cap = cap;
  w->byte_pos = 0;
  w->bit_pos = 0;

  if (cap > 0) {
    w->buf[0] = 0;
  }
  return LOXC_OK;
}

int loxc_write_bit(loxc_writer_t *w, uint8_t bit) {
  if (w == NULL || w->buf == NULL) return LOXC_ERR_NULL;
  if (w->byte_pos >= w->cap) return LOXC_ERR_OVERFLOW;

  bit = loxc__bit_to_u1(bit);
  w->buf[w->byte_pos] =
      (uint8_t)(w->buf[w->byte_pos] | (uint8_t)(bit << (7u - w->bit_pos)));

  w->bit_pos++;
  if (w->bit_pos == 8) {
    w->bit_pos = 0;
    w->byte_pos++;
    if (w->byte_pos < w->cap) {
      w->buf[w->byte_pos] = 0;
    }
  }
  return LOXC_OK;
}

int loxc_write_bits(loxc_writer_t *w, uint32_t bits, uint8_t n) {
  if (w == NULL) return LOXC_ERR_NULL;
  if (n == 0) return LOXC_OK;
  if (n > 32) return LOXC_ERR_OVERFLOW;

  for (uint8_t i = 0; i < n; i++) {
    uint8_t shift = (uint8_t)(n - 1u - i);
    uint8_t b = (uint8_t)((bits >> shift) & 1u);
    int rc = loxc_write_bit(w, b);
    if (rc != LOXC_OK) return rc;
  }
  return LOXC_OK;
}

int loxc_write_bytes(loxc_writer_t *w, const uint8_t *data, size_t len) {
  if (w == NULL) return LOXC_ERR_NULL;
  if (len == 0) return LOXC_OK;
  if (data == NULL) return LOXC_ERR_NULL;

  for (size_t i = 0; i < len; i++) {
    int rc = loxc_write_bits(w, (uint32_t)data[i], 8);
    if (rc != LOXC_OK) return rc;
  }
  return LOXC_OK;
}

int loxc_writer_flush(loxc_writer_t *w) {
  if (w == NULL || w->buf == NULL) return LOXC_ERR_NULL;
  if (w->bit_pos == 0) return LOXC_OK;

  w->bit_pos = 0;
  w->byte_pos++;
  if (w->byte_pos > w->cap) return LOXC_ERR_OVERFLOW;
  return LOXC_OK;
}

size_t loxc_writer_size(const loxc_writer_t *w) {
  if (w == NULL) return 0;
  return w->byte_pos + (w->bit_pos ? 1u : 0u);
}

int loxc_reader_init(loxc_reader_t *r, const uint8_t *buf, size_t len) {
  if (r == NULL || buf == NULL) return LOXC_ERR_NULL;

  r->buf = buf;
  r->len = len;
  r->byte_pos = 0;
  r->bit_pos = 0;
  return LOXC_OK;
}

int loxc_reader_eof(const loxc_reader_t *r) {
  if (r == NULL) return 1;
  return r->byte_pos >= r->len;
}

int loxc_read_bit(loxc_reader_t *r, uint8_t *out) {
  if (r == NULL || r->buf == NULL || out == NULL) return LOXC_ERR_NULL;
  if (loxc_reader_eof(r)) return LOXC_ERR_TRUNCATED;

  uint8_t b = (uint8_t)((r->buf[r->byte_pos] >> (7u - r->bit_pos)) & 1u);
  *out = b;

  r->bit_pos++;
  if (r->bit_pos == 8) {
    r->bit_pos = 0;
    r->byte_pos++;
  }
  return LOXC_OK;
}

int loxc_read_bits(loxc_reader_t *r, uint8_t n, uint32_t *out) {
  if (r == NULL || out == NULL) return LOXC_ERR_NULL;
  if (n == 0) {
    *out = 0;
    return LOXC_OK;
  }
  if (n > 32) return LOXC_ERR_OVERFLOW;

  uint32_t v = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t b = 0;
    int rc = loxc_read_bit(r, &b);
    if (rc != LOXC_OK) return rc;
    v = (uint32_t)((v << 1u) | (uint32_t)(b & 1u));
  }
  *out = v;
  return LOXC_OK;
}

int loxc_read_bytes(loxc_reader_t *r, uint8_t *out, size_t len) {
  if (r == NULL) return LOXC_ERR_NULL;
  if (len == 0) return LOXC_OK;
  if (out == NULL) return LOXC_ERR_NULL;

  for (size_t i = 0; i < len; i++) {
    uint32_t v = 0;
    int rc = loxc_read_bits(r, 8, &v);
    if (rc != LOXC_OK) return rc;
    out[i] = (uint8_t)(v & 0xFFu);
  }
  return LOXC_OK;
}
