#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "loxc_hier.h"

int loxc_hier_build(
    const loxc_freq_entry_t *freqs,
    size_t n,
    loxc_strategy_t strategy,
    loxc_hier_t *out
) {
  if (freqs == NULL || out == NULL) return LOXC_ERR_NULL;
  if (n == 0) return LOXC_ERR_NULL;
  if (strategy != LOXC_STRATEGY_HIERARCHICAL_8 &&
      strategy != LOXC_STRATEGY_HIERARCHICAL_4) {
    return LOXC_ERR_INVALID_MAGIC;
  }

  memset(out, 0, sizeof(*out));
  out->strategy = strategy;

  if (strategy == LOXC_STRATEGY_HIERARCHICAL_8) {
    out->direct_slots = 56;   /* 8*8 - 8 escape cells */
    out->bits_per_level = 6;  /* ceil(log2(64)) */
    out->escape_pos = 56;
  } else {
    out->direct_slots = 15;   /* 4*4 - 1 escape cell */
    out->bits_per_level = 4;  /* ceil(log2(16)) */
    out->escape_pos = 15;
  }

  out->symbol_count = (uint32_t)n;
  out->level_count =
      (uint16_t)((n + out->direct_slots - 1) / out->direct_slots);

  /* Allocate pos_to_symbol: [0..n-1] maps rank to symbol_id */
  out->pos_to_symbol = (uint32_t *)malloc(n * sizeof(uint32_t));
  if (out->pos_to_symbol == NULL) return LOXC_ERR_OVERFLOW;

  /* Allocate symbol_to_pos: [0..n-1] maps symbol_id to rank
   * Initialize to UINT32_MAX (not in table) */
  out->symbol_to_pos = (uint32_t *)malloc(n * sizeof(uint32_t));
  if (out->symbol_to_pos == NULL) {
    free(out->pos_to_symbol);
    out->pos_to_symbol = NULL;
    return LOXC_ERR_OVERFLOW;
  }

  for (uint32_t i = 0; i < (uint32_t)n; i++) {
    out->symbol_to_pos[i] = UINT32_MAX;
  }

  /* Populate both tables from freqs array */
  for (size_t i = 0; i < n; i++) {
    uint32_t sid = freqs[i].symbol_id;
    out->pos_to_symbol[i] = sid;
    if (sid >= (uint32_t)n) return LOXC_ERR_INVALID_MAGIC;
    out->symbol_to_pos[sid] = (uint32_t)i;
  }

  return LOXC_OK;
}

int loxc_hier_encode(
    const loxc_hier_t *h,
    uint32_t symbol_id,
    loxc_writer_t *w
) {
  if (h == NULL || w == NULL) return LOXC_ERR_NULL;

  if (symbol_id >= h->symbol_count) return LOXC_ERR_SYMBOL_NOT_FOUND;

  uint32_t rank = h->symbol_to_pos[symbol_id];
  if (rank == UINT32_MAX) return LOXC_ERR_SYMBOL_NOT_FOUND;

  uint32_t level = rank / h->direct_slots;
  uint32_t pos_in_level = rank % h->direct_slots;

  /* Write escape codes for levels we pass through */
  for (uint32_t l = 0; l < level; l++) {
    int rc = loxc_write_bits(w, h->escape_pos, h->bits_per_level);
    if (rc != LOXC_OK) return rc;
  }

  /* Write the position in the final level */
  return loxc_write_bits(w, pos_in_level, h->bits_per_level);
}

int loxc_hier_decode(
    const loxc_hier_t *h,
    loxc_reader_t *r,
    uint32_t *out_symbol
) {
  if (h == NULL || r == NULL || out_symbol == NULL) return LOXC_ERR_NULL;

  for (uint32_t level = 0; level < (uint32_t)h->level_count; level++) {
    uint32_t pos = 0;
    int rc = loxc_read_bits(r, h->bits_per_level, &pos);
    if (rc != LOXC_OK) return rc;

    if (pos < h->escape_pos) {
      /* Found the symbol */
      uint32_t rank = level * h->direct_slots + pos;
      if (rank >= h->symbol_count) return LOXC_ERR_INVALID_MAGIC;
      *out_symbol = h->pos_to_symbol[rank];
      return LOXC_OK;
    }
    /* pos >= escape_pos: escape to next level */
  }

  /* Exhausted all levels without finding a symbol */
  return LOXC_ERR_INVALID_MAGIC;
}

void loxc_hier_free(loxc_hier_t *h) {
  if (h == NULL) return;
  free(h->pos_to_symbol);
  free(h->symbol_to_pos);
  memset(h, 0, sizeof(*h));
}
