#include "loxc_plain.h"

#include <string.h>

#include "loxc_base.h"
#include "loxc_stream.h"
#include "loxc_types.h"

/*
 * NOTE: Tento modul je staticky generovaný "naivný baseline" — používa
 * fixed-width kódovanie s pevnou 8×8 maticou a bigramami. Všetky symboly
 * sú kódované na 8 bitov bez ohľadu na frekvenciu.
 *
 * Frekvenčne optimalizované a hierarchické moduly sa generujú dynamicky
 * cez `loxc_train` (Fáza 8) na základe trénincových dát.
 *
 * Tento modul slúži ako:
 *   - Referenčná implementácia pre testovanie formátu
 *   - Fallback pre prípady kde sa trénovanie neoplatí
 *   - Zálohový kompresér bez potreby analýzy dát
 */

enum {
  LOXC_PLAIN_HDR_SIZE_NOCRC = 15  /* v2 header without CRC: magic(3) + module_id(1) + version(1) + flags(1) + strategy_id(1) + data_len(2) + level_count(2) + reserved(4) */
};

static const loxc_matrix_value_t g_plain_data[256] = {
    /* Unused slots are ESCAPE by default (static storage is zero-initialized). */

    /* Common bigrams */
    [0] = {.codepoint = LOXC_BIGRAM_CP('t', 'h'), .type = LOXC_TYPE_BIGRAM},
    [1] = {.codepoint = LOXC_BIGRAM_CP('h', 'e'), .type = LOXC_TYPE_BIGRAM},
    [2] = {.codepoint = LOXC_BIGRAM_CP('i', 'n'), .type = LOXC_TYPE_BIGRAM},
    [3] = {.codepoint = LOXC_BIGRAM_CP('e', 'r'), .type = LOXC_TYPE_BIGRAM},
    [4] = {.codepoint = LOXC_BIGRAM_CP('a', 'n'), .type = LOXC_TYPE_BIGRAM},
    [5] = {.codepoint = LOXC_BIGRAM_CP('r', 'e'), .type = LOXC_TYPE_BIGRAM},
    [6] = {.codepoint = LOXC_BIGRAM_CP('o', 'n'), .type = LOXC_TYPE_BIGRAM},
    [7] = {.codepoint = LOXC_BIGRAM_CP('e', 'n'), .type = LOXC_TYPE_BIGRAM},
    [8] = {.codepoint = LOXC_BIGRAM_CP('a', 't'), .type = LOXC_TYPE_BIGRAM},
    [9] = {.codepoint = LOXC_BIGRAM_CP('e', 's'), .type = LOXC_TYPE_BIGRAM},

    /* Whitespace */
    [10] = {.codepoint = (uint32_t)' ', .type = LOXC_TYPE_CHAR},
    [11] = {.codepoint = (uint32_t)'\n', .type = LOXC_TYPE_CHAR},
    [12] = {.codepoint = (uint32_t)'\t', .type = LOXC_TYPE_CHAR},

    /* Digits */
    [13] = {.codepoint = (uint32_t)'0', .type = LOXC_TYPE_CHAR},
    [14] = {.codepoint = (uint32_t)'1', .type = LOXC_TYPE_CHAR},
    [15] = {.codepoint = (uint32_t)'2', .type = LOXC_TYPE_CHAR},
    [16] = {.codepoint = (uint32_t)'3', .type = LOXC_TYPE_CHAR},
    [17] = {.codepoint = (uint32_t)'4', .type = LOXC_TYPE_CHAR},
    [18] = {.codepoint = (uint32_t)'5', .type = LOXC_TYPE_CHAR},
    [19] = {.codepoint = (uint32_t)'6', .type = LOXC_TYPE_CHAR},
    [20] = {.codepoint = (uint32_t)'7', .type = LOXC_TYPE_CHAR},
    [21] = {.codepoint = (uint32_t)'8', .type = LOXC_TYPE_CHAR},
    [22] = {.codepoint = (uint32_t)'9', .type = LOXC_TYPE_CHAR},

    /* Lowercase a-z */
    [23] = {.codepoint = (uint32_t)'a', .type = LOXC_TYPE_CHAR},
    [24] = {.codepoint = (uint32_t)'b', .type = LOXC_TYPE_CHAR},
    [25] = {.codepoint = (uint32_t)'c', .type = LOXC_TYPE_CHAR},
    [26] = {.codepoint = (uint32_t)'d', .type = LOXC_TYPE_CHAR},
    [27] = {.codepoint = (uint32_t)'e', .type = LOXC_TYPE_CHAR},
    [28] = {.codepoint = (uint32_t)'f', .type = LOXC_TYPE_CHAR},
    [29] = {.codepoint = (uint32_t)'g', .type = LOXC_TYPE_CHAR},
    [30] = {.codepoint = (uint32_t)'h', .type = LOXC_TYPE_CHAR},
    [31] = {.codepoint = (uint32_t)'i', .type = LOXC_TYPE_CHAR},
    [32] = {.codepoint = (uint32_t)'j', .type = LOXC_TYPE_CHAR},
    [33] = {.codepoint = (uint32_t)'k', .type = LOXC_TYPE_CHAR},
    [34] = {.codepoint = (uint32_t)'l', .type = LOXC_TYPE_CHAR},
    [35] = {.codepoint = (uint32_t)'m', .type = LOXC_TYPE_CHAR},
    [36] = {.codepoint = (uint32_t)'n', .type = LOXC_TYPE_CHAR},
    [37] = {.codepoint = (uint32_t)'o', .type = LOXC_TYPE_CHAR},
    [38] = {.codepoint = (uint32_t)'p', .type = LOXC_TYPE_CHAR},
    [39] = {.codepoint = (uint32_t)'q', .type = LOXC_TYPE_CHAR},
    [40] = {.codepoint = (uint32_t)'r', .type = LOXC_TYPE_CHAR},
    [41] = {.codepoint = (uint32_t)'s', .type = LOXC_TYPE_CHAR},
    [42] = {.codepoint = (uint32_t)'t', .type = LOXC_TYPE_CHAR},
    [43] = {.codepoint = (uint32_t)'u', .type = LOXC_TYPE_CHAR},
    [44] = {.codepoint = (uint32_t)'v', .type = LOXC_TYPE_CHAR},
    [45] = {.codepoint = (uint32_t)'w', .type = LOXC_TYPE_CHAR},
    [46] = {.codepoint = (uint32_t)'x', .type = LOXC_TYPE_CHAR},
    [47] = {.codepoint = (uint32_t)'y', .type = LOXC_TYPE_CHAR},
    [48] = {.codepoint = (uint32_t)'z', .type = LOXC_TYPE_CHAR},

    /* Uppercase A-Z */
    [49] = {.codepoint = (uint32_t)'A', .type = LOXC_TYPE_CHAR},
    [50] = {.codepoint = (uint32_t)'B', .type = LOXC_TYPE_CHAR},
    [51] = {.codepoint = (uint32_t)'C', .type = LOXC_TYPE_CHAR},
    [52] = {.codepoint = (uint32_t)'D', .type = LOXC_TYPE_CHAR},
    [53] = {.codepoint = (uint32_t)'E', .type = LOXC_TYPE_CHAR},
    [54] = {.codepoint = (uint32_t)'F', .type = LOXC_TYPE_CHAR},
    [55] = {.codepoint = (uint32_t)'G', .type = LOXC_TYPE_CHAR},
    [56] = {.codepoint = (uint32_t)'H', .type = LOXC_TYPE_CHAR},
    [57] = {.codepoint = (uint32_t)'I', .type = LOXC_TYPE_CHAR},
    [58] = {.codepoint = (uint32_t)'J', .type = LOXC_TYPE_CHAR},
    [59] = {.codepoint = (uint32_t)'K', .type = LOXC_TYPE_CHAR},
    [60] = {.codepoint = (uint32_t)'L', .type = LOXC_TYPE_CHAR},
    [61] = {.codepoint = (uint32_t)'M', .type = LOXC_TYPE_CHAR},
    [62] = {.codepoint = (uint32_t)'N', .type = LOXC_TYPE_CHAR},
    [63] = {.codepoint = (uint32_t)'O', .type = LOXC_TYPE_CHAR},
    [64] = {.codepoint = (uint32_t)'P', .type = LOXC_TYPE_CHAR},
    [65] = {.codepoint = (uint32_t)'Q', .type = LOXC_TYPE_CHAR},
    [66] = {.codepoint = (uint32_t)'R', .type = LOXC_TYPE_CHAR},
    [67] = {.codepoint = (uint32_t)'S', .type = LOXC_TYPE_CHAR},
    [68] = {.codepoint = (uint32_t)'T', .type = LOXC_TYPE_CHAR},
    [69] = {.codepoint = (uint32_t)'U', .type = LOXC_TYPE_CHAR},
    [70] = {.codepoint = (uint32_t)'V', .type = LOXC_TYPE_CHAR},
    [71] = {.codepoint = (uint32_t)'W', .type = LOXC_TYPE_CHAR},
    [72] = {.codepoint = (uint32_t)'X', .type = LOXC_TYPE_CHAR},
    [73] = {.codepoint = (uint32_t)'Y', .type = LOXC_TYPE_CHAR},
    [74] = {.codepoint = (uint32_t)'Z', .type = LOXC_TYPE_CHAR},

    /* Basic punctuation: . , ! ? ; : ' \" - ( ) */
    [75] = {.codepoint = (uint32_t)'.', .type = LOXC_TYPE_CHAR},
    [76] = {.codepoint = (uint32_t)',', .type = LOXC_TYPE_CHAR},
    [77] = {.codepoint = (uint32_t)'!', .type = LOXC_TYPE_CHAR},
    [78] = {.codepoint = (uint32_t)'?', .type = LOXC_TYPE_CHAR},
    [79] = {.codepoint = (uint32_t)';', .type = LOXC_TYPE_CHAR},
    [80] = {.codepoint = (uint32_t)':', .type = LOXC_TYPE_CHAR},
    [81] = {.codepoint = (uint32_t)'\'', .type = LOXC_TYPE_CHAR},
    [82] = {.codepoint = (uint32_t)'"', .type = LOXC_TYPE_CHAR},
    [83] = {.codepoint = (uint32_t)'-', .type = LOXC_TYPE_CHAR},
    [84] = {.codepoint = (uint32_t)'(', .type = LOXC_TYPE_CHAR},
    [85] = {.codepoint = (uint32_t)')', .type = LOXC_TYPE_CHAR},
};

static const loxc_matrix_t g_plain_matrix = {
    .levels = 2,
    .level =
        {
            {.bits = 4, .dims = 1, .size = {16, 0, 0}},
            {.bits = 4, .dims = 1, .size = {16, 0, 0}},
        },
    .data = g_plain_data,
    .data_len = 256,
};

const loxc_matrix_t *loxc_plain_matrix(void) {
  return &g_plain_matrix;
}

static int loxc__plain_find_bigram_index(uint8_t c0, uint8_t c1,
                                        uint8_t *out_idx) {
  if (out_idx == NULL) return LOXC_ERR_NULL;
  for (uint32_t i = 0; i < 10; i++) {
    if (g_plain_data[i].type != LOXC_TYPE_BIGRAM) continue;
    uint32_t cp = g_plain_data[i].codepoint;
    if (((uint8_t)(cp >> 8)) == c0 && ((uint8_t)cp) == c1) {
      *out_idx = (uint8_t)i;
      return LOXC_OK;
    }
  }
  return LOXC_ERR_INVALID_MAGIC;
}

static int loxc__plain_find_char_index(uint8_t c, uint8_t *out_idx) {
  if (out_idx == NULL) return LOXC_ERR_NULL;
  for (uint32_t i = 10; i < 256; i++) {
    if (g_plain_data[i].type != LOXC_TYPE_CHAR) continue;
    if ((uint8_t)g_plain_data[i].codepoint == c) {
      *out_idx = (uint8_t)i;
      return LOXC_OK;
    }
  }
  return LOXC_ERR_INVALID_MAGIC;
}

int loxc_plain_encode(const uint8_t *in, size_t in_len, uint8_t *out,
                      size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;
  if (out_cap < LOXC_PLAIN_HDR_SIZE_NOCRC) return LOXC_ERR_OVERFLOW;
  if (in_len > 0xFFFFu) return LOXC_ERR_OVERFLOW;

  loxc_writer_t w;
  int rc = loxc_writer_init(&w, out + LOXC_PLAIN_HDR_SIZE_NOCRC,
                            out_cap - LOXC_PLAIN_HDR_SIZE_NOCRC);
  if (rc != LOXC_OK) return rc;

  size_t i = 0;
  while (i < in_len) {
    uint8_t idx = 0xFF;
    if (i + 1 < in_len) {
      if (loxc__plain_find_bigram_index(in[i], in[i + 1], &idx) == LOXC_OK) {
        rc = loxc_write_bits(&w, (uint32_t)idx, 8);
        if (rc != LOXC_OK) return rc;
        i += 2;
        continue;
      }
    }

    rc = loxc__plain_find_char_index(in[i], &idx);
    if (rc != LOXC_OK) return rc;
    rc = loxc_write_bits(&w, (uint32_t)idx, 8);
    if (rc != LOXC_OK) return rc;
    i++;
  }

  rc = loxc_writer_flush(&w);
  if (rc != LOXC_OK) return rc;

  size_t data_bytes = loxc_writer_size(&w);
  if (data_bytes > 0xFFFFu) return LOXC_ERR_OVERFLOW;

  loxc_writer_t hw;
  rc = loxc_writer_init(&hw, out, out_cap);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  h.module_id = LOXC_MODULE_PLAIN;
  h.version = 2;  /* v2 format (written by loxc_header_write) */
  h.flags = 0;
  h.strategy_id = LOXC_STRATEGY_FLAT_FIXED_WIDTH;
  h.data_len = (uint16_t)data_bytes;
  h.level_count = 0;
  for (int i = 0; i < 4; i++) h.reserved[i] = 0x00;
  h.crc32 = 0;

  rc = loxc_header_write(&hw, &h);
  if (rc != LOXC_OK) return rc;

  if (LOXC_PLAIN_HDR_SIZE_NOCRC + data_bytes > out_cap) return LOXC_ERR_OVERFLOW;
  /* Header already written into out[0..7], data is already in place at +8. */
  *out_len = LOXC_PLAIN_HDR_SIZE_NOCRC + data_bytes;
  return LOXC_OK;
}

int loxc_plain_decode(const uint8_t *in, size_t in_len, uint8_t *out,
                      size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return LOXC_ERR_NULL;
  *out_len = 0;
  if (in == NULL || out == NULL) return LOXC_ERR_NULL;

  loxc_reader_t hr;
  int rc = loxc_reader_init(&hr, in, in_len);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  rc = loxc_header_read(&hr, &h);
  if (rc != LOXC_OK) return rc;
  if (h.module_id != LOXC_MODULE_PLAIN) return LOXC_ERR_INVALID_MAGIC;
  if (h.version != 2) return LOXC_ERR_INVALID_MAGIC;  /* v2 format */
  if ((h.flags & (LOXC_FLAG_CRC | LOXC_FLAG_DICT)) != 0) return LOXC_ERR_INVALID_MAGIC;
  if (h.strategy_id != LOXC_STRATEGY_FLAT_FIXED_WIDTH) return LOXC_ERR_INVALID_MAGIC;
  if (h.level_count != 0) return LOXC_ERR_INVALID_MAGIC;

  size_t header_bytes = 15;  /* v2 header size without CRC */
  if (in_len < header_bytes) return LOXC_ERR_TRUNCATED;
  if ((size_t)h.data_len > (in_len - header_bytes)) return LOXC_ERR_TRUNCATED;

  loxc_reader_t r;
  rc = loxc_reader_init(&r, in + header_bytes, (size_t)h.data_len);
  if (rc != LOXC_OK) return rc;

  size_t out_pos = 0;
  while (!loxc_reader_eof(&r)) {
    uint32_t idx = 0;
    rc = loxc_read_bits(&r, 8, &idx);
    if (rc == LOXC_ERR_TRUNCATED) break;
    if (rc != LOXC_OK) return rc;
    if (idx >= g_plain_matrix.data_len) return LOXC_ERR_INVALID_MAGIC;

    loxc_matrix_value_t v = g_plain_matrix.data[idx];
    if (v.type == LOXC_TYPE_ESCAPE) return LOXC_ERR_INVALID_MAGIC;
    if (v.type == LOXC_TYPE_CHAR) {
      if (out_pos + 1 > out_cap) return LOXC_ERR_OVERFLOW;
      out[out_pos++] = (uint8_t)v.codepoint;
    } else if (v.type == LOXC_TYPE_BIGRAM) {
      if (out_pos + 2 > out_cap) return LOXC_ERR_OVERFLOW;
      out[out_pos++] = (uint8_t)(v.codepoint >> 8);
      out[out_pos++] = (uint8_t)(v.codepoint);
    } else {
      return LOXC_ERR_INVALID_MAGIC;
    }
  }

  *out_len = out_pos;
  return LOXC_OK;
}
