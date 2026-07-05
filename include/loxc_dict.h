#ifndef LOXC_DICT_H
#define LOXC_DICT_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_stream.h"

typedef struct {
  char *word;
  size_t word_len;
  uint32_t count;
  int64_t gain; /* bits */
  uint16_t ref_id;
} loxc_dict_entry_t;

typedef struct {
  loxc_dict_entry_t *entries;
  size_t count;
  uint8_t ref_bytes; /* 1 or 2 */
} loxc_dict_t;

int loxc_dict_analyze(const char *input, size_t len, loxc_dict_t *dict);

int loxc_dict_encode(const loxc_dict_t *dict, uint8_t module_id,
                     loxc_writer_t *w);
int loxc_dict_decode(loxc_reader_t *r, uint8_t module_id, loxc_dict_t *dict);

void loxc_dict_free(loxc_dict_t *dict);

#endif /* LOXC_DICT_H */
