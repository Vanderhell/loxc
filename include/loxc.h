#ifndef LOXC_H
#define LOXC_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_types.h"

typedef struct loxc_module {
  const char *name;
  uint8_t module_id;
  uint8_t version;
  uint8_t strategy_id;

  int (*encode)(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                size_t *out_len);
  int (*decode)(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                size_t *out_len);
} loxc_module_t;

/* Modul sa zaregistruje pri svojom startup (constructor alebo explicitne). */
int loxc_module_register(const loxc_module_t *module);

/* Univerzálne compress/decompress. */
int loxc_compress(const char *module_name, const char *input, size_t input_len,
                  uint8_t *output, size_t *output_capacity,
                  size_t *output_actual);

int loxc_decompress(const uint8_t *input, size_t input_len, char *output,
                    size_t *output_capacity, size_t *output_actual);

#endif /* LOXC_H */
