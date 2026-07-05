#include "loxc_strategy.h"

#include <limits.h>

static uint32_t loxc__ceil_log2(uint32_t n) {
  if (n <= 1u) return 0u;
  n--;
  {
    uint32_t bits = 0u;
    while (n > 0u) {
      bits++;
      n >>= 1u;
    }
    return bits;
  }
}

static int loxc__u64_add(uint64_t a, uint64_t b, uint64_t *out) {
  if (out == NULL) return 0;
  if (a > UINT64_MAX - b) return 0;
  *out = a + b;
  return 1;
}

static int loxc__u64_mul(uint64_t a, uint64_t b, uint64_t *out) {
  if (out == NULL) return 0;
  if (a != 0u && b > UINT64_MAX / a) return 0;
  *out = a * b;
  return 1;
}

static uint64_t loxc__accumulate_weighted(const loxc_freq_entry_t *freqs,
                                          size_t n,
                                          uint32_t bits_per_symbol) {
  uint64_t total = 0u;

  if (n == 0u) return 0u;
  if (freqs == NULL) return UINT64_MAX;

  for (size_t i = 0; i < n; i++) {
    uint64_t term = 0u;
    if (!loxc__u64_mul(freqs[i].count, (uint64_t)bits_per_symbol, &term) ||
        !loxc__u64_add(total, term, &total)) {
      return UINT64_MAX;
    }
  }
  return total;
}

static int loxc__result_better(const loxc_strategy_result_t *cand,
                               const loxc_strategy_result_t *best) {
  if (cand->predicted_bits != best->predicted_bits) {
    return cand->predicted_bits < best->predicted_bits;
  }
  if (cand->predicted_total_bits != best->predicted_total_bits) {
    return cand->predicted_total_bits < best->predicted_total_bits;
  }
  if (cand->level_count != best->level_count) {
    return cand->level_count < best->level_count;
  }
  return cand->strategy < best->strategy;
}

int loxc_strategy_describe(loxc_strategy_t strategy,
                           loxc_strategy_desc_t *out_desc) {
  loxc_strategy_desc_t desc;

  if (out_desc == NULL) return LOXC_ERR_NULL;

  desc.strategy = strategy;
  desc.base_size = 0u;
  desc.bits_per_level = 0u;
  desc.direct_slots = 0u;
  desc.raw_pos = 0u;
  desc.continue_pos = 0u;

  switch (strategy) {
    case LOXC_STRATEGY_FLAT_FIXED_WIDTH:
      break;
    case LOXC_STRATEGY_HIERARCHICAL_8:
      desc.base_size = 8u;
      desc.bits_per_level = 6u;
      desc.direct_slots = 55u;
      desc.raw_pos = 55u;
      desc.continue_pos = 56u;
      break;
    case LOXC_STRATEGY_HIERARCHICAL_4:
      desc.base_size = 4u;
      desc.bits_per_level = 4u;
      desc.direct_slots = 14u;
      desc.raw_pos = 14u;
      desc.continue_pos = 15u;
      break;
    default:
      return LOXC_ERR_INVALID_FORMAT;
  }

  *out_desc = desc;
  return LOXC_OK;
}

int loxc_strategy_level_count(loxc_strategy_t strategy,
                              size_t symbol_count,
                              uint16_t *out_level_count) {
  loxc_strategy_desc_t desc;

  if (out_level_count == NULL) return LOXC_ERR_NULL;
  *out_level_count = 0u;

  if (loxc_strategy_describe(strategy, &desc) != LOXC_OK) {
    return LOXC_ERR_INVALID_FORMAT;
  }
  if (strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH || symbol_count == 0u) {
    return LOXC_OK;
  }
  if (desc.direct_slots == 0u) return LOXC_ERR_INVALID_FORMAT;
  if (symbol_count > (size_t)UINT16_MAX * (size_t)desc.direct_slots) {
    return LOXC_ERR_OVERFLOW;
  }

  *out_level_count =
      (uint16_t)((symbol_count + (size_t)desc.direct_slots - 1u) /
                 (size_t)desc.direct_slots);
  return LOXC_OK;
}

int loxc_strategy_validate_layout(loxc_strategy_t strategy,
                                  uint8_t base_size,
                                  uint8_t bits_per_level,
                                  uint16_t level_count,
                                  loxc_strategy_desc_t *out_desc) {
  loxc_strategy_desc_t desc;
  int rc = loxc_strategy_describe(strategy, &desc);
  if (rc != LOXC_OK) return rc;

  if (strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
    if (base_size != 0u || bits_per_level != 0u || level_count != 0u) {
      return LOXC_ERR_INVALID_FORMAT;
    }
  } else {
    if (base_size != desc.base_size || bits_per_level != desc.bits_per_level ||
        level_count == 0u) {
      return LOXC_ERR_INVALID_FORMAT;
    }
  }

  if (out_desc != NULL) *out_desc = desc;
  return LOXC_OK;
}

uint64_t loxc_strategy_cost_flat(const loxc_freq_entry_t *freqs, size_t n) {
  if (n == 0u) return 0u;
  return loxc__accumulate_weighted(freqs, n, loxc__ceil_log2((uint32_t)n));
}

uint64_t loxc_strategy_cost_flat_with_raw(const loxc_freq_entry_t *freqs, size_t n) {
  if (n == 0u) return 0u;
  return loxc__accumulate_weighted(freqs, n,
                                   loxc__ceil_log2((uint32_t)n + 1u));
}

uint64_t loxc_strategy_cost_hierarchical(
    const loxc_freq_entry_t *freqs,
    size_t n,
    loxc_strategy_t strategy,
    uint16_t *out_level_count
) {
  loxc_strategy_desc_t desc;
  uint64_t total = 0u;
  uint32_t max_level = 0u;

  if (out_level_count != NULL) *out_level_count = 0u;
  if (n == 0u) return 0u;
  if (freqs == NULL) return UINT64_MAX;
  if (loxc_strategy_describe(strategy, &desc) != LOXC_OK ||
      desc.direct_slots == 0u) {
    return UINT64_MAX;
  }

  for (size_t i = 0; i < n; i++) {
    uint64_t term = 0u;
    uint32_t level = (uint32_t)(i / (size_t)desc.direct_slots);
    uint32_t bits = (level + 1u) * (uint32_t)desc.bits_per_level;
    if (!loxc__u64_mul(freqs[i].count, (uint64_t)bits, &term) ||
        !loxc__u64_add(total, term, &total)) {
      return UINT64_MAX;
    }
    if (level > max_level) max_level = level;
  }

  if (out_level_count != NULL) *out_level_count = (uint16_t)(max_level + 1u);
  return total;
}

loxc_strategy_result_t loxc_strategy_select(
    const loxc_freq_entry_t *freqs,
    size_t freq_count
) {
  loxc_strategy_result_t best;
  loxc_strategy_result_t cand;
  uint64_t total_syms = 0u;

  best.strategy = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  best.predicted_bits = UINT64_MAX;
  best.predicted_total_bits = UINT64_MAX;
  best.level_count = UINT16_MAX;
  best.bits_per_symbol = 0.0;

  if (freq_count != 0u && freqs == NULL) return best;

  cand.strategy = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  cand.predicted_bits = loxc_strategy_cost_flat_with_raw(freqs, freq_count);
  cand.predicted_total_bits = cand.predicted_bits;
  cand.level_count = 0u;
  cand.bits_per_symbol = 0.0;
  if (loxc__result_better(&cand, &best)) best = cand;

  cand.strategy = LOXC_STRATEGY_HIERARCHICAL_8;
  cand.predicted_bits =
      loxc_strategy_cost_hierarchical(freqs, freq_count, cand.strategy,
                                      &cand.level_count);
  cand.predicted_total_bits = cand.predicted_bits;
  cand.bits_per_symbol = 0.0;
  if (loxc__result_better(&cand, &best)) best = cand;

  cand.strategy = LOXC_STRATEGY_HIERARCHICAL_4;
  cand.predicted_bits =
      loxc_strategy_cost_hierarchical(freqs, freq_count, cand.strategy,
                                      &cand.level_count);
  cand.predicted_total_bits = cand.predicted_bits;
  cand.bits_per_symbol = 0.0;
  if (loxc__result_better(&cand, &best)) best = cand;

  for (size_t i = 0; i < freq_count; i++) {
    if (!loxc__u64_add(total_syms, freqs[i].count, &total_syms)) {
      total_syms = 0u;
      best.bits_per_symbol = 0.0;
      return best;
    }
  }

  best.bits_per_symbol =
      (total_syms > 0u && best.predicted_bits != UINT64_MAX)
          ? (double)best.predicted_bits / (double)total_syms
          : 0.0;
  return best;
}
