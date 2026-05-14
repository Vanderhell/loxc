# API Reference

This document covers both the recommended wrapper API and the lower-level API.

## Simple API (recommended)

The wrapper API lives in `include/loxc_simple.h`. It is the fastest path for
common use cases.

### `loxc_open`

```c
loxc_ctx_t *loxc_open(const char *table_path);
```

Loads a `.loxctab` file, registers the module, and returns a context handle.

### `loxc_close`

```c
void loxc_close(loxc_ctx_t *ctx);
```

Unregisters the module, frees the loaded table, and releases the wrapper
context.

### `loxc_compress_file`

```c
int loxc_compress_file(loxc_ctx_t *ctx, const char *input_path,
                       const char *output_path, int embed_table);
```

Compresses a file to `.loxc`. Use `embed_table = 1` for self-contained output.

### `loxc_decompress_file`

```c
int loxc_decompress_file(loxc_ctx_t *ctx, const char *input_path,
                         const char *output_path);
```

Decompresses a `.loxc` file to disk. For embedded `.loxc` input, `ctx` may be
`NULL`.

### `loxc_compress_buffer`

```c
loxc_buffer_t loxc_compress_buffer(loxc_ctx_t *ctx,
                                   const void *data, size_t len,
                                   int embed_table);
```

Compresses an in-memory buffer and returns an owned output buffer.

### `loxc_decompress_buffer`

```c
loxc_buffer_t loxc_decompress_buffer(loxc_ctx_t *ctx,
                                     const void *data, size_t len);
```

Decompresses an in-memory `.loxc` buffer and returns an owned output buffer.
For embedded input, `ctx` may be `NULL`.

### `loxc_buffer_free`

```c
void loxc_buffer_free(loxc_buffer_t *buf);
```

Frees the owned memory inside a `loxc_buffer_t` and resets the structure.

### `loxc_strerror`

```c
const char *loxc_strerror(int error_code);
```

Maps `LOXC_ERR_*` values to human-readable strings.

### `loxc_check_file`

```c
int loxc_check_file(const char *path);
```

Validates that a file looks like a readable `.loxc` container.

### Simple API example

```c
#include "loxc_simple.h"

loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
loxc_buffer_t compressed = loxc_compress_buffer(ctx, data, len, 0);
loxc_buffer_t restored =
    loxc_decompress_buffer(ctx, compressed.data, compressed.size);

loxc_buffer_free(&compressed);
loxc_buffer_free(&restored);
loxc_close(ctx);
```

## Advanced API

The advanced API is declared in `include/loxc.h` and `include/loxc_tab.h`.

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
