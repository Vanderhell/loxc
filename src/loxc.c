#include "loxc.h"

#include <string.h>

#include "loxc_base.h"

enum { LOXC_REGISTRY_MAX_MODULES = 32 };

static const loxc_module_t *g_registry[LOXC_REGISTRY_MAX_MODULES];
static size_t g_registry_count = 0;

static const loxc_module_t *loxc_registry_find_by_name(const char *name) {
  if (name == NULL) return NULL;
  for (size_t i = 0; i < g_registry_count; i++) {
    const loxc_module_t *m = g_registry[i];
    if (m == NULL || m->name == NULL) continue;
    if (strcmp(m->name, name) == 0) return m;
  }
  return NULL;
}

static const loxc_module_t *loxc_registry_find_by_id(uint8_t module_id) {
  for (size_t i = 0; i < g_registry_count; i++) {
    const loxc_module_t *m = g_registry[i];
    if (m == NULL) continue;
    if (m->module_id == module_id) return m;
  }
  return NULL;
}

int loxc_module_register(const loxc_module_t *module) {
  if (module == NULL) return LOXC_ERR_NULL;
  if (module->name == NULL) return LOXC_ERR_INVALID_MODULE;
  if (module->encode == NULL || module->decode == NULL) return LOXC_ERR_INVALID_MODULE;

  if (loxc_registry_find_by_name(module->name) != NULL) return LOXC_ERR_DUPLICATE_MODULE;
  if (loxc_registry_find_by_id(module->module_id) != NULL) return LOXC_ERR_DUPLICATE_MODULE;

  if (g_registry_count >= LOXC_REGISTRY_MAX_MODULES) return LOXC_ERR_REGISTRY_FULL;
  g_registry[g_registry_count++] = module;
  return LOXC_OK;
}

int loxc_compress(const char *module_name, const char *input, size_t input_len,
                  uint8_t *output, size_t *output_capacity,
                  size_t *output_actual) {
  if (output_actual == NULL) return LOXC_ERR_NULL;
  *output_actual = 0;
  if (module_name == NULL || input == NULL || output == NULL ||
      output_capacity == NULL) {
    return LOXC_ERR_NULL;
  }

  const loxc_module_t *m = loxc_registry_find_by_name(module_name);
  if (m == NULL) return LOXC_ERR_MODULE_NOT_FOUND;

  return m->encode((const uint8_t *)input, input_len, output, *output_capacity,
                   output_actual);
}

int loxc_decompress(const uint8_t *input, size_t input_len, char *output,
                    size_t *output_capacity, size_t *output_actual) {
  if (output_actual == NULL) return LOXC_ERR_NULL;
  *output_actual = 0;
  if (input == NULL || output == NULL || output_capacity == NULL) return LOXC_ERR_NULL;

  loxc_reader_t r;
  int rc = loxc_reader_init(&r, input, input_len);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) return rc;

  const loxc_module_t *m = loxc_registry_find_by_id(h.module_id);
  if (m == NULL) return LOXC_ERR_MODULE_NOT_FOUND;

  return m->decode(input, input_len, (uint8_t *)output, *output_capacity,
                   output_actual);
}
