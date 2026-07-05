#ifndef LOXC_STRATEGY_H
#define LOXC_STRATEGY_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_base.h"

/* Frequency entry: symbol ID + occurrence count */
typedef struct {
  uint32_t symbol_id;
  uint64_t count;
} loxc_freq_entry_t;

/* Strategy selection result */
typedef struct {
  loxc_strategy_t strategy;
  uint64_t predicted_bits;
  uint16_t level_count;
  double bits_per_symbol;
} loxc_strategy_result_t;

/*
 * Select optimal encoding strategy based on frequency distribution.
 *
 * Tests three approaches:
 *   - FLAT_FIXED_WIDTH: ceil(log2(N)) bits per symbol
 *   - HIERARCHICAL_8: 8×8 matrix with escape to next level (6 bits per level)
 *   - HIERARCHICAL_4: 4×4 matrix with escape to next level (4 bits per level)
 *
 * Assumes 'freqs' is sorted descending by count.
 * Returns the strategy with lowest total predicted bits.
 */
loxc_strategy_result_t loxc_strategy_select(
    const loxc_freq_entry_t *freqs,
    size_t freq_count
);

/* Cost estimation functions (exported for testing) */
uint64_t loxc_strategy_cost_flat(const loxc_freq_entry_t *freqs, size_t n);
uint64_t loxc_strategy_cost_flat_with_raw(const loxc_freq_entry_t *freqs, size_t n);

uint64_t loxc_strategy_cost_hierarchical(
    const loxc_freq_entry_t *freqs,
    size_t n,
    uint32_t base_size,
    uint32_t escape_count,
    uint16_t *out_level_count
);

#endif /* LOXC_STRATEGY_H */
