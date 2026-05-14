# loxc Cookbook

Common recipes. Copy, adapt, and keep moving.

## Recipes

### 1. Compress a string to memory

```c
#include "loxc_simple.h"

loxc_ctx_t *ctx = loxc_open("table.loxctab");
loxc_buffer_t out = loxc_compress_buffer(ctx, data, len, 0);
/* use out.data and out.size */
loxc_buffer_free(&out);
loxc_close(ctx);
```

### 2. Compress a file

```c
loxc_ctx_t *ctx = loxc_open("table.loxctab");
int rc = loxc_compress_file(ctx, "input.txt", "output.loxc", 0);
if (rc != LOXC_OK) {
    fprintf(stderr, "Failed: %s\n", loxc_strerror(rc));
}
loxc_close(ctx);
```

### 3. Create a self-contained file

Pass `embed_table = 1`:

```c
loxc_compress_file(ctx, "in.txt", "out.loxc", 1);
```

The output `.loxc` includes the table, so the recipient does not need a
separate `.loxctab` file.

### 4. Decompress without knowing the original size

The wrapper allocates automatically:

```c
loxc_buffer_t restored = loxc_decompress_buffer(ctx, data, len);
/* restored.data is malloc'd to whatever size is needed */
```

### 5. Validate a `.loxc` file before processing

```c
if (loxc_check_file("incoming.loxc") != LOXC_OK) {
    fprintf(stderr, "Not a valid .loxc file\n");
    return 1;
}
```

### 6. Handle the "symbol not in module" error

This happens when input contains bytes the module was not trained for:

```c
loxc_buffer_t out = loxc_compress_buffer(ctx, data, len, 0);
if (out.error == LOXC_ERR_SYMBOL_NOT_FOUND) {
    /* Train a new module on similar data or preprocess the input. */
}
```

### 7. Train a module from multiple files

```bash
./tools/loxc_train \
    --input file1.txt \
    --input file2.txt \
    --input file3.txt \
    --output modules/loxc_combined \
    --module-name combined --module-id 30
```

This combines frequencies from all inputs into one table.

### 8. Switch between modules at runtime

```c
loxc_ctx_t *ctx1 = loxc_open("module_a.loxctab");
/* ... use ctx1 ... */
loxc_close(ctx1);

loxc_ctx_t *ctx2 = loxc_open("module_b.loxctab");
/* ... use ctx2 ... */
loxc_close(ctx2);
```

Note: only one runtime module is active at a time in `v0.1.x`.

### 9. Get human-readable error messages

```c
int rc = loxc_compress_file(ctx, "in", "out", 0);
if (rc != LOXC_OK) {
    fprintf(stderr, "Error %d: %s\n", rc, loxc_strerror(rc));
}
```

### 10. Use loxc in a build system

Add this to your `Makefile`:

```makefile
CFLAGS += -I/path/to/loxc/include -I/path/to/loxc/modules
LDFLAGS += /path/to/loxc/libloxc.a /path/to/loxc/modules/loxc_demo.o

myapp: myapp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
```
