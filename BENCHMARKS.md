# Benchmarks

This document describes how `loxc` performance is measured, what is measured,
and how to reproduce the numbers yourself. It is structured so that the same
single command (`make bench-full`) regenerates everything: corpora, trained
modules, loxc results, baseline-compressor results, and the final report.

## Current run snapshot

The current integrated benchmark pass (`make bench-full ITER=100`) produced:

- Host: `Vanderhell`
- CPU: `Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz`
- Memory: `64251 MiB`
- OS: `Linux 6.6.87.2-microsoft-standard-WSL2 / x86_64`
- Compiler: `cc 13.3.0`
- Flags: `-std=c99 -Wall -Wextra -O2`
- Iterations: `100` timed iterations after `3` warmup iterations
- Command: `make bench-full ITER=100`

Headline rows from that run:

| file | tool | ratio | encode | decode |
|---|---|---|---|---|
| `trainings/demo_corpus.txt` | `loxc-ext(demo)` | `60.8%` | `114.55 ms / 6.1 MB/s` | `13.72 ms / 51.3 MB/s` |
| `benchmarks/plain_sample_text.txt` | `loxc-ext(demo)` | `62.2%` | `4.80 ms / 6.0 MB/s` | `0.55 ms / 51.8 MB/s` |
| `benchmarks/corpora/json_test.json` | `loxc-ext(json)` | `62.9%` | see `bench_out/DOMAIN_WINS.md` | `77.7 MB/s` |
| `benchmarks/corpora/csrc_test.c` | `loxc-ext(csrc)` | `45.1%` | see `bench_out/DOMAIN_WINS.md` | `83.6 MB/s` |
| `trainings/demo_corpus.txt` | `gzip:6` | `36.0%` | `74.69 ms / 9.4 MB/s` | `9.63 ms / 73.1 MB/s` |
| `trainings/demo_corpus.txt` | `xz:6` | `29.3%` | `390.71 ms / 1.8 MB/s` | `17.44 ms / 40.4 MB/s` |

The full generated report for this run is written to:

- `bench_out/BENCHMARKS_FULL.md`
- `bench_out/DOMAIN_WINS.md`
- `bench_out/merged.csv`

The run is reproducible from the following pinned inputs and module tables:

| Kind | Path | SHA-256 |
|---|---|---|
| corpus | `trainings/demo_corpus.txt` | `A5666F87ABF2CBFDAA27EA8C73BD284DA9649B9A2AB27B4E6C8F6AEAB1BD1C88` |
| corpus | `benchmarks/plain_sample_text.txt` | `A391E53B317797193E7A74046B6F23D9BF5722895342D420CFD4912534093766` |
| corpus | `benchmarks/corpora/json_test.json` | `0F6AD0ACCA7471D993A6D24B0E627A041C1FC874AC48070DF26A9BB817CCAAFE` |
| corpus | `benchmarks/corpora/logs_test.txt` | `0FFE53F3673367613F2BCA123305FE4CC3FA7C79C123DC8952A044A9642557C7` |
| corpus | `benchmarks/corpora/csrc_test.c` | `902C48806C805A77DB88035C642AD710A6BF7AA3B548217FF121FC6E813B6C0E` |
| corpus | `benchmarks/corpora/text_1024.txt` | `E91B2D8900A1E2376A23AB70201E69532623F3A06880CDF1FF926F33EEB9E3DF` |
| corpus | `benchmarks/corpora/text_524288.txt` | `5BFE6457877AA94C9407F454D15F5C5E351796E147403A161C6F1839A8142B50` |
| table | `modules/loxc_demo.loxctab` | `91BDEE3792F67CA6EB0104A29562384C3E724A499F4019C27577759717A97BAB` |
| table | `modules/loxc_json.loxctab` | `6D19A0B2BE3E77FC9FCBADA3F4D580083E0BE5576415E7F7D1684D9C16E9EB68` |
| table | `modules/loxc_logs.loxctab` | `CE203E2FDBB1D48BF9DA8CDA190AE58967077E375AEE80BE774D869180A831BD` |
| table | `modules/loxc_csrc.loxctab` | `E29A120117C87C157F927369D945DC05C055FF480015C04F17020D287F45C16D` |

## What we measure

For every (file, compressor) pair we report:

| metric | meaning |
|---|---|
| `raw_bytes`            | original input size |
| `encoded_bytes`        | output size after compression |
| `ratio_pct`            | `100 * encoded / raw` (lower is better) |
| `encode_median_ms`     | median wall time over N iterations after warmup |
| `decode_median_ms`     | median wall time over N iterations after warmup |
| `encode_mbps_median`   | `(raw / 1 MiB) / (encode_median_ms / 1000)` |
| `decode_mbps_median`   | `(raw / 1 MiB) / (decode_median_ms / 1000)` |
| `enc_p95_ms` / `enc_p99_ms` / `enc_stddev_ms` | (loxc only) tail latency |
| `dec_p95_ms` / `dec_p99_ms` / `dec_stddev_ms` | (loxc only) tail latency |
| `round_trip_ok`        | (loxc only) `1` iff `decode(encode(x)) == x` for every iteration |

For `loxc` we additionally measure both packaging modes:

- **External mode** (`loxc-ext`): the `.loxc` payload requires the matching
  `.loxctab` table at decode time. The table size is amortized across many
  payloads.
- **Embedded mode** (`loxc-emb`): the `.loxc` file is self-contained — header
  + full table + payload. The table blob is constant per module, so its
  relative cost shrinks as the payload grows.

## How to run

```
make bench-full              # iterations defaults to 25
make bench-full ITER=50      # higher iteration count = tighter percentiles
```

What that does, in order:

1. Builds `libloxc.a`, all CLI tools, and the new `tools/loxc_bench2`.
2. Generates training and held-out test corpora into `trainings/extra` and
   `benchmarks/corpora` (idempotent; existing files are kept).
3. Trains four domain-specific modules: `demo` (bundled sample text), `json`, `logs`,
   `csrc`.
4. Runs `loxc_bench2` per module against its matching suite.
5. Runs `tools/bench_baselines.sh` over the same files with `gzip`, `zstd`,
   `xz`, `lz4`, `brotli`, `bzip2`, and `raw` (no-op pass-through).
6. Aggregates everything into `bench_out/BENCHMARKS_FULL.md`,
   `bench_out/DOMAIN_WINS.md`, and `bench_out/merged.csv`.

Outputs:

```
bench_out/
├── BENCHMARKS_FULL.md      # the report you read
├── DOMAIN_WINS.md          # per-domain module summary
├── merged.csv              # long-form CSV: every (file, tool, level) row
├── loxc_demo.{csv,json,txt}
├── loxc_json.{csv,json,txt}
├── loxc_logs.{csv,json,txt}
├── loxc_csrc.{csv,json,txt}
├── baselines.csv
├── suite_demo.list
├── suite_json.list
├── suite_logs.list
├── suite_csrc.list
└── suite_union.list
```

## Tools

### `tools/loxc_bench2` (new)

Replaces the original `tools/loxc_bench` for everything but its simplest
demo mode. New features:

- Loads any `.loxctab` at runtime via `--table` (no recompile per module).
- `--iterations N --warmup K`: warmup pass plus N timed iterations.
- Computes min / median / p95 / p99 / max / stddev for both encode and decode.
- Reports throughput in MB/s for both directions.
- Measures **external** and **embedded** mode encoded size for every file.
- Verifies round-trip integrity (`memcmp`) on every iteration.
- Emits human-readable, CSV, and JSON outputs.

```
tools/loxc_bench2 \
  --table modules/loxc_demo.loxctab \
  --suite benchmarks/suite.list \
  --iterations 25 --warmup 3 \
  --csv  bench_out/loxc_demo.csv \
  --json bench_out/loxc_demo.json
```

### `tools/bench_baselines.sh`

Runs `gzip`, `zstd`, `xz`, `lz4`, `brotli`, `bzip2`, and a `raw` no-op pass on
every file in the suite at multiple compression levels, producing a CSV with
the same shape as the loxc one so the two can be merged.

```
tools/bench_baselines.sh benchmarks/suite.list bench_out/baselines.csv 25
```

### `tools/bench_aggregate.py`

Reads the two CSVs, merges them, and emits the per-file comparison tables,
aggregate scoreboards, and run metadata (CPU, compiler, OS, date).

### `tools/bench_run.sh`

The orchestrator. The thing `make bench-full` calls. Idempotent: re-runs
cheaply if corpora and modules are already up to date.

## Corpora

Two kinds of files live in the repository:

- `trainings/` — corpora used to train modules. **Not** used to evaluate
  compression ratio (that would be cheating).
- `benchmarks/corpora/` — held-out test files. Synthetic but realistic;
  generated deterministically with a fixed RNG seed by
  `tools/bench_make_corpora.py` so results are reproducible.

The test corpora cover:

| file | what it is | role |
|---|---|---|
| `text_1024.txt` … `text_524288.txt` | Sample-text slices of `demo_corpus.txt` | latency-vs-size curve |
| `json_test.json`     | 300 JSON user records, ~88 KiB                | JSON domain held-out |
| `logs_test.txt`      | structured log lines, ~89 KiB                 | logs domain held-out |
| `csrc_test.c`        | synthetic C functions, ~16 KiB                | source-code domain held-out |
| `repetitive_16k.txt` | one sentence repeated to 16 KiB               | compressor best-case sanity |
| `plain_sample_text.txt` | 30 KiB slice of `demo_corpus.txt`          | quick demo |

## Caveats — read this before quoting numbers

- **loxc requires a trained table that covers the input's byte distribution.**
  If the table doesn't, the encoder returns `LOXC_ERR_SYMBOL_NOT_FOUND` and
  the file is reported as `UNSUPPORTED`. This is by design.
- **No LZ77 backreferences.** Without them, `loxc` is structurally unable to
  match `gzip`/`zstd`/`brotli` on ratio. Its win is decoder throughput.
- **Baselines are measured via CLI.** Fork/exec adds a fixed ~1–3 ms cost on
  Linux. On files smaller than ~64 KiB this hides the actual codec speed and
  the throughput numbers look artificially low for everyone. The
  apples-to-apples comparison points are the larger files in the suite.
  loxc itself is measured in-process and is **not** subject to this overhead.
- **Embedded mode is heavier than external mode on small payloads.** The
  table blob is constant (~2 KiB for the bundled modules), so on a 1 KiB
  payload the embedded `.loxc` file is larger than the input. Use embedded
  mode only when the payload is large or when self-containedness matters
  more than size.
- **Single-threaded.** All measurements are single-threaded.
- **Page-cache warm.** Inputs are read once into RAM before timing starts.

## Headline numbers (representative — your machine will differ)

Measured on x86_64 Linux, single thread, `cc -O2`:

| file | tool | ratio | dec MB/s |
|---|---|---|---|
| `trainings/demo_corpus.txt` (720 KiB) | `zstd:3`        | 34.9% | ~182 |
| `trainings/demo_corpus.txt` (720 KiB) | `gzip:6`        | 36.0% | ~91  |
| `trainings/demo_corpus.txt` (720 KiB) | `loxc-ext(demo)`| 60.8% | ~55  |
| `trainings/demo_corpus.txt` (720 KiB) | `lz4:1`         | 60.4% | ~207 |
| `json_test.json` (88 KiB)             | `loxc-ext(json)`| 62.9% | ~103 |
| `csrc_test.c` (16 KiB)                | `loxc-ext(csrc)`| 45.1% | ~120 |

Read: on a payload covered by its trained table, `loxc` lands in
roughly-`lz4`-level ratio territory with decode throughput in the 50–120 MB/s
range, against a `.loxctab` file of ~1.4–2 KiB. The decode path is one
table-lookup-per-symbol; no entropy coding, no backreference scan.

For absolute archival ratio, use `zstd -19` or `brotli -11`. For random-access
string decode in databases, `FSST` remains the closest production-grade
competitor. The `loxc` story is "self-contained, retrainable codec with
predictable bitstream-walk decode" — see the comparison table in `README.md`.

## Files added/changed by this benchmark package

```
tools/loxc_bench2.c          # new C bench
tools/bench_baselines.sh     # baselines comparator
tools/bench_aggregate.py     # merges results, writes report
tools/bench_run.sh           # orchestrator (make bench-full)
tools/bench_make_corpora.py  # deterministic corpora generator
benchmarks/suite.list        # default suite for loxc_bench2
benchmarks/plain_sample_text.txt    # 30 KiB slice referenced by docs
benchmarks/corpora/...       # held-out test files
trainings/extra/...          # extra training corpora
Makefile                     # adds `bench-full` target
```
