#ifndef LOXC_HIER_H
#define LOXC_HIER_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_base.h"
#include "loxc_stream.h"
#include "loxc_strategy.h"
#include "loxc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hierarchical symbol encoder/decoder state */
typedef struct {
  loxc_strategy_t strategy;
  uint32_t direct_slots;
  uint32_t bits_per_level;
  uint32_t raw_pos;
  uint32_t continue_pos;
  uint16_t level_count;
  uint32_t symbol_count;
  uint32_t *pos_to_symbol;  /* [symbol_count]: rank → symbol_id */
  uint32_t *symbol_to_pos;  /* [symbol_count]: symbol_id → rank (UINT32_MAX = absent) */
} loxc_hier_t;

/*
 * Build a hierarchical symbol table from frequency data.
 * freqs must be sorted by count (descending).
 * strategy must be HIER8 or HIER4.
 */
int loxc_hier_build(
    const loxc_freq_entry_t *freqs,
    size_t n,
    loxc_strategy_t strategy,
    loxc_hier_t *out
);

/* Encode a single symbol. Returns bits written in w. */
int loxc_hier_encode(
    const loxc_hier_t *h,
    uint32_t symbol_id,
    loxc_writer_t *w
);

/* Decode a single symbol from reader. */
int loxc_hier_decode(
    const loxc_hier_t *h,
    loxc_reader_t *r,
    uint32_t *out_symbol
);

/* Free internal allocations. */
void loxc_hier_free(loxc_hier_t *h);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOXC_HIER_H */
