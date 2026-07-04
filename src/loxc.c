#include "loxc.h"

#include <stdlib.h>
#include <string.h>

#include "loxc_base.h"
#include "loxc_tab.h"

int loxc_loaded_module_encode(const loxc_module_t *module,
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_cap,
                              size_t *out_len);
int loxc_loaded_module_decode(const loxc_module_t *module,
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_cap,
                              size_t *out_len);

enum { LOXC_REGISTRY_MAX_MODULES = 32 };

typedef struct {
  const loxc_module_t *module;
  size_t active_ops;
} loxc_registry_entry_t;

static loxc_registry_entry_t g_registry[LOXC_REGISTRY_MAX_MODULES];
static size_t g_registry_count = 0;

static loxc_registry_entry_t *loxc_registry_find_by_name(const char *name) {
  if (name == NULL) return NULL;
  for (size_t i = 0; i < g_registry_count; i++) {
    loxc_registry_entry_t *entry = &g_registry[i];
    const loxc_module_t *m = entry->module;
    if (m == NULL || m->name == NULL) continue;
    if (strcmp(m->name, name) == 0) return entry;
  }
  return NULL;
}

static loxc_registry_entry_t *loxc_registry_find_by_id(uint8_t module_id) {
  for (size_t i = 0; i < g_registry_count; i++) {
    loxc_registry_entry_t *entry = &g_registry[i];
    const loxc_module_t *m = entry->module;
    if (m == NULL) continue;
    if (m->module_id == module_id) return entry;
  }
  return NULL;
}

static void loxc_registry_release(loxc_registry_entry_t *entry) {
  if (entry == NULL || entry->active_ops == 0u) return;
  entry->active_ops--;
}

static int loxc_registry_acquire_by_name(const char *name,
                                         loxc_registry_entry_t **out_entry) {
  loxc_registry_entry_t *entry = NULL;

  if (out_entry == NULL) return LOXC_ERR_NULL;
  *out_entry = NULL;

  entry = loxc_registry_find_by_name(name);
  if (entry == NULL) return LOXC_ERR_MODULE_NOT_FOUND;

  entry->active_ops++;
  *out_entry = entry;
  return LOXC_OK;
}

static int loxc_registry_acquire_by_id(uint8_t module_id,
                                       loxc_registry_entry_t **out_entry) {
  loxc_registry_entry_t *entry = NULL;

  if (out_entry == NULL) return LOXC_ERR_NULL;
  *out_entry = NULL;

  entry = loxc_registry_find_by_id(module_id);
  if (entry == NULL) return LOXC_ERR_MODULE_NOT_FOUND;

  entry->active_ops++;
  *out_entry = entry;
  return LOXC_OK;
}

int loxc_registry_module_status(const loxc_module_t *module,
                                int *out_registered,
                                int *out_busy) {
  int registered = 0;
  int busy = 0;

  if (out_registered == NULL || out_busy == NULL) return LOXC_ERR_NULL;
  *out_registered = 0;
  *out_busy = 0;
  if (module == NULL) return LOXC_ERR_NULL;

  for (size_t i = 0; i < g_registry_count; i++) {
    if (g_registry[i].module != module) continue;
    registered = 1;
    busy = (g_registry[i].active_ops != 0u);
    break;
  }

  *out_registered = registered;
  *out_busy = busy;
  return LOXC_OK;
}

static uint32_t loxc__read_u32_le_mem(const uint8_t *p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8u) |
         ((uint32_t)p[2] << 16u) |
         ((uint32_t)p[3] << 24u);
}

int loxc_module_register(const loxc_module_t *module) {
  if (module == NULL) return LOXC_ERR_NULL;
  if (module->name == NULL) return LOXC_ERR_INVALID_MODULE;
  if (module->encode == NULL || module->decode == NULL) return LOXC_ERR_INVALID_MODULE;

  if (loxc_registry_find_by_name(module->name) != NULL) return LOXC_ERR_DUPLICATE_MODULE;
  if (loxc_registry_find_by_id(module->module_id) != NULL) return LOXC_ERR_DUPLICATE_MODULE;

  if (g_registry_count >= LOXC_REGISTRY_MAX_MODULES) return LOXC_ERR_REGISTRY_FULL;
  g_registry[g_registry_count].module = module;
  g_registry[g_registry_count].active_ops = 0u;
  g_registry_count++;
  return LOXC_OK;
}

int loxc_module_unregister(const char *module_name) {
  size_t i = 0;

  if (module_name == NULL) return LOXC_ERR_NULL;

  for (i = 0; i < g_registry_count; i++) {
    const loxc_module_t *m = g_registry[i].module;
    if (m == NULL || m->name == NULL) continue;
    if (strcmp(m->name, module_name) != 0) continue;
    if (g_registry[i].active_ops != 0u) return LOXC_ERR_BUSY;

    for (; i + 1u < g_registry_count; i++) {
      g_registry[i] = g_registry[i + 1u];
    }
    g_registry[g_registry_count - 1u].module = NULL;
    g_registry[g_registry_count - 1u].active_ops = 0u;
    g_registry_count--;
    return LOXC_OK;
  }

  return LOXC_ERR_MODULE_NOT_FOUND;
}

int loxc_compress(const char *module_name, const char *input, size_t input_len,
                  uint8_t *output, size_t *output_capacity,
                  size_t *output_actual) {
  return loxc_compress_with_options(module_name, input, input_len, output,
                                    output_capacity, output_actual, 0);
}

int loxc_compress_with_options(const char *module_name,
                               const char *input, size_t input_len,
                               uint8_t *output, size_t *output_capacity,
                               size_t *output_actual,
                               int embed_table) {
  if (output_actual == NULL) return LOXC_ERR_NULL;
  *output_actual = 0;
  if (module_name == NULL || input == NULL || output == NULL ||
      output_capacity == NULL) {
    return LOXC_ERR_NULL;
  }

  int rc = LOXC_OK;
  loxc_registry_entry_t *entry = NULL;
  rc = loxc_registry_acquire_by_name(module_name, &entry);
  if (rc != LOXC_OK) return rc;
  const loxc_module_t *m = entry->module;
  if (embed_table == 0) {
    rc = loxc_loaded_module_encode(m, (const uint8_t *)input, input_len, output,
                                   *output_capacity, output_actual);
    if (rc == LOXC_ERR_INVALID_MODULE) {
      rc = m->encode((const uint8_t *)input, input_len, output, *output_capacity,
                     output_actual);
    }
    loxc_registry_release(entry);
    return rc;
  }

  const uint8_t *tab_blob = NULL;
  size_t tab_size = 0;
  rc = loxc_module_get_table_blob(m, &tab_blob, &tab_size);
  if (rc != LOXC_OK) {
    loxc_registry_release(entry);
    return LOXC_ERR_INVALID_FORMAT;
  }

  uint8_t *encoded = (uint8_t *)malloc(*output_capacity);
  if (encoded == NULL) {
    loxc_registry_release(entry);
    return LOXC_ERR_OVERFLOW;
  }

  size_t encoded_len = 0;
  rc = loxc_loaded_module_encode(m, (const uint8_t *)input, input_len, encoded,
                                 *output_capacity, &encoded_len);
  if (rc != LOXC_OK) {
    free(encoded);
    loxc_registry_release(entry);
    return rc;
  }

  const size_t header_bytes = LOXC_HEADER_SIZE_V2;
  if (encoded_len < header_bytes) {
    free(encoded);
    loxc_registry_release(entry);
    return LOXC_ERR_INVALID_FORMAT;
  }
  const size_t payload_len = encoded_len - header_bytes;
  if (tab_size > SIZE_MAX - header_bytes ||
      payload_len > SIZE_MAX - header_bytes - tab_size) {
    free(encoded);
    loxc_registry_release(entry);
    return LOXC_ERR_OVERFLOW;
  }
  const size_t total = header_bytes + tab_size + payload_len;
  if (total > *output_capacity) {
    free(encoded);
    loxc_registry_release(entry);
    return LOXC_ERR_OVERFLOW;
  }

  loxc_reader_t hr;
  rc = loxc_reader_init(&hr, encoded, encoded_len);
  if (rc != LOXC_OK) {
    free(encoded);
    loxc_registry_release(entry);
    return rc;
  }
  loxc_header_t h;
  rc = loxc_header_read(&hr, &h);
  if (rc != LOXC_OK) {
    free(encoded);
    loxc_registry_release(entry);
    return rc;
  }
  h.flags = (uint8_t)(h.flags | LOXC_FLAG_EMBEDDED_TABLE);

  loxc_writer_t hw;
  rc = loxc_writer_init(&hw, output, *output_capacity);
  if (rc != LOXC_OK) {
    free(encoded);
    loxc_registry_release(entry);
    return rc;
  }
  rc = loxc_header_write(&hw, &h);
  if (rc != LOXC_OK) {
    free(encoded);
    loxc_registry_release(entry);
    return rc;
  }
  if (loxc_writer_size(&hw) != header_bytes) {
    free(encoded);
    loxc_registry_release(entry);
    return LOXC_ERR_INVALID_FORMAT;
  }

  memcpy(output + header_bytes, tab_blob, tab_size);
  memcpy(output + header_bytes + tab_size, encoded + header_bytes, payload_len);
  *output_actual = total;
  free(encoded);
  loxc_registry_release(entry);
  return LOXC_OK;
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

  if ((h.flags & LOXC_FLAG_EMBEDDED_TABLE) != 0) {
    const size_t header_bytes = loxc_header_size(&h);
    if (input_len < header_bytes + LOXC_TAB_HEADER_SIZE + LOXC_TAB_TRAILER_SIZE) {
      return LOXC_ERR_TRUNCATED;
    }

    const uint8_t *blob_start = input + header_bytes;
    if (memcmp(blob_start, LOXC_TAB_MAGIC, 4) != 0) return LOXC_ERR_INVALID_FORMAT;
    const uint32_t blob_data_size = loxc__read_u32_le_mem(blob_start + 20u);
    const size_t blob_total =
        (size_t)LOXC_TAB_HEADER_SIZE + (size_t)blob_data_size +
        (size_t)LOXC_TAB_TRAILER_SIZE;
    if (blob_total > input_len - header_bytes) return LOXC_ERR_TRUNCATED;

    loxc_module_t *m =
        loxc_module_load_from_memory(blob_start, blob_total, "embedded_temp");
    if (m == NULL) return LOXC_ERR_INVALID_FORMAT;

    const uint8_t *payload = blob_start + blob_total;
    size_t payload_len = 0;
    rc = loxc_header_resolve_payload_len(&h, input_len - header_bytes - blob_total,
                                         &payload_len);
    if (rc != LOXC_OK) {
      (void)loxc_module_unload(m);
      return rc;
    }
    if (payload_len > SIZE_MAX - header_bytes) {
      (void)loxc_module_unload(m);
      return LOXC_ERR_OVERFLOW;
    }

    uint8_t *compact = (uint8_t *)malloc(header_bytes + payload_len);
    if (compact == NULL) {
      (void)loxc_module_unload(m);
      return LOXC_ERR_OVERFLOW;
    }
    loxc_header_t compact_header = h;
    compact_header.flags =
        (uint8_t)(compact_header.flags & (uint8_t)~LOXC_FLAG_EMBEDDED_TABLE);
    loxc_writer_t compact_writer;
    rc = loxc_writer_init(&compact_writer, compact, header_bytes);
    if (rc != LOXC_OK) {
      free(compact);
      (void)loxc_module_unload(m);
      return rc;
    }
    rc = loxc_header_write(&compact_writer, &compact_header);
    if (rc != LOXC_OK || loxc_writer_size(&compact_writer) != header_bytes) {
      free(compact);
      (void)loxc_module_unload(m);
      return (rc != LOXC_OK) ? rc : LOXC_ERR_INVALID_FORMAT;
    }
    memcpy(compact + header_bytes, payload, payload_len);

    rc = m->decode(compact, header_bytes + payload_len, (uint8_t *)output,
                   *output_capacity, output_actual);
    if (rc == LOXC_ERR_INVALID_MODULE) {
      rc = loxc_loaded_module_decode(m, compact, header_bytes + payload_len,
                                     (uint8_t *)output, *output_capacity,
                                     output_actual);
    }
    free(compact);
    (void)loxc_module_unload(m);
    return rc;
  }

  loxc_registry_entry_t *entry = NULL;
  rc = loxc_registry_acquire_by_id(h.module_id, &entry);
  if (rc != LOXC_OK) return rc;
  const loxc_module_t *m = entry->module;

  rc = loxc_loaded_module_decode(m, input, input_len, (uint8_t *)output,
                                 *output_capacity, output_actual);
  if (rc == LOXC_ERR_INVALID_MODULE) {
    rc = m->decode(input, input_len, (uint8_t *)output, *output_capacity,
                   output_actual);
  }
  loxc_registry_release(entry);

  return rc;
}
