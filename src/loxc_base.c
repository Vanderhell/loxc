#include "loxc_base.h"

const uint8_t *loxc_magic_bytes(void) {
  return LOXC_CONTAINER_MAGIC_BYTES;
}

static int loxc__strategy_validate(uint8_t strategy_id, uint16_t level_count) {
  switch (strategy_id) {
    case LOXC_STRATEGY_FLAT_FIXED_WIDTH:
      return (level_count == 0u) ? LOXC_OK : LOXC_ERR_INVALID_FORMAT;
    case LOXC_STRATEGY_HIERARCHICAL_8:
    case LOXC_STRATEGY_HIERARCHICAL_4:
      return (level_count != 0u) ? LOXC_OK : LOXC_ERR_INVALID_FORMAT;
    default:
      return LOXC_ERR_INVALID_FORMAT;
  }
}

static int loxc__header_validate_common(const loxc_header_t *h) {
  int rc = LOXC_OK;

  if (h == NULL) return LOXC_ERR_NULL;
  if (h->version != LOXC_HEADER_VERSION_V2) return LOXC_ERR_INVALID_FORMAT;
  if ((h->flags & (uint8_t)~LOXC_FLAG_EMBEDDED_TABLE) != 0u) {
    return LOXC_ERR_INVALID_FORMAT;
  }

  rc = loxc__strategy_validate(h->strategy_id, h->level_count);
  if (rc != LOXC_OK) return rc;

  if (h->payload_len == 0u && h->uncompressed_len != 0u) {
    return LOXC_ERR_INVALID_FORMAT;
  }

  return LOXC_OK;
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
  return loxc__header_validate_common(h);
}

size_t loxc_header_size(const loxc_header_t *h) {
  (void)h;
  return LOXC_HEADER_SIZE_V2;
}

int loxc_header_resolve_payload_len(const loxc_header_t *h,
                                    size_t available_bytes,
                                    size_t *out_payload_len) {
  int rc = LOXC_OK;

  if (out_payload_len == NULL) return LOXC_ERR_NULL;
  *out_payload_len = 0;

  rc = loxc__header_validate_common(h);
  if (rc != LOXC_OK) return rc;

  if (h->payload_len == LOXC_HEADER_PAYLOAD_LEN_LEGACY_TO_EOF) {
    *out_payload_len = available_bytes;
  } else if ((size_t)h->payload_len > available_bytes) {
    return LOXC_ERR_TRUNCATED;
  } else if ((size_t)h->payload_len < available_bytes) {
    return LOXC_ERR_INVALID_FORMAT;
  } else {
    *out_payload_len = (size_t)h->payload_len;
  }

  if (*out_payload_len == 0u && h->uncompressed_len != 0u) {
    return LOXC_ERR_INVALID_FORMAT;
  }

  return LOXC_OK;
}

int loxc_reader_finish_zero_padding(loxc_reader_t *r) {
  if (r == NULL) return LOXC_ERR_NULL;

  if (r->bit_pos != 0u) {
    const uint8_t current = r->buf[r->byte_pos];
    const uint8_t mask = (uint8_t)((1u << (8u - r->bit_pos)) - 1u);
    if ((current & mask) != 0u) return LOXC_ERR_INVALID_FORMAT;
    r->bit_pos = 0u;
    r->byte_pos++;
  }

  if (r->byte_pos != r->len) return LOXC_ERR_INVALID_FORMAT;
  return LOXC_OK;
}

int loxc_header_write(loxc_writer_t *w, const loxc_header_t *h) {
  int rc = LOXC_OK;

  if (w == NULL || h == NULL) return LOXC_ERR_NULL;
  rc = loxc__writer_require_byte_aligned(w);
  if (rc != LOXC_OK) return rc;
  rc = loxc__header_validate_common(h);
  if (rc != LOXC_OK) return rc;
  if (h->payload_len == LOXC_HEADER_PAYLOAD_LEN_LEGACY_TO_EOF) {
    return LOXC_ERR_INVALID_FORMAT;
  }

  rc = loxc__write_u8(w, (uint8_t)'L');
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, (uint8_t)'X');
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, (uint8_t)'C');
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, h->module_id);
  if (rc != LOXC_OK) return rc;

  rc = loxc__write_u8(w, h->version);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, h->flags);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u8(w, h->strategy_id);
  if (rc != LOXC_OK) return rc;

  rc = loxc__write_u16_le(w, h->payload_len);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u16_le(w, h->level_count);
  if (rc != LOXC_OK) return rc;

  rc = loxc__write_u32_le(w, h->uncompressed_len);
  if (rc != LOXC_OK) return rc;
  rc = loxc__write_u32_le(w, h->table_fingerprint);
  if (rc != LOXC_OK) return rc;
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

  rc = loxc__read_u16_le(r, &h->payload_len);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u16_le(r, &h->level_count);
  if (rc != LOXC_OK) return rc;

  h->crc32 = 0;
  rc = loxc__read_u32_le(r, &h->uncompressed_len);
  if (rc != LOXC_OK) return rc;
  rc = loxc__read_u32_le(r, &h->table_fingerprint);
  if (rc != LOXC_OK) return rc;

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
