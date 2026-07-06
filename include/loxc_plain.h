#ifndef LOXC_PLAIN_H
#define LOXC_PLAIN_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  LOXC_MODULE_PLAIN = 0x01
};

/* Bigram codepoint packing: (c0<<8) | c1, type=LOXC_TYPE_BIGRAM */
#define LOXC_BIGRAM_CP(c0, c1) \
  ((uint32_t)((((uint32_t)(uint8_t)(c0)) << 8) | (uint32_t)(uint8_t)(c1)))

const loxc_matrix_t *loxc_plain_matrix(void);

/* Encode/decode plain ASCII text using the plain matrix.
   Output format:
     [loxc_header_t][encoded data bytes]
   Flags: CRC=no, DICT=no (for now).
*/
int loxc_plain_encode(const uint8_t *in, size_t in_len, uint8_t *out,
                      size_t out_cap, size_t *out_len);
int loxc_plain_decode(const uint8_t *in, size_t in_len, uint8_t *out,
                      size_t out_cap, size_t *out_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOXC_PLAIN_H */
