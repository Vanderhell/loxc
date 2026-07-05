#ifndef LOXC_STRATEGY_H
#define LOXC_STRATEGY_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_base.h"

typedef struct {
  uint32_t symbol_id;
  uint64_t count;
} loxc_freq_entry_t;

typedef struct {
  loxc_strategy_t strategy;
  uint64_t predicted_bits;
  uint64_t predicted_total_bits;
  uint16_t level_count;
  double bits_per_symbol;
} loxc_strategy_result_t;

typedef struct {
  loxc_strategy_t strategy;
  uint8_t base_size;
  uint8_t bits_per_level;
  uint8_t direct_slots;
  uint8_t raw_pos;
  uint8_t continue_pos;
} loxc_strategy_desc_t;

/* Shared strategy metadata used across estimator, generator, loader, and hier codec. */
int loxc_strategy_describe(loxc_strategy_t strategy,
                           loxc_strategy_desc_t *out_desc);

int loxc_strategy_level_count(loxc_strategy_t strategy,
                              size_t symbol_count,
                              uint16_t *out_level_count);

int loxc_strategy_validate_layout(loxc_strategy_t strategy,
                                  uint8_t base_size,
                                  uint8_t bits_per_level,
                                  uint16_t level_count,
                                  loxc_strategy_desc_t *out_desc);

/*
 * Select the strategy with the smallest predicted emitted payload bit count.
 * Ties are resolved by lower level count, then lower strategy id.
 * freqs must be sorted deterministically by descending count.
 */
loxc_strategy_result_t loxc_strategy_select(
    const loxc_freq_entry_t *freqs,
    size_t freq_count
);

uint64_t loxc_strategy_cost_flat(const loxc_freq_entry_t *freqs, size_t n);
uint64_t loxc_strategy_cost_flat_with_raw(const loxc_freq_entry_t *freqs, size_t n);

uint64_t loxc_strategy_cost_hierarchical(
    const loxc_freq_entry_t *freqs,
    size_t n,
    loxc_strategy_t strategy,
    uint16_t *out_level_count
);

#endif /* LOXC_STRATEGY_H */
