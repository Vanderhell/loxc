#include "loxc_matrix.h"

static int loxc__checked_mul_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a != 0u && b > SIZE_MAX / a) return LOXC_ERR_OVERFLOW;
  *out = a * b;
  return LOXC_OK;
}

static int loxc__checked_add_size(size_t a, size_t b, size_t *out) {
  if (out == NULL) return LOXC_ERR_NULL;
  if (a > SIZE_MAX - b) return LOXC_ERR_OVERFLOW;
  *out = a + b;
  return LOXC_OK;
}

static int loxc__is_pow2_u8(uint8_t v) {
  return v != 0 && (uint8_t)(v & (uint8_t)(v - 1u)) == 0;
}

static int loxc__level_cells(const loxc_matrix_level_t *lvl, size_t *out) {
  if (lvl == NULL || out == NULL) return LOXC_ERR_NULL;
  if (lvl->dims < 1 || lvl->dims > 3) return LOXC_ERR_INVALID_MAGIC;
  if (lvl->bits == 0 || lvl->bits > 24) return LOXC_ERR_INVALID_MAGIC;

  size_t cells = 1;
  for (uint8_t d = 0; d < lvl->dims; d++) {
    uint8_t sz = lvl->size[d];
    if (!loxc__is_pow2_u8(sz)) return LOXC_ERR_INVALID_MAGIC;
    if (loxc__checked_mul_size(cells, (size_t)sz, &cells) != LOXC_OK) {
      return LOXC_ERR_OVERFLOW;
    }
  }

  /* Ensure bit count matches the number of cells exactly. */
  if (((size_t)1u << lvl->bits) != cells) return LOXC_ERR_INVALID_MAGIC;
  *out = cells;
  return LOXC_OK;
}

static int loxc__matrix_capacity(const loxc_matrix_t *m, size_t *out_total_cells) {
  size_t total_cells = 1u;

  if (m == NULL || out_total_cells == NULL) return LOXC_ERR_NULL;
  if (m->levels == 0u || m->levels > 8u) return LOXC_ERR_INVALID_MAGIC;
  if (m->data == NULL || m->data_len == 0u) return LOXC_ERR_INVALID_MAGIC;

  for (uint8_t i = 0; i < m->levels; i++) {
    size_t level_cells = 0u;
    int rc = loxc__level_cells(&m->level[i], &level_cells);
    if (rc != LOXC_OK) return rc;
    if (loxc__checked_mul_size(total_cells, level_cells, &total_cells) != LOXC_OK) {
      return LOXC_ERR_OVERFLOW;
    }
  }

  if (m->data_len > total_cells) return LOXC_ERR_INVALID_FORMAT;
  *out_total_cells = total_cells;
  return LOXC_OK;
}

int loxc_matrix_decode(const loxc_matrix_t *m, loxc_reader_t *r,
                       loxc_matrix_value_t *out) {
  size_t total_cells = 0u;

  if (m == NULL || r == NULL || out == NULL) return LOXC_ERR_NULL;
  {
    int rc = loxc__matrix_capacity(m, &total_cells);
    if (rc != LOXC_OK) return rc;
  }

  size_t linear = 0;
  for (uint8_t i = 0; i < m->levels; i++) {
    size_t cells = 0;
    int rc = loxc__level_cells(&m->level[i], &cells);
    if (rc != LOXC_OK) return rc;

    uint32_t idx = 0;
    rc = loxc_read_bits(r, m->level[i].bits, &idx);
    if (rc != LOXC_OK) return rc;
    if ((size_t)idx >= cells) return LOXC_ERR_INVALID_MAGIC;

    if (loxc__checked_mul_size(linear, cells, &linear) != LOXC_OK ||
        loxc__checked_add_size(linear, (size_t)idx, &linear) != LOXC_OK) {
      return LOXC_ERR_OVERFLOW;
    }
  }

  if (linear >= total_cells) return LOXC_ERR_INVALID_MAGIC;
  if (linear >= m->data_len) return LOXC_ERR_INVALID_MAGIC;
  *out = m->data[linear];
  return LOXC_OK;
}

int loxc_matrix_encode(const loxc_matrix_t *m, uint32_t codepoint,
                       loxc_writer_t *w) {
  size_t total_cells = 0u;

  if (m == NULL || w == NULL) return LOXC_ERR_NULL;
  {
    int rc = loxc__matrix_capacity(m, &total_cells);
    if (rc != LOXC_OK) return rc;
  }

  size_t linear = (size_t)-1;
  for (size_t i = 0; i < m->data_len; i++) {
    if (m->data[i].codepoint == codepoint) {
      linear = i;
      break;
    }
  }
  if (linear == (size_t)-1) return LOXC_ERR_INVALID_MAGIC;

  size_t cells[8];
  for (uint8_t i = 0; i < m->levels; i++) {
    cells[i] = 0;
    int rc = loxc__level_cells(&m->level[i], &cells[i]);
    if (rc != LOXC_OK) return rc;
  }

  uint32_t idx_per_level[8];
  for (int i = (int)m->levels - 1; i >= 0; i--) {
    idx_per_level[i] = (uint32_t)(linear % cells[i]);
    linear /= cells[i];
  }
  if (linear != 0u) return LOXC_ERR_INVALID_FORMAT;

  for (uint8_t i = 0; i < m->levels; i++) {
    int rc = loxc_write_bits(w, idx_per_level[i], m->level[i].bits);
    if (rc != LOXC_OK) return rc;
  }
  return LOXC_OK;
}
