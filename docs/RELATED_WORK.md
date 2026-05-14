# Related Work

`loxc` combines known ideas from the compression literature. It implements an
adaptive family of `(s,c)`-Dense-Code-like layouts with automatic parameter
selection based on the training corpus:

- `HIER4`: `(15,1)` dense-code variant, 4 bits per level
- `HIER8`: `(56,8)` dense-code variant, 6 bits per level
- `FLAT`: fixed-width fallback, `ceil(log2(N))` bits per symbol

For future versions, `HIER16` would correspond to a `(240,16)` variant with
8 bits per level.

The selector chooses the best strategy through global cost analysis on the
actual corpus. That adaptive selection step is the main `loxc` contribution
relative to classic dense-code literature, where `s,c` parameters are usually
chosen manually or by one fixed heuristic.

## References and comparison

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
