#include "loxc_strategy.h"

static uint32_t loxc__ceil_log2(uint32_t n) {
  if (n <= 1) return 0;
  uint32_t bits = 0;
  uint32_t power = 1;
  while (power < n) {
    bits++;
    power <<= 1;
  }
  return bits;
}

uint64_t loxc_strategy_cost_flat(const loxc_freq_entry_t *freqs, size_t n) {
  if (n == 0) return 0;

  uint32_t bits = loxc__ceil_log2((uint32_t)n);
  uint64_t total = 0;
  for (size_t i = 0; i < n; i++) {
    total += freqs[i].count * (uint64_t)bits;
  }
  return total;
}

uint64_t loxc_strategy_cost_hierarchical(
    const loxc_freq_entry_t *freqs,
    size_t n,
    uint32_t base_size,
    uint32_t escape_count,
    uint16_t *out_level_count
) {
  if (n == 0) {
    if (out_level_count) *out_level_count = 0;
    return 0;
  }

  uint32_t bits_per_level = loxc__ceil_log2(base_size * base_size);
  uint32_t direct_slots = base_size * base_size - escape_count;

  uint64_t total = 0;
  uint32_t max_level = 0;

  for (size_t i = 0; i < n; i++) {
    uint32_t level = (uint32_t)(i / direct_slots);
    uint32_t bits = (level + 1) * bits_per_level;
    total += freqs[i].count * (uint64_t)bits;
    if (level > max_level) max_level = level;
  }

  if (out_level_count) *out_level_count = (uint16_t)(max_level + 1);
  return total;
}

loxc_strategy_result_t loxc_strategy_select(
    const loxc_freq_entry_t *freqs,
    size_t freq_count
) {
  loxc_strategy_result_t best;
  best.strategy = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  best.predicted_bits = loxc_strategy_cost_flat(freqs, freq_count);
  best.level_count = 0;

  uint16_t lvl8 = 0, lvl4 = 0;
  uint64_t h8 = loxc_strategy_cost_hierarchical(freqs, freq_count, 8, 8, &lvl8);
  uint64_t h4 = loxc_strategy_cost_hierarchical(freqs, freq_count, 4, 4, &lvl4);

  if (h8 < best.predicted_bits) {
    best.strategy = LOXC_STRATEGY_HIERARCHICAL_8;
    best.predicted_bits = h8;
    best.level_count = lvl8;
  }
  if (h4 < best.predicted_bits) {
    best.strategy = LOXC_STRATEGY_HIERARCHICAL_4;
    best.predicted_bits = h4;
    best.level_count = lvl4;
  }

  uint64_t total_syms = 0;
  for (size_t i = 0; i < freq_count; i++) {
    total_syms += freqs[i].count;
  }
  best.bits_per_symbol = total_syms > 0
      ? (double)best.predicted_bits / (double)total_syms
      : 0.0;

  return best;
}
