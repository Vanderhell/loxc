#ifndef LOXC_H
#define LOXC_H

#include <stddef.h>
#include <stdint.h>

#include "loxc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loxc_module {
  const char *name;
  const char *table_name;
  uint8_t module_id;
  uint8_t version;
  uint8_t strategy_id;
  uint16_t level_count;
  uint32_t table_fingerprint;

  int (*encode)(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                size_t *out_len);
  int (*decode)(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                size_t *out_len);
  void *private_data;
} loxc_module_t;

/*
 * Legacy process-global registry contract:
 * - zero-initialized, no explicit init/shutdown API
 * - maximum of 32 registered modules
 * - stores borrowed module/name pointers owned by the caller
 * - not verified as thread-safe; callers must externally synchronize registry
 *   mutation and module lifetime if multiple threads are involved
 * - unregister is rejected while a module operation on that entry is active
 */
int loxc_module_register(const loxc_module_t *module);
int loxc_module_unregister(const char *module_name);

/* Univerzálne compress/decompress. */
int loxc_compress(const char *module_name, const char *input, size_t input_len,
                  uint8_t *output, size_t *output_capacity,
                  size_t *output_actual);

int loxc_compress_with_options(const char *module_name,
                               const char *input, size_t input_len,
                               uint8_t *output, size_t *output_capacity,
                               size_t *output_actual,
                               int embed_table);

int loxc_module_get_table_blob(const loxc_module_t *module,
                               const uint8_t **out_blob,
                               size_t *out_size);

int loxc_decompress(const uint8_t *input, size_t input_len, char *output,
                    size_t *output_capacity, size_t *output_actual);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LOXC_H */
