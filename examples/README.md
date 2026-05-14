# loxc Examples

Run all examples with:

```sh
make examples
./examples/01_hello_world
```

## Learning path

1. **01_hello_world.c** - Smallest possible usage
   The "compress a string, decompress it back" tutorial.
2. **02_compress_file.c** - File operations
   Take any text file, compress it, and time it.
3. **03_embedded_mode.c** - Self-contained `.loxc` files
   When you cannot ship a separate table.
4. **04_error_handling.c** - Robust error handling
   What errors mean and how to react to them.
5. **05_training_pipeline.c** - Train your own module
   End-to-end from raw corpus to working compression.
6. **06_compare_modes.c** - External vs embedded trade-off
   See the size difference between modes.
7. **07_streaming_chunks.c** - Working with large files
   Current limitations and workarounds.

## Common patterns

### Compress and forget

```c
loxc_ctx_t *ctx = loxc_open("table.loxctab");
loxc_compress_file(ctx, "in.txt", "out.loxc", 0);
loxc_close(ctx);
```

### In-memory round-trip

```c
loxc_buffer_t compressed = loxc_compress_buffer(ctx, data, len, 0);
loxc_buffer_t restored =
    loxc_decompress_buffer(ctx, compressed.data, compressed.size);
loxc_buffer_free(&compressed);
loxc_buffer_free(&restored);
```

### Self-contained file

```c
loxc_buffer_t out = loxc_compress_buffer(ctx, data, len, 1);
/* Write out.data to disk - it has everything needed to decompress */
```
