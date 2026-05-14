# Benchmarks

Build:
- `make tools/loxc_bench`

Run (default suite):
- `./tools/loxc_bench --module demo`

Default suite files:
- `benchmarks/plain_english.txt`
- `benchmarks/tiny.txt`
- `benchmarks/small.txt`
- `benchmarks/medium.txt`
- `benchmarks/source.c`
- `benchmarks/data.json`
- `trainings/demo_corpus.txt`

## Results

Measured on 2026-05-14 with the generated `demo` module.

```text
File                           | Size      | Encoded   | Ratio   | Enc Time | Dec Time | OK
-------------------------------+-----------+-----------+---------+----------+----------+----
trainings/demo_corpus.txt      |    738046 |    448523 |   60.8% |  109.67ms |   12.96ms | OK
benchmarks/plain_english.txt   |     30000 |     18646 |   62.2% |    4.62ms |    0.51ms | OK
benchmarks/tiny.txt            |        92 |         - |       - |        - |        - | UNSUPPORTED
benchmarks/small.txt           |     12500 |         - |       - |        - |        - | UNSUPPORTED
benchmarks/medium.txt          |    112500 |         - |       - |        - |        - | UNSUPPORTED
benchmarks/source.c            |      2718 |         - |       - |        - |        - | UNSUPPORTED
benchmarks/data.json           |      5385 |         - |       - |        - |        - | UNSUPPORTED
```

## Unsupported Files

The `demo` module is trained on the English training corpus. It supports the
symbols and dictionary entries observed in that corpus, so files with different
byte distributions can be rejected as unsupported.

The current unsupported benchmark files include JSON, C source, synthetic text
with characters outside the demo corpus, and Windows line endings. These are
outside the demo dictionary and symbol table. They should be handled by training
separate modules for those data families.

## Notes

- `demo_corpus.txt`: 738 KB input to 448 KB encoded, 60.8% ratio.
- `plain_english.txt`: 30 KB corpus slice to 18 KB encoded, 62.2% ratio.
- Decode is roughly 8-10x faster than encode on the passing benchmark files.
  Encoding is more expensive because it performs longest-match dictionary
  search before writing symbols; decoding is mostly table lookup and bitstream
  traversal.
