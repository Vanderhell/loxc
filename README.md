# loxc

> **Train a compression model on your text. Ship it. Compress and decompress at hardware speed.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C99](https://img.shields.io/badge/C-99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![CI](https://github.com/Vanderhell/loxc/actions/workflows/ci.yml/badge.svg)](https://github.com/Vanderhell/loxc/actions)
[![Version](https://img.shields.io/badge/version-0.1.0-green.svg)](https://github.com/Vanderhell/loxc/releases/tag/v0.1.0)

A trainable, frequency-optimized text codec in pure C99.
Made for embedded systems, log pipelines, and any domain-specific text.

## At a glance

```text
Input:             "The quick brown fox jumps over the lazy dog"  (43 bytes)
Encoded:           29 bytes  (67% of original)
Decode:            < 0.1 ms
Compression ratio: ~60% on the bundled sample text corpus
Decode speed:      ~8x faster than encode
Decoder size:      ~5 KB compiled
Dependencies:      none (pure C99)
```

## Language agnostic

`loxc` has no built-in language assumptions. The included `demo` module is
trained on a public-domain text sample (`Pride and Prejudice`) for testing
purposes only. For your own data:

- **Slovak, Czech, Polish, and other natural language corpora**: train on your corpus
- **JSON, XML, and log lines**: train on representative samples of your format
- **URLs and file paths**: train on a representative set
- **Source code**: train on files from your codebase

The compression algorithm works on bytes, not characters or words. Any text-like
data with repeated patterns can compress well when the module is trained on a
matching corpus.

## Three lines to compress text

```c
loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
loxc_buffer_t out = loxc_compress_buffer(ctx, "Hello world!", 12, 0);
loxc_close(ctx);
```

That's it. `out.data` now holds compressed bytes.

[See full examples ->](examples/) | [5-minute tutorial ->](docs/QUICKSTART.md)

## Why loxc?

### Built for

- **Domain-specific text**: JSON APIs, log lines, URL paths, localization files
- **Embedded systems**: small decoder, no heavyweight runtime dependencies
- **Repeated payloads**: train once on your corpus, compress millions of similar messages
- **Predictable latency**: decode is table-driven, not entropy-decoder heavy

### Not for

- Archival compression, where `zstd` or `brotli` will give better ratios
- Random-access string storage in databases, where `FSST` is a better fit
- One-shot compression of unknown text, where `gzip` or `zstd` is simpler

## How it works

```text
TRAINING (offline, once per corpus)

your_corpus.txt  -->  loxc_train  -->  mytable.loxctab
    |
    +-- Counts byte frequencies
    +-- Extracts repeated phrases ("the", "and", "that", ...)
    +-- Greedy filter keeps only entries that reduce total output size
    `-- Picks best strategy:
        FLAT   (fixed-width)
        HIER4  (4x4 matrix with escapes)
        HIER8  (8x8 matrix with escapes)

RUNTIME (online, many times)

input text  -->  [encode via lookup tables]  -->  .loxc file
.loxc file  -->  [decode via lookup tables]  -->  output text
```

### The hierarchical matrix idea

Top-frequency symbols live in a 6-bit grid in `HIER8`:

```text
Position 0-55:  direct symbol (6 bits)
Position 56-63: ESCAPE -> read 6 more bits for the next grid

Frequent symbols:  [space] [e] [t] [o]   -> 6 bits each
Less frequent:     [q] [z] [x]           -> 12 bits each
Rare:                                      18 bits each
```

This is mathematically close to an adaptive **(56,8)-Dense Code** with
auto-selected parameters per corpus.

## Benchmarks

Tested on the bundled sample text corpus (`Pride and Prejudice`, 738 KB),
x86_64, `-O2`:

| Metric | Value |
|--------|-------|
| Compressed size | 449 KB |
| Compression ratio | 60.8% |
| Encode time | 110 ms |
| Decode time | 13 ms |
| Decode throughput | ~56 MB/s |

**Honest comparison** with established codecs. These are public headline numbers,
not same-hardware apples-to-apples measurements:

| Codec | Ratio | Decode speed |
|-------|-------|--------------|
| zstd -1 | ~36% | ~390 MB/s |
| LZ4 | ~48% | ~3.8 GB/s |
| FSST | ~50% | ~1-3 GB/s |
| **loxc** | **~60%** | **~56 MB/s*** |

> `*` Standardized absolute throughput benchmarking is planned for `v0.2`.

[Full benchmark details ->](BENCHMARKS.md)

## Quick start

### Build

```bash
git clone https://github.com/Vanderhell/loxc
cd loxc && make
```

### Use the CLI

```bash
./tools/loxc_cli compress \
    --table modules/loxc_demo.loxctab --embed \
    your_file.txt your_file.loxc

./tools/loxc_cli decompress your_file.loxc restored.txt
```

### Use as a library

```c
#include "loxc_simple.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");
    const char *text = "compress me";
    loxc_buffer_t out = loxc_compress_buffer(ctx, text, strlen(text), 0);

    printf("Original: %zu bytes, Compressed: %zu bytes\n",
           strlen(text), out.size);

    loxc_buffer_free(&out);
    loxc_close(ctx);
    return 0;
}
```

```bash
cc -Iinclude -Imodules myapp.c libloxc.a -o myapp && ./myapp
```

### Train on your own data

```bash
./tools/loxc_train \
    --input your_data.txt \
    --output modules/loxc_mytable \
    --module-name mytable --module-id 50
```

[Full tutorial ->](docs/QUICKSTART.md) | [Cookbook ->](docs/COOKBOOK.md)

## Examples

Working code in [examples/](examples/):

| # | File | Shows |
|---|------|-------|
| 1 | `01_hello_world.c` | Smallest possible usage |
| 2 | `02_compress_file.c` | File operations with timing |
| 3 | `03_embedded_mode.c` | Self-contained `.loxc` files |
| 4 | `04_error_handling.c` | Error handling paths |
| 5 | `05_training_pipeline.c` | Train and use a custom module |
| 6 | `06_compare_modes.c` | External vs embedded size tradeoff |
| 7 | `07_streaming_chunks.c` | Current large-file workaround |

Run them with `make examples && ./examples/01_hello_world`.

## API

### Simple API (recommended)

```c
loxc_ctx_t *loxc_open(const char *table_path);
void        loxc_close(loxc_ctx_t *ctx);

int loxc_compress_file(loxc_ctx_t *ctx, const char *in_path,
                       const char *out_path, int embed_table);
int loxc_decompress_file(loxc_ctx_t *ctx, const char *in_path,
                         const char *out_path);

loxc_buffer_t loxc_compress_buffer(loxc_ctx_t *ctx,
                                   const void *data, size_t len,
                                   int embed_table);
loxc_buffer_t loxc_decompress_buffer(loxc_ctx_t *ctx,
                                     const void *data, size_t len);
void          loxc_buffer_free(loxc_buffer_t *buf);

const char   *loxc_strerror(int code);
```

[Full API reference ->](docs/API.md)

### Advanced API

For direct registry and buffer control, see [docs/API.md#advanced-api](docs/API.md#advanced-api).

## Architecture

```text
+-----------------------------------------------------------+
| Application                                               |
|  +-----------------------------------------------+        |
|  | loxc_simple.h  (recommended)                  |        |
|  | loxc.h         (low-level)                    |        |
|  +-----------------------------------------------+        |
+--------------------------+--------------------------------+
                           |
                           v
+-----------------------------------------------------------+
| libloxc.a                                                 |
|  +--------------+  +---------------+  +----------------+ |
|  | Strategy     |  | Hierarchical  |  | Stream Reader/ | |
|  | Selector     |  | Encoder/      |  | Writer         | |
|  |              |  | Decoder       |  |                | |
|  +--------------+  +---------------+  +----------------+ |
|  +--------------+  +---------------+                     |
|  | Dictionary   |  | Module        |                     |
|  | Filter       |  | Registry      |                     |
|  +--------------+  +---------------+                     |
+--------------------------+--------------------------------+
                           |
                           v
+-----------------------------------------------------------+
| Module tables (.loxctab files)                            |
| Hardcoded C modules or runtime-loaded portable tables     |
+-----------------------------------------------------------+
```

[Full architecture ->](docs/ARCHITECTURE.md)

## File structure

```text
include/    public headers
src/        library implementation
tools/      loxc_train, loxc_cli, loxc_bench
tests/      unit tests
modules/    generated modules (git-ignored)
benchmarks/ benchmark inputs
trainings/  training data
examples/   runnable example programs
docs/       implementation documentation
```

## Roadmap

- `v0.1.0` - Initial release
- `v0.2.0` - Absolute MB/s benchmarks, Huffman strategy
- `v0.3.0` - Multi-module support, streaming API
- `v1.0.0` - Production-stable release

## Related work

[Detailed comparison with Dense Codes, FSST, zstd dictionary mode, and Shared Brotli ->](docs/RELATED_WORK.md)

Briefly, `loxc` is **not a new compression principle**. It is a practical
recombination of:

- Dense-code-like prefix structure
- Learned per-corpus symbol tables
- Trained dictionary deployment
- External or embedded packaging

## License

MIT - see [LICENSE](LICENSE)

## Contributing

PRs welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

Questions or bug reports: [open an issue](https://github.com/Vanderhell/loxc/issues).
