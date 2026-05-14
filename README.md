# loxc

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C99](https://img.shields.io/badge/C-99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![CI](https://github.com/Vanderhell/loxc/actions/workflows/ci.yml/badge.svg)](https://github.com/Vanderhell/loxc/actions)

Frequency-optimized text compression with hierarchical matrices and
runtime-loadable dictionaries.

## Quick start

Start here:

- [docs/QUICKSTART.md](docs/QUICKSTART.md) for the 5-minute setup
- [examples/](examples/) for runnable sample programs
- [docs/COOKBOOK.md](docs/COOKBOOK.md) for copy-paste recipes

## Examples

See [examples/](examples/) for working code samples.

## Architecture

### Training

`loxc_train` analyzes the input text, counts byte-level frequencies and word
patterns, applies a greedy dictionary filter, and picks the cheapest encoding
strategy for the observed data.

### Strategies

- `FLAT_FIXED_WIDTH`: `ceil(log2(N))` bits per symbol, suitable for small or
  uniform alphabets
- `HIERARCHICAL_8`: 8x8 matrices with escape chaining, 6 bits per level,
  useful for medium alphabets with skewed distributions
- `HIERARCHICAL_4`: 4x4 matrices with escape chaining, 4 bits per level,
  useful for smaller alphabets

The selector chooses the cheapest strategy from the real measured data.

### `.loxctab`

Portable binary module table format:

```text
Magic "LOXT" + version + strategy parameters
byte_to_symbol[256]
symbols[N] (type + index)
dict_offsets + dict_data
CRC32
```

Tables can be loaded at runtime with `loxc_module_load_from_file()`.

### `.loxc`

Two output modes:

- External: header + compressed data. The decoder needs the matching table.
- Embedded: header + full table + compressed data. Self-contained and portable.

## Benchmark

Details: [BENCHMARKS.md](BENCHMARKS.md)

English corpus (`Pride and Prejudice`, 738 KB):

- External mode: 449 KB (60.8% of the original size)
- Encode: about 110 ms
- Decode: about 13 ms, roughly 8x faster than encode

`loxc` is not primarily optimized for compression ratio. The focus is fast
decoding through direct table lookup.

## Library API

The recommended entry point is the wrapper API in `loxc_simple.h`:

```c
#include "loxc_simple.h"

loxc_ctx_t *ctx = loxc_open("modules/loxc_demo.loxctab");

loxc_buffer_t compressed = loxc_compress_buffer(ctx, input, input_len, 0);
loxc_buffer_t restored =
    loxc_decompress_buffer(ctx, compressed.data, compressed.size);

loxc_buffer_free(&compressed);
loxc_buffer_free(&restored);
loxc_close(ctx);
```

For more wrapper-based recipes, see [docs/COOKBOOK.md](docs/COOKBOOK.md).

### Advanced API

The low-level API is still available for direct module and buffer control:

```c
#include "loxc.h"
#include "loxc_tab.h"

loxc_module_t *m = loxc_module_load_from_file("mytable.loxctab");
loxc_module_register(m);

uint8_t out[N];
size_t cap = sizeof(out), actual;
loxc_compress("mytable", input, input_len, out, &cap, &actual);

char restored[M];
size_t rcap = sizeof(restored), ractual;
loxc_decompress(out, actual, restored, &rcap, &ractual);

loxc_module_unload(m);
```

## Limitations

- Only one runtime-loaded module can be active at a time
- The module should match the input domain
- No Huffman or arithmetic coding yet, so compression ratio is weaker than
  gzip, but decode speed is higher

## Future work

- Multiple runtime modules in parallel
- Huffman strategy
- Context-aware encoding `P(Y|X)`
- Streaming API for files larger than RAM

## Build and test

```sh
make           # build libloxc.a and tools
make test      # run all test suites
make examples  # build example programs
```

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

## Related work

`loxc` combines known ideas from the compression literature. It implements an
adaptive family of `(s,c)`-Dense-Code-like layouts with automatic parameter
selection based on the training corpus:

- `HIER4`: `(15,1)` dense-code variant, 4 bits per level
- `HIER8`: `(56,8)` dense-code variant, 6 bits per level
- `FLAT`: fixed-width fallback, `ceil(log2(N))` bits per symbol

For future versions: `HIER16` would correspond to a `(240,16)` variant with
8 bits per level.

The selector chooses the best strategy through global cost analysis on the
actual corpus. That adaptive selection step is the main `loxc` contribution
relative to classic dense-code literature, where `s,c` parameters are usually
chosen manually or by one fixed heuristic.

### References and comparison

| Project | Architecture | Relation to `loxc` |
|---------|--------------|--------------------|
| **(s,c)-Dense Codes / ETDC / SCDC** | Prefix stop/continue code with fixed `b`-bit steps | Closest theoretical predecessor. `loxc` is an adaptive multi-`(s,c)` variant. |
| **FSST** | Trained 1-byte symbol table, decode via array lookup | Closest production codec. Used in DuckDB. Differs in being byte-aligned and random-access oriented. |
| **zstd dictionary mode** | LZ77 + Huff0/FSE with portable trained dictionary | Closest deployment model: train -> portable model -> runtime load. |
| **Shared Brotli RFC 9841** | Brotli with shared dictionaries, container supports external and embedded modes | Closest precedent for the external/embedded switch within one format. |

Links:

- FSST: https://github.com/cwida/fsst
- zstd: https://facebook.github.io/zstd/
- Shared Brotli: https://datatracker.ietf.org/doc/rfc9841/

## Honest positioning

`loxc` is not a new compression principle. It is a recombination of existing
ideas in one C99 codec:

- Dense-code-like prefix structure derived from `(s,c)` codes
- Adaptive per-corpus parameter selection
- Learned per-corpus tables, similar in spirit to FSST and dictionary-trained codecs
- External or embedded packaging, similar to shared-dictionary deployment models

### When to consider `loxc`

- Small to medium text payloads
- Strong similarity across documents
- Offline dictionary training is acceptable
- Embedded or IoT environments that benefit from a simple decoder
- Domain-specific text such as JSON APIs, log lines, URL paths, localization
  files, or telemetry

### When not to consider `loxc`

- Archival compression: use `zstd` or `brotli`
- Extremely fast string decode in databases: use `FSST`
- Random access into compressed blocks: use `FSST`
- One-shot compression without a training phase: use `gzip` or `zstd`

### Known limits

- Absolute decode throughput in MB/s has not yet been benchmarked against
  standard baselines such as `zstd`, `LZ4`, or `FSST`. That is planned for
  `v0.2`.
- Compression ratio is weaker than `gzip`, `zstd`, or `brotli` because there is
  no LZ77 backreference layer and no full entropy coder.
- If the input drifts significantly from the training corpus, the table should
  be retrained.

## License

MIT - see [LICENSE](LICENSE)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md)

## Security

See [SECURITY.md](SECURITY.md)
