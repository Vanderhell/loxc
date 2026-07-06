#ifndef LOXC_MATRIX_H
#define LOXC_MATRIX_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_stream.h"
#include "loxc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A single matrix level */
typedef struct {
  uint8_t bits; /* how many bits this level consumes (e.g. 3 -> 8 cells) */
  uint8_t dims; /* 1 = 1D, 2 = 2D, 3 = 3D */
  uint8_t size[3]; /* each dimension size (power of 2) */
} loxc_matrix_level_t;

/* Leaf decoded value */
typedef struct {
  uint32_t codepoint;
  uint8_t type;
} loxc_matrix_value_t;

enum {
  LOXC_TYPE_CHAR = 1,
  LOXC_TYPE_BIGRAM = 2,
  LOXC_TYPE_REF = 3,
  LOXC_TYPE_ESCAPE = 4
};

/* Full matrix hierarchy for one module */
typedef struct {
  uint8_t levels;                 /* number of levels */
  loxc_matrix_level_t level[8];   /* max 8 levels */
  const loxc_matrix_value_t *data; /* flat array, row-major across levels */
  size_t data_len;
} loxc_matrix_t;

int loxc_matrix_decode(const loxc_matrix_t *m, loxc_reader_t *r,
                       loxc_matrix_value_t *out);
int loxc_matrix_encode(const loxc_matrix_t *m, uint32_t codepoint,
                       loxc_writer_t *w);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOXC_MATRIX_H */
