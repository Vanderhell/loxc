#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc_hier.h"
#include "loxc_stream.h"
#include "loxc_strategy.h"

static const char *LOREM_IPSUM =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, "
    "quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
    "consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse "
    "cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non "
    "proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

static const char *C_SOURCE_CODE =
    "#include <stdio.h>\n#include <stdlib.h>\n"
    "int main(int argc, char *argv[]) {\n"
    "  if (argc < 2) { fprintf(stderr, \"Usage: %s <file>\\n\", argv[0]); return 1; }\n"
    "  FILE *fp = fopen(argv[1], \"rb\");\n"
    "  if (!fp) { perror(\"fopen\"); return 1; }\n"
    "  uint8_t buf[4096]; size_t nread;\n"
    "  while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {\n"
    "    for (size_t i = 0; i < nread; i++) {\n"
    "      if (buf[i] >= 32 && buf[i] < 127) putchar(buf[i]);\n"
    "    }\n"
    "  }\n"
    "  fclose(fp); return 0;\n"
    "}\n";

typedef struct {
  loxc_strategy_t strategy;
  uint64_t estimated_bits;
  uint64_t actual_bits;
  uint16_t level_count;
} strategy_measure_t;

static int cmp_freq_desc(const void *a, const void *b) {
  const loxc_freq_entry_t *fa = (const loxc_freq_entry_t *)a;
  const loxc_freq_entry_t *fb = (const loxc_freq_entry_t *)b;
  if (fa->count > fb->count) return -1;
  if (fa->count < fb->count) return 1;
  if (fa->symbol_id < fb->symbol_id) return -1;
  if (fa->symbol_id > fb->symbol_id) return 1;
  return 0;
}

static uint64_t writer_bits_used(const loxc_writer_t *w) {
  return (uint64_t)w->byte_pos * 8u + (uint64_t)w->bit_pos;
}

static uint8_t bits_needed_u32(uint32_t n) {
  uint8_t bits = 0u;
  uint32_t v = (n <= 1u) ? 1u : (n - 1u);
  while (v > 0u) {
    bits++;
    v >>= 1u;
  }
  return (bits == 0u) ? 1u : bits;
}

static uint64_t encode_flat_bits(const loxc_freq_entry_t *freqs, size_t n) {
  const uint8_t sym_bits = bits_needed_u32((uint32_t)n + 1u);
  uint8_t buffer[65536];
  loxc_writer_t w;
  assert(loxc_writer_init(&w, buffer, sizeof(buffer)) == LOXC_OK);

  for (size_t i = 0; i < n; i++) {
    for (uint64_t c = 0; c < freqs[i].count; c++) {
      assert(loxc_write_bits(&w, freqs[i].symbol_id, sym_bits) == LOXC_OK);
    }
  }
  return writer_bits_used(&w);
}

static uint64_t encode_hier_bits(const loxc_freq_entry_t *freqs, size_t n,
                                 loxc_strategy_t strategy,
                                 uint16_t *out_level_count) {
  uint8_t buffer[65536];
  loxc_writer_t w;
  loxc_hier_t h;

  memset(&h, 0, sizeof(h));
  assert(loxc_hier_build(freqs, n, strategy, &h) == LOXC_OK);
  assert(loxc_writer_init(&w, buffer, sizeof(buffer)) == LOXC_OK);

  for (size_t i = 0; i < n; i++) {
    for (uint64_t c = 0; c < freqs[i].count; c++) {
      assert(loxc_hier_encode(&h, freqs[i].symbol_id, &w) == LOXC_OK);
    }
  }

  if (out_level_count != NULL) *out_level_count = h.level_count;
  {
    const uint64_t bits = writer_bits_used(&w);
    loxc_hier_free(&h);
    return bits;
  }
}

static void make_zipfian(loxc_freq_entry_t *out, size_t n, uint64_t total) {
  double harmonic = 0.0;
  for (size_t i = 0; i < n; i++) {
    harmonic += 1.0 / (double)(i + 1u);
  }

  for (size_t i = 0; i < n; i++) {
    out[i].symbol_id = (uint32_t)i;
    out[i].count = (uint64_t)((double)total / ((double)(i + 1u) * harmonic));
    if (out[i].count == 0u) out[i].count = 1u;
  }
}

static loxc_freq_entry_t *build_freqs_from_text(const uint8_t *text, size_t len,
                                                size_t *out_count) {
  loxc_freq_entry_t *freqs = NULL;
  uint64_t counts[256];
  size_t n = 0u;

  memset(counts, 0, sizeof(counts));
  for (size_t i = 0; i < len; i++) counts[text[i]]++;
  for (int i = 0; i < 256; i++) {
    if (counts[i] > 0u) n++;
  }

  freqs = (loxc_freq_entry_t *)malloc(n * sizeof(loxc_freq_entry_t));
  assert(freqs != NULL);
  n = 0u;
  for (int i = 0; i < 256; i++) {
    if (counts[i] == 0u) continue;
    freqs[n].symbol_id = (uint32_t)i;
    freqs[n].count = counts[i];
    n++;
  }
  qsort(freqs, n, sizeof(loxc_freq_entry_t), cmp_freq_desc);
  for (size_t i = 0; i < n; i++) freqs[i].symbol_id = (uint32_t)i;
  *out_count = n;
  return freqs;
}

static strategy_measure_t measure_strategy(const loxc_freq_entry_t *freqs, size_t n,
                                           loxc_strategy_t strategy) {
  strategy_measure_t m;
  uint16_t actual_levels = 0u;
  memset(&m, 0, sizeof(m));
  m.strategy = strategy;
  if (strategy == LOXC_STRATEGY_FLAT_FIXED_WIDTH) {
    m.estimated_bits = loxc_strategy_cost_flat_with_raw(freqs, n);
    m.actual_bits = encode_flat_bits(freqs, n);
    m.level_count = 0u;
  } else {
    m.estimated_bits = loxc_strategy_cost_hierarchical(freqs, n, strategy,
                                                       &m.level_count);
    m.actual_bits = encode_hier_bits(freqs, n, strategy, &actual_levels);
    assert(actual_levels == m.level_count);
  }
  return m;
}

static int strategy_better(const strategy_measure_t *cand,
                           const strategy_measure_t *best) {
  if (cand->actual_bits != best->actual_bits) {
    return cand->actual_bits < best->actual_bits;
  }
  if (cand->level_count != best->level_count) {
    return cand->level_count < best->level_count;
  }
  return cand->strategy < best->strategy;
}

static void test_scenario(const char *name, const loxc_freq_entry_t *freqs, size_t n) {
  strategy_measure_t measures[3];
  strategy_measure_t best;
  loxc_strategy_result_t result;

  measures[0] = measure_strategy(freqs, n, LOXC_STRATEGY_FLAT_FIXED_WIDTH);
  measures[1] = measure_strategy(freqs, n, LOXC_STRATEGY_HIERARCHICAL_8);
  measures[2] = measure_strategy(freqs, n, LOXC_STRATEGY_HIERARCHICAL_4);

  assert(measures[0].estimated_bits == measures[0].actual_bits);
  assert(measures[1].estimated_bits == measures[1].actual_bits);
  assert(measures[2].estimated_bits == measures[2].actual_bits);

  best = measures[0];
  if (strategy_better(&measures[1], &best)) best = measures[1];
  if (strategy_better(&measures[2], &best)) best = measures[2];

  result = loxc_strategy_select(freqs, n);

  printf("%s:\n", name);
  printf("  Symbols: %zu\n", n);
  printf("  FLAT  est=%llu act=%llu\n",
         (unsigned long long)measures[0].estimated_bits,
         (unsigned long long)measures[0].actual_bits);
  printf("  HIER8 est=%llu act=%llu levels=%u\n",
         (unsigned long long)measures[1].estimated_bits,
         (unsigned long long)measures[1].actual_bits,
         measures[1].level_count);
  printf("  HIER4 est=%llu act=%llu levels=%u\n",
         (unsigned long long)measures[2].estimated_bits,
         (unsigned long long)measures[2].actual_bits,
         measures[2].level_count);

  assert(result.strategy == best.strategy);
  assert(result.predicted_bits == best.actual_bits);
  assert(result.level_count == best.level_count);
  printf("  PASS (selector matches actual emitted bits)\n\n");
}

static void test_scenario_1_zipf30(void) {
  loxc_freq_entry_t freqs[30];
  make_zipfian(freqs, 30, 500);
  test_scenario("Scenario 1: Zipf(30, 500 occurrences)", freqs, 30);
}

static void test_scenario_2_zipf100(void) {
  loxc_freq_entry_t freqs[100];
  make_zipfian(freqs, 100, 5000);
  test_scenario("Scenario 2: Zipf(100, 5000 occurrences)", freqs, 100);
}

static void test_scenario_3_zipf256(void) {
  loxc_freq_entry_t freqs[256];
  make_zipfian(freqs, 256, 50000);
  test_scenario("Scenario 3: Zipf(256, 50000 occurrences)", freqs, 256);
}

static void test_scenario_4_lorem_ipsum(void) {
  size_t n = 0u;
  loxc_freq_entry_t *freqs =
      build_freqs_from_text((const uint8_t *)LOREM_IPSUM, strlen(LOREM_IPSUM), &n);
  test_scenario("Scenario 4: Lorem Ipsum text", freqs, n);
  free(freqs);
}

static void test_scenario_5_source(void) {
  size_t n = 0u;
  loxc_freq_entry_t *freqs =
      build_freqs_from_text((const uint8_t *)C_SOURCE_CODE, strlen(C_SOURCE_CODE), &n);
  test_scenario("Scenario 5: C source code sample", freqs, n);
  free(freqs);
}

int main(void) {
  printf("=== test_strategy: Actual Emitted-Bit Strategy Verification ===\n\n");

  test_scenario_1_zipf30();
  test_scenario_2_zipf100();
  test_scenario_3_zipf256();
  test_scenario_4_lorem_ipsum();
  test_scenario_5_source();

  printf("=== test_strategy: PASS (all scenarios) ===\n");
  return 0;
}
