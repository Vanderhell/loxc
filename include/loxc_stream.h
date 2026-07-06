#ifndef LOXC_STREAM_H
#define LOXC_STREAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t *buf;
  size_t cap;
  size_t byte_pos;
  uint8_t bit_pos; /* 0-7 */
} loxc_writer_t;

typedef struct {
  const uint8_t *buf;
  size_t len;
  size_t byte_pos;
  uint8_t bit_pos; /* 0-7 */
} loxc_reader_t;

int loxc_writer_init(loxc_writer_t *w, uint8_t *buf, size_t cap);
int loxc_write_bit(loxc_writer_t *w, uint8_t bit);
int loxc_write_bits(loxc_writer_t *w, uint32_t bits, uint8_t n);
int loxc_write_bytes(loxc_writer_t *w, const uint8_t *data, size_t len);
int loxc_writer_flush(loxc_writer_t *w);
size_t loxc_writer_size(const loxc_writer_t *w);

int loxc_reader_init(loxc_reader_t *r, const uint8_t *buf, size_t len);
int loxc_read_bit(loxc_reader_t *r, uint8_t *out);
int loxc_read_bits(loxc_reader_t *r, uint8_t n, uint32_t *out);
int loxc_read_bytes(loxc_reader_t *r, uint8_t *out, size_t len);
int loxc_reader_eof(const loxc_reader_t *r);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOXC_STREAM_H */
