#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc_hier.h"

/* Helper: create a fake freqs array where symbol_id == rank (for testing) */
static void make_freqs_ordered(loxc_freq_entry_t *out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    out[i].symbol_id = (uint32_t)i;
    out[i].count = n - i; /* descending by count */
  }
}

/* Test 1: HIER8 with 30 symbols (all on L0) */
static void test_hier8_30_symbols(void) {
  printf("Test 1: HIER8 with 30 symbols (all on L0)\n");

  loxc_freq_entry_t freqs[30];
  make_freqs_ordered(freqs, 30);

  loxc_hier_t h;
  int rc = loxc_hier_build(freqs, 30, LOXC_STRATEGY_HIERARCHICAL_8, &h);
  assert(rc == LOXC_OK);
  assert(h.symbol_count == 30);
  assert(h.level_count == 1); /* all fit in L0 */
  assert(h.direct_slots == 56);
  assert(h.bits_per_level == 6);

  /* Encode all symbols */
  uint8_t buffer[256];
  loxc_writer_t w;
  loxc_writer_init(&w, buffer, sizeof(buffer));

  for (uint32_t i = 0; i < 30; i++) {
    rc = loxc_hier_encode(&h, i, &w);
    assert(rc == LOXC_OK);
  }

  rc = loxc_writer_flush(&w);
  assert(rc == LOXC_OK);
  size_t total_bits = w.byte_pos * 8;
  size_t expected_bits = 30 * 6; /* 180, but stored with padding = 180 bits in 23 bytes */
  printf("  Encoded bits: %zu (expected ~%zu, stored in %zu bytes)\n", total_bits, expected_bits, w.byte_pos);

  /* Decode and verify */
  loxc_reader_t r;
  loxc_reader_init(&r, buffer, w.byte_pos);

  for (uint32_t i = 0; i < 30; i++) {
    uint32_t symbol = 0;
    rc = loxc_hier_decode(&h, &r, &symbol);
    assert(rc == LOXC_OK);
    assert(symbol == i);
  }

  loxc_hier_free(&h);
  printf("  ✓ PASS (round-trip OK)\n\n");
}

/* Test 2: HIER8 with 100 symbols (L0 + L1) */
static void test_hier8_100_symbols(void) {
  printf("Test 2: HIER8 with 100 symbols (L0 + L1)\n");

  loxc_freq_entry_t *freqs = (loxc_freq_entry_t *)malloc(100 * sizeof(loxc_freq_entry_t));
  assert(freqs != NULL);
  make_freqs_ordered(freqs, 100);

  loxc_hier_t h;
  int rc = loxc_hier_build(freqs, 100, LOXC_STRATEGY_HIERARCHICAL_8, &h);
  assert(rc == LOXC_OK);
  assert(h.symbol_count == 100);
  assert(h.level_count == 2); /* 56 on L0, 44 on L1 */

  uint8_t buffer[512];
  loxc_writer_t w;
  loxc_writer_init(&w, buffer, sizeof(buffer));

  for (uint32_t i = 0; i < 100; i++) {
    rc = loxc_hier_encode(&h, i, &w);
    assert(rc == LOXC_OK);
  }

  rc = loxc_writer_flush(&w);
  assert(rc == LOXC_OK);
  size_t total_bits = w.byte_pos * 8;
  size_t expected_bits = 56 * 6 + 44 * 12; /* 336 + 528 = 864, stored in 108 bytes = 864 bits */
  printf("  Encoded bits: %zu (expected ~%zu, stored in %zu bytes)\n", total_bits, expected_bits, w.byte_pos);

  loxc_reader_t r;
  loxc_reader_init(&r, buffer, w.byte_pos);

  for (uint32_t i = 0; i < 100; i++) {
    uint32_t symbol = 0;
    rc = loxc_hier_decode(&h, &r, &symbol);
    assert(rc == LOXC_OK);
    assert(symbol == i);
  }

  loxc_hier_free(&h);
  free(freqs);
  printf("  ✓ PASS (round-trip OK)\n\n");
}

/* Test 3: HIER8 with 200 symbols (L0 + L1 + L2 + L3) */
static void test_hier8_200_symbols(void) {
  printf("Test 3: HIER8 with 200 symbols (L0 + L1 + L2 + L3)\n");

  loxc_freq_entry_t *freqs = (loxc_freq_entry_t *)malloc(200 * sizeof(loxc_freq_entry_t));
  assert(freqs != NULL);
  make_freqs_ordered(freqs, 200);

  loxc_hier_t h;
  int rc = loxc_hier_build(freqs, 200, LOXC_STRATEGY_HIERARCHICAL_8, &h);
  assert(rc == LOXC_OK);
  assert(h.symbol_count == 200);
  assert(h.level_count == 4); /* 56 L0, 56 L1, 56 L2, 32 L3 */

  uint8_t buffer[1024];
  loxc_writer_t w;
  loxc_writer_init(&w, buffer, sizeof(buffer));

  for (uint32_t i = 0; i < 200; i++) {
    rc = loxc_hier_encode(&h, i, &w);
    assert(rc == LOXC_OK);
  }

  rc = loxc_writer_flush(&w);
  assert(rc == LOXC_OK);
  size_t total_bits = w.byte_pos * 8;
  size_t expected_bits = 56 * 6 + 56 * 12 + 56 * 18 + 32 * 24; /* 2784, stored in 348 bytes = 2784 bits */
  printf("  Encoded bits: %zu (expected ~%zu, stored in %zu bytes)\n", total_bits, expected_bits, w.byte_pos);

  loxc_reader_t r;
  loxc_reader_init(&r, buffer, w.byte_pos);

  for (uint32_t i = 0; i < 200; i++) {
    uint32_t symbol = 0;
    rc = loxc_hier_decode(&h, &r, &symbol);
    assert(rc == LOXC_OK);
    assert(symbol == i);
  }

  loxc_hier_free(&h);
  free(freqs);
  printf("  ✓ PASS (round-trip OK)\n\n");
}

/* Test 4: HIER4 with 20 symbols (L0 + L1) */
static void test_hier4_20_symbols(void) {
  printf("Test 4: HIER4 with 20 symbols (L0 + L1)\n");

  loxc_freq_entry_t freqs[20];
  make_freqs_ordered(freqs, 20);

  loxc_hier_t h;
  int rc = loxc_hier_build(freqs, 20, LOXC_STRATEGY_HIERARCHICAL_4, &h);
  assert(rc == LOXC_OK);
  assert(h.symbol_count == 20);
  assert(h.level_count == 2); /* 15 on L0, 5 on L1 */
  assert(h.direct_slots == 15);
  assert(h.bits_per_level == 4);

  uint8_t buffer[128];
  loxc_writer_t w;
  loxc_writer_init(&w, buffer, sizeof(buffer));

  for (uint32_t i = 0; i < 20; i++) {
    rc = loxc_hier_encode(&h, i, &w);
    assert(rc == LOXC_OK);
  }

  rc = loxc_writer_flush(&w);
  assert(rc == LOXC_OK);
  size_t total_bits = w.byte_pos * 8;
  size_t expected_bits = 15 * 4 + 5 * 8; /* 100, stored in 13 bytes = 104 bits with padding */
  printf("  Encoded bits: %zu (expected ~%zu, stored in %zu bytes)\n", total_bits, expected_bits, w.byte_pos);

  loxc_reader_t r;
  loxc_reader_init(&r, buffer, w.byte_pos);

  for (uint32_t i = 0; i < 20; i++) {
    uint32_t symbol = 0;
    rc = loxc_hier_decode(&h, &r, &symbol);
    assert(rc == LOXC_OK);
    assert(symbol == i);
  }

  loxc_hier_free(&h);
  printf("  ✓ PASS (round-trip OK)\n\n");
}

/* Test 5: Edge case — symbol not in table */
static void test_edge_symbol_not_in_table(void) {
  printf("Test 5: Edge case — symbol not in table\n");

  loxc_freq_entry_t freqs[5];
  make_freqs_ordered(freqs, 5);

  loxc_hier_t h;
  int rc = loxc_hier_build(freqs, 5, LOXC_STRATEGY_HIERARCHICAL_8, &h);
  assert(rc == LOXC_OK);

  uint8_t buffer[64];
  loxc_writer_t w;
  loxc_writer_init(&w, buffer, sizeof(buffer));

  /* Try to encode a symbol that's not in the table */
  rc = loxc_hier_encode(&h, 999, &w);
  assert(rc == LOXC_ERR_SYMBOL_NOT_FOUND);

  loxc_hier_free(&h);
  printf("  ✓ PASS (correctly rejected symbol 999)\n\n");
}

/* Test 6: Edge case — truncated stream mid-escape chain */
static void test_edge_truncated_stream(void) {
  printf("Test 6: Edge case — truncated stream mid-escape chain\n");

  loxc_freq_entry_t *freqs = (loxc_freq_entry_t *)malloc(100 * sizeof(loxc_freq_entry_t));
  assert(freqs != NULL);
  make_freqs_ordered(freqs, 100);

  loxc_hier_t h;
  int rc = loxc_hier_build(freqs, 100, LOXC_STRATEGY_HIERARCHICAL_8, &h);
  assert(rc == LOXC_OK);

  uint8_t buffer[64];
  loxc_writer_t w;
  loxc_writer_init(&w, buffer, sizeof(buffer));

  /* Encode symbol 99 (on L1, will need escape + position = 12 bits total) */
  rc = loxc_hier_encode(&h, 99, &w);
  assert(rc == LOXC_OK);

  /* Manually truncate the buffer to only 4 bits (just the escape, missing the position) */
  rc = loxc_writer_flush(&w);
  assert(rc == LOXC_OK);

  /* Create a reader with truncated data (only 4 bits instead of 12) */
  loxc_reader_t r;
  loxc_reader_init(&r, buffer, 1); /* only 1 byte = 8 bits, which is 4 bits escape + 4 bits of garbage, but we'll read it all */

  uint32_t symbol = 0;
  rc = loxc_hier_decode(&h, &r, &symbol);
  /* After reading escape (4 bits) at L0, we loop to L1 and read 4 more bits.
     That's 8 bits total, which fits. The position read would be from the garbage bits.
     Then we'd try to read the 3rd level... but we're out of data. */

  /* Actually, let me reconsider: we encoded symbol 99, which is at rank 99.
     level = 99 / 56 = 1
     pos_in_level = 99 % 56 = 43
     So we write: escape_pos (6 bits) + 43 (6 bits) = 12 bits = 1.5 bytes
     Then we flush, which pads to 2 bytes = 16 bits.

     To test truncation: read only 1 byte (8 bits) = just the escape + first 2 bits of position.
     When we read 6 bits at L0, we get bits [0-5] = escape code.
     When we read 6 bits at L1, we need bits [6-11] but only bits [6-7] are available.
     loxc_read_bits will hit TRUNCATED. */

  assert(rc == LOXC_ERR_TRUNCATED);

  loxc_hier_free(&h);
  free(freqs);
  printf("  ✓ PASS (correctly detected truncation)\n\n");
}

int main(void) {
  printf("=== test_hier: Hierarchical Encoder/Decoder ===\n\n");

  test_hier8_30_symbols();
  test_hier8_100_symbols();
  test_hier8_200_symbols();
  test_hier4_20_symbols();
  test_edge_symbol_not_in_table();
  test_edge_truncated_stream();

  printf("=== test_hier: PASS (all scenarios) ===\n");

  return 0;
}
