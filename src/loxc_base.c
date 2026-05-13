#include "loxc_base.h"

const uint8_t *loxc_magic_bytes(void) {
  return LOXC_MAGIC;
}

static int loxc__writer_require_byte_aligned(const loxc_writer_t *w) {
  if (w == NULL) return LOXC_ERR_NULL;
  return (w->bit_pos == 0) ? LOXC_OK : LOXC_ERR_INVALID_MAGIC;
}

static int loxc__reader_require_byte_aligned(const loxc_reader_t *r) {
  if (r == NULL) return LOXC_ERR_NULL;
  return (r->bit_pos == 0) ? LOXC_OK : LOXC_ERR_INVALID_MAGIC;
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

static int loxc__write_u16_le(loxc_writer_t *w, uint16_t v) {
  int rc = loxc__write_u8(w, (uint8_t)(v & 0xFFu));
  if (rc != LOXC_OK) return rc;
  return loxc__write_u8(w, (uint8_t)((v >> 8) & 0xFFu));
}

static int loxc__read_u16_le(loxc_reader_t *r, uint16_t *out) {
  uint8_t lo = 0, hi = 0;
  int rc = loxc__read_u8(r, &lo);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &hi);
  if (rc != LOXC_OK) return rc;
  *out = (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
  return LOXC_OK;
}

static int loxc__write_u32_le(loxc_writer_t *w, uint32_t v) {
  int rc = loxc__write_u8(w, (uint8_t)(v & 0xFFu));
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, (uint8_t)((v >> 8) & 0xFFu));
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, (uint8_t)((v >> 16) & 0xFFu));
  if (rc != LOXC_OK) return rc;
  return loxc__write_u8(w, (uint8_t)((v >> 24) & 0xFFu));
}

static int loxc__read_u32_le(loxc_reader_t *r, uint32_t *out) {
  uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
  int rc = loxc__read_u8(r, &b0);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &b1);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &b2);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &b3);
  if (rc != LOXC_OK) return rc;
  *out = ((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | (uint32_t)b0;
  return LOXC_OK;
}

int loxc_header_validate(const loxc_header_t *h) {
  if (h == NULL) return LOXC_ERR_NULL;
  if (h->version == 0) return LOXC_ERR_INVALID_MAGIC;
  return LOXC_OK;
}

int loxc_header_write(loxc_writer_t *w, const loxc_header_t *h) {
  if (w == NULL || h == NULL) return LOXC_ERR_NULL;
  int rc = loxc__writer_require_byte_aligned(w);
  if (rc != LOXC_OK) return rc;
  rc = loxc_header_validate(h);
  if (rc != LOXC_OK) return rc;

  rc = loxc__write_u8(w, (uint8_t)'L');
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, (uint8_t)'X');
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, (uint8_t)'C');
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, h->module_id);
  if (rc != LOXC_OK) return rc;

  rc = loxc__write_u8(w, 2u);  /* version = 2 (v2 format) */
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, h->flags);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, h->strategy_id);
  if (rc != LOXC_OK) return rc;

  rc = loxc__write_u16_le(w, h->data_len);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u16_le(w, h->level_count);
  if (rc != LOXC_OK) return rc;

  for (int i = 0; i < 4; i++) {
    rc = loxc__write_u8(w, 0x00);
    if (rc != LOXC_OK) return rc;
  }

  if ((h->flags & LOXC_FLAG_CRC) != 0) {
    rc = loxc__write_u32_le(w, h->crc32);
    if (rc != LOXC_OK) return rc;
  }
  return LOXC_OK;
}

int loxc_header_read(loxc_reader_t *r, loxc_header_t *h) {
  if (r == NULL || h == NULL) return LOXC_ERR_NULL;
  int rc = loxc__reader_require_byte_aligned(r);
  if (rc != LOXC_OK) return rc;

  uint8_t m0 = 0, m1 = 0, m2 = 0, module_id = 0;
  rc = loxc__read_u8(r, &m0);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &m1);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &m2);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &module_id);
  if (rc != LOXC_OK) return rc;

  if (m0 != (uint8_t)'L' || m1 != (uint8_t)'X' || m2 != (uint8_t)'C') {
    return LOXC_ERR_INVALID_MAGIC;
  }

  h->module_id = module_id;
  rc = loxc__read_u8(r, &h->version);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &h->flags);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u8(r, &h->strategy_id);
  if (rc != LOXC_OK) return rc;

  rc = loxc__read_u16_le(r, &h->data_len);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u16_le(r, &h->level_count);
  if (rc != LOXC_OK) return rc;

  for (int i = 0; i < 4; i++) {
    uint8_t reserved_byte = 0;
    rc = loxc__read_u8(r, &reserved_byte);
    if (rc != LOXC_OK) return rc;
    h->reserved[i] = reserved_byte;
  }

  h->crc32 = 0;
  if ((h->flags & LOXC_FLAG_CRC) != 0) {
    rc = loxc__read_u32_le(r, &h->crc32);
    if (rc != LOXC_OK) return rc;
  }

  return loxc_header_validate(h);
}

uint32_t loxc_crc32(const uint8_t *data, size_t len) {
  if (data == NULL && len != 0) return 0;

  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint32_t)data[i];
    for (int b = 0; b < 8; b++) {
      uint32_t mask = (uint32_t)-(int)(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}
