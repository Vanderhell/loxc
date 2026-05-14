# API Reference

This document covers the public functions declared in `include/loxc.h`.

## `loxc_module_register`

```c
int loxc_module_register(const loxc_module_t *module);
```

Registers a module in the global registry.

Returns:
- `LOXC_OK` on success
- an error code on failure

## `loxc_compress`

```c
int loxc_compress(const char *module_name,
                  const char *input, size_t input_len,
                  uint8_t *output, size_t *output_capacity,
                  size_t *output_actual);
```

Compresses text using a registered module.

Notes:
- `output_capacity` is both input and output
- `output_actual` receives the produced size

## `loxc_compress_with_options`

```c
int loxc_compress_with_options(const char *module_name,
                               const char *input, size_t input_len,
                               uint8_t *output, size_t *output_capacity,
                               size_t *output_actual,
                               int embed_table);
```

Same as `loxc_compress`, with optional embedded-table output.

## `loxc_module_get_table_blob`

```c
int loxc_module_get_table_blob(const loxc_module_t *module,
                               const uint8_t **out_blob,
                               size_t *out_size);
```

Returns the raw `.loxctab` blob for runtime-loaded modules.

## `loxc_decompress`

```c
int loxc_decompress(const uint8_t *input, size_t input_len,
                    char *output, size_t *output_capacity,
                    size_t *output_actual);
```

Decompresses a `.loxc` payload back into text.

Behavior:
- embedded files auto-load their own table
- external files require the matching module to be registered

## Common return codes

- `LOXC_OK`
- `LOXC_ERR_NULL`
- `LOXC_ERR_TRUNCATED`
- `LOXC_ERR_OVERFLOW`
- `LOXC_ERR_INVALID_MAGIC`
- `LOXC_ERR_SYMBOL_NOT_FOUND`
- `LOXC_ERR_INVALID_FORMAT`
- `LOXC_ERR_MODULE_NOT_FOUND`
- `LOXC_ERR_REGISTRY_FULL`
- `LOXC_ERR_DUPLICATE_MODULE`
- `LOXC_ERR_INVALID_MODULE`

## Example

```c
loxc_module_t *m = loxc_module_load_from_file("modules/loxc_demo.loxctab");
loxc_module_register(m);

uint8_t out[4096];
size_t out_cap = sizeof(out);
size_t out_len = 0;
loxc_compress("demo", "hello", 5, out, &out_cap, &out_len);

loxc_module_unload(m);
```
