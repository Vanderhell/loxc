# loxc – Codex Agent Roadmap

Postupný plán implementácie. Každá fáza má vlastný prompt pre Codex agenta.
Neprechádzaj na ďalšiu fázu kým nie sú testy zelenej.

---

## FÁZA 1 – Základná štruktúra projektu

### Cieľ
Pripraviť kostru projektu, adresárovú štruktúru, Makefile a prázdne hlavičkové súbory.

### Prompt pre Codex
```
Create a C99 library project called "loxc" with the following structure:

loxc/
  include/
    loxc_base.h        # main public header, includes everything
    loxc_types.h       # shared types, error codes, constants
    loxc_lower.h       # lowercase letters a-z module
    loxc_upper.h       # uppercase letters A-Z module
    loxc_digits.h      # digits 0-9 module
    loxc_punct.h       # basic punctuation module
    loxc_whitespace.h  # space, tab, newline module
    loxc_plain.h       # composite: lower+upper+digits+punct+whitespace
  src/
    loxc_base.c        # core encode/decode engine
    loxc_matrix.c      # matrix navigation logic
    loxc_dict.c        # LOCAL_DICT analysis and encoding
    loxc_stream.c      # bitstream read/write helpers
  tools/
    loxc_train.c       # standalone training tool (generates .h modules)
    loxc_cli.c         # simple CLI for testing
    loxc_bench.c       # benchmark tool
  tests/
    test_basic.c       # basic encode/decode roundtrip tests
    test_matrix.c      # matrix navigation tests
    test_dict.c        # LOCAL_DICT tests
  Makefile
  README.md

Requirements:
- C99 standard, no external dependencies
- Every module must be a separate compilation unit (linker friendly)
- Use include guards in all headers
- Define error codes in loxc_types.h: LOXC_OK, LOXC_ERR_NULL, LOXC_ERR_TRUNCATED, LOXC_ERR_OVERFLOW, LOXC_ERR_INVALID_MAGIC
- Define magic constant: LOXC_MAGIC = { 'L', 'X', 'C', 0x00 }
- Makefile targets: all, test, bench, clean
```

### Hotovo keď
- [ ] `make all` prebehne bez chýb
- [ ] `make test` spustí testy (môžu failovať, len musia bežať)
- [ ] Všetky hlavičkové súbory existujú s include guards

---

## FÁZA 2 – Bitový stream

### Cieľ
Implementovať čítanie a zápis bitov. Základ pre všetko ostatné.

### Prompt pre Codex
```
Implement a bitstream reader and writer for the loxc library in C99.

File: src/loxc_stream.c and include/loxc_stream.h

Requirements:

Writer:
  typedef struct {
    uint8_t *buf;       // output buffer
    size_t   cap;       // buffer capacity in bytes
    size_t   byte_pos;  // current byte position
    uint8_t  bit_pos;   // current bit position (0-7)
  } loxc_writer_t;

  int loxc_writer_init(loxc_writer_t *w, uint8_t *buf, size_t cap);
  int loxc_write_bit(loxc_writer_t *w, uint8_t bit);
  int loxc_write_bits(loxc_writer_t *w, uint32_t bits, uint8_t n); // write n bits from bits
  int loxc_writer_flush(loxc_writer_t *w);  // pad last byte with zeros
  size_t loxc_writer_size(const loxc_writer_t *w); // total bytes written

Reader:
  typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         byte_pos;
    uint8_t        bit_pos;
  } loxc_reader_t;

  int loxc_reader_init(loxc_reader_t *r, const uint8_t *buf, size_t len);
  int loxc_read_bit(loxc_reader_t *r, uint8_t *out);
  int loxc_read_bits(loxc_reader_t *r, uint8_t n, uint32_t *out); // read n bits
  int loxc_reader_eof(const loxc_reader_t *r);

Write tests in tests/test_stream.c:
- Write 8 bits, read them back, verify match
- Write mixed bit lengths (3 bits, 5 bits, 7 bits), read back
- Test boundary: write exactly buffer capacity, verify no overflow
- Test EOF detection
- All tests use assert(), print PASS/FAIL per test
```

### Hotovo keď
- [ ] `make test` → `test_stream: PASS`
- [ ] Zápis a čítanie sú symetrické (write→read = original)
- [ ] Buffer overflow je zachytený a vrátený ako error

---

## FÁZA 3 – Maticová navigácia

### Cieľ
Implementovať jadro systému – hierarchické matice a navigáciu cez vektory.

### Prompt pre Codex
```
Implement the core matrix navigation system for loxc in C99.

File: src/loxc_matrix.c and include/loxc_matrix.h

Concept:
A matrix is a multi-dimensional lookup table. Navigation works by reading
groups of bits and indexing into the matrix at each level. Each matrix
cell can either contain a decoded value (leaf) or point to a sub-matrix.

Data structures:

  // A single matrix level
  typedef struct {
    uint8_t  bits;      // how many bits this level consumes (e.g. 3 for 8 cells)
    uint8_t  dims;      // 1 = 1D array, 2 = 2D, 3 = 3D
    uint8_t  size[3];   // size of each dimension (must be powers of 2)
  } loxc_matrix_level_t;

  // A decoded value at a matrix leaf
  typedef struct {
    uint32_t codepoint; // Unicode codepoint or special value
    uint8_t  type;      // LOXC_TYPE_CHAR, LOXC_TYPE_BIGRAM, LOXC_TYPE_REF, LOXC_TYPE_ESCAPE
  } loxc_matrix_value_t;

  // Full matrix hierarchy for one module
  typedef struct {
    uint8_t              levels;          // number of levels
    loxc_matrix_level_t  level[8];        // max 8 levels
    loxc_matrix_value_t *data;            // flat array, row-major
    size_t               data_len;
  } loxc_matrix_t;

Functions:
  // Navigate matrix using bits from reader, return decoded value
  int loxc_matrix_decode(const loxc_matrix_t *m,
                         loxc_reader_t *r,
                         loxc_matrix_value_t *out);

  // Find encoding path for a codepoint (for encoder)
  int loxc_matrix_encode(const loxc_matrix_t *m,
                         uint32_t codepoint,
                         loxc_writer_t *w);

For now, hardcode a simple test matrix in tests/test_matrix.c:
- 2-level matrix: level1 = 2 bits (4 cells), level2 = 2 bits (4 cells)
- Assign 16 ASCII chars to leaves
- Encode each char, decode it back, verify roundtrip
- Test that unknown codepoint returns LOXC_ERR_INVALID_MAGIC
```

### Hotovo keď
- [ ] `make test` → `test_matrix: PASS`
- [ ] Encode→decode roundtrip pre všetky znaky v testovacej matici
- [ ] Neznámy znak vráti správny error kód

---

## FÁZA 4 – Hlavička súboru

### Cieľ
Implementovať zápis a čítanie loxc hlavičky.

### Prompt pre Codex
```
Implement loxc file header encoding and decoding in C99.

File: src/loxc_base.c (header section) and include/loxc_base.h

Header format (total minimum 8 bytes):
  Offset  Size  Field
  0       4     MAGIC: { 'L', 'X', 'C', module_id }
  4       1     VERSION: module version (0x01)
  5       1     FLAGS: bit flags
                  bit 7 = LOXC_FLAG_CRC   (CRC32 present after header)
                  bit 6 = LOXC_FLAG_DICT  (LOCAL_DICT present)
  6       2     DATA_LEN: length of encoded data in bytes (uint16_t, big-endian)
  [8]     4     CRC32: optional, present if LOXC_FLAG_CRC set
  [...]   var   LOCAL_DICT: optional, present if LOXC_FLAG_DICT set
  [...]   var   DATA: encoded bitstream

Data structures:
  typedef struct {
    uint8_t  module_id;
    uint8_t  version;
    uint8_t  flags;
    uint16_t data_len;
    uint32_t crc32;      // only valid if LOXC_FLAG_CRC set
  } loxc_header_t;

Functions:
  int loxc_header_write(loxc_writer_t *w, const loxc_header_t *h);
  int loxc_header_read(loxc_reader_t *r, loxc_header_t *h);
  int loxc_header_validate(const loxc_header_t *h);

CRC32:
  Implement standard CRC32 (polynomial 0xEDB88320).
  CRC covers DATA bytes only (not the header itself).
  Function: uint32_t loxc_crc32(const uint8_t *data, size_t len);

Tests in tests/test_header.c:
- Write header, read it back, verify all fields match
- Test with and without CRC flag
- Test with invalid magic, expect LOXC_ERR_INVALID_MAGIC
- Test truncated header, expect LOXC_ERR_TRUNCATED
```

### Hotovo keď
- [ ] `make test` → `test_header: PASS`
- [ ] Header write→read roundtrip
- [ ] CRC32 overenie funguje

---

## FÁZA 5 – Prvý modul: loxc_plain

### Cieľ
Implementovať prvý reálny modul – plain text (a-z, A-Z, 0-9, interpunkcia).
Toto je prvý krát keď sa skutočne niečo zakóduje a dekóduje end-to-end.

### Prompt pre Codex
```
Implement the first real loxc module: loxc_plain (plain ASCII text).

File: include/loxc_plain.h

This module covers: a-z, A-Z, 0-9, space, basic punctuation (.,!?;:'"-)
and common bigrams (th, he, in, er, an, re, on, en, at, es).

Create a static const matrix for this module using frequency analysis
of English text. Higher frequency = shorter path in matrix.

Approximate character frequencies for English:
  space=18%, e=10%, t=7%, a=6.5%, o=6%, i=5.5%, n=5.5%, s=5%
  h=4.5%, r=4.5%, d=3.5%, l=3.5%, c=3%, u=2.5%, m=2.5%
  bigram "th"=3.2%, "he"=2.5%, "in"=2.0%, "er"=1.8%

Matrix design:
  Level 1 (3 bits, 8 cells):
    000 = space           (most frequent)
    001 = e
    010 = t
    011 = a/o/i group → Level 2
    100 = common letters → Level 2
    101 = bigrams → Level 2
    110 = uppercase + digits → Level 2
    111 = punctuation + escape → Level 2

  Level 2 (3 bits, 8 cells per branch): assign remaining chars

Define as static const arrays:
  static const loxc_matrix_t LOXC_PLAIN_MATRIX = { ... };
  #define LOXC_MODULE_PLAIN  0x01

Implement in loxc_base.c:
  int loxc_encode(const char *input, size_t in_len,
                  uint8_t module_id,
                  uint8_t *output, size_t out_cap,
                  size_t *out_len);

  int loxc_decode(const uint8_t *input, size_t in_len,
                  char *output, size_t out_cap,
                  size_t *out_len);

Tests in tests/test_basic.c:
  - Encode "hello world", decode, verify match
  - Encode "The quick brown fox", decode, verify
  - Verify encoded size < original size
  - Test empty string
  - Test single character
  - Test string with all supported chars
```

### Hotovo keď
- [ ] `make test` → `test_basic: PASS`
- [ ] Encode→decode roundtrip pre všetky testovacie stringy
- [ ] Zakódovaný string je menší než originál

---

## FÁZA 6 – LOCAL_DICT

### Cieľ
Implementovať analýzu a kódovanie lokálneho slovníka.

### Prompt pre Codex
```
Implement LOCAL_DICT analysis and encoding for loxc in C99.

File: src/loxc_dict.c and include/loxc_dict.h

Concept:
Before encoding, analyze the input to find repeating words/tokens.
If a word appears enough times to justify a dictionary entry, replace
all occurrences with a short reference code. The dictionary is encoded
using the same loxc module and stored in the file header.

The formula to decide if a word goes into the dict:
  bits_word    = strlen(word) * avg_bits_per_char
  bits_in_dict = bits_word + DICT_ENTRY_OVERHEAD (= 8 bits)
  bits_ref     = 8  (1 byte reference, for dict size <= 256)
               = 16 (2 byte reference, for dict size > 256)

  GAIN = (count * bits_word) - (count * bits_ref + bits_in_dict)
  If GAIN > 0 → add to dict

Data structures:
  typedef struct {
    char    *word;
    size_t   word_len;
    uint32_t count;
    int32_t  gain;       // calculated gain in bits
    uint16_t ref_id;     // assigned reference id
  } loxc_dict_entry_t;

  typedef struct {
    loxc_dict_entry_t *entries;
    size_t             count;
    uint8_t            ref_bytes;  // 1 or 2
  } loxc_dict_t;

Functions:
  // Analyze input, build candidate dict
  int loxc_dict_analyze(const char *input, size_t len,
                        loxc_dict_t *dict);

  // Encode dict into header section
  int loxc_dict_encode(const loxc_dict_t *dict,
                       uint8_t module_id,
                       loxc_writer_t *w);

  // Decode dict from header section
  int loxc_dict_decode(loxc_reader_t *r,
                       uint8_t module_id,
                       loxc_dict_t *dict);

  void loxc_dict_free(loxc_dict_t *dict);

Tests in tests/test_dict.c:
  - Input with "getUserData" repeated 10 times → must be in dict
  - Input with "if" repeated 50 times → must NOT be in dict (too short)
  - Verify gain formula is correct for known inputs
  - Encode dict, decode it, verify all entries match
  - Integration: encode string with dict, decode, verify roundtrip
```

### Hotovo keď
- [ ] `make test` → `test_dict: PASS`
- [ ] Krátke slová nie sú v dict aj keď sa veľakrát opakujú
- [ ] Dlhé slová sú v dict ak sa oplatí
- [ ] Dict encode→decode roundtrip

---

## FÁZA 7 – Streaming API

### Cieľ
Implementovať streaming pre veľké vstupy.

### Prompt pre Codex
```
Implement streaming API for loxc in C99 for large inputs that don't
fit in memory.

File: src/loxc_stream_api.c (separate from loxc_stream.c bitstream)
      include/loxc_stream_api.h

Two-phase approach:
  Phase 1: feed chunks for dict analysis only (no output yet)
  Phase 2: feed chunks again for actual encoding (with output)

  typedef struct {
    uint8_t          module_id;
    loxc_dict_t      dict;
    loxc_writer_t    writer;
    enum { LOXC_PHASE_ANALYZE, LOXC_PHASE_ENCODE } phase;
    uint8_t         *header_buf;
    size_t           header_len;
  } loxc_stream_ctx_t;

Functions:
  int loxc_stream_open(loxc_stream_ctx_t *ctx, uint8_t module_id);

  // Phase 1: analyze chunks for dict
  int loxc_stream_feed(loxc_stream_ctx_t *ctx,
                       const char *chunk, size_t len);

  // Finalize dict after all chunks fed in phase 1
  int loxc_stream_finalize_dict(loxc_stream_ctx_t *ctx);

  // Phase 2: encode chunks (call with same chunks as phase 1)
  int loxc_stream_encode(loxc_stream_ctx_t *ctx,
                         const char *chunk, size_t len,
                         uint8_t *out, size_t out_cap,
                         size_t *out_len);

  int loxc_stream_close(loxc_stream_ctx_t *ctx);

Tests in tests/test_streaming.c:
  - Encode 1MB string in 4KB chunks, decode result, verify match
  - Verify dict is same as non-streaming encode of same input
  - Test single-byte chunks (stress test)
  - Test empty chunks (should be no-op)
```

### Hotovo keď
- [ ] `make test` → `test_streaming: PASS`
- [ ] Streaming výsledok = non-streaming výsledok pre rovnaký vstup
- [ ] Pamäťová stopa je konštantná bez ohľadu na veľkosť vstupu

---

## FÁZA 8 – Trénovací nástroj

### Cieľ
Implementovať `loxc_train` – nástroj ktorý z vzoriek vygeneruje `.h` modul.

### Prompt pre Codex
```
Implement the loxc_train standalone tool in C99.

File: tools/loxc_train.c

Purpose: analyze sample files, build optimized matrix, output a .h module file.

CLI usage:
  loxc_train --input samples/ --output loxc_mymodule.h
             --module-id 0x05 --max-ngram 4 --penalty 1.5

Algorithm:
  1. Read all files from --input directory
  2. Count character frequencies (single chars)
  3. Count n-gram frequencies up to --max-ngram
  4. For each n-gram, calculate gain vs encoding chars individually:
       gain = count * (sum of individual char bits) - count * ngram_bits
       where ngram_bits is estimated from its frequency rank
  5. Keep n-grams where gain > 0
  6. Sort all symbols (chars + ngrams) by frequency descending
  7. Build matrix levels:
       - Most frequent symbols get shortest paths
       - Apply penalty: score = frequency / (depth ^ penalty)
       - Group symbols into matrix levels (powers of 2 per level)
  8. Output .h file with static const loxc_matrix_t
9. Diagonálne umiestnenie:
   - Po zoradení symbolov podľa frekvencie
   - Top-N symbolov priraď diagonálnym pozíciám (kde N = veľkosť matice)
   - Zvyšok rozsyp do off-diagonal buniek
   - V hlavičke modulu nastav flag LOXC_FLAG_DIAG_OPT
A v loxc_matrix_decode/encode pridaj vetvu pre LOXC_FLAG_DIAG_OPT:
cif (flag_diag) {
    uint8_t marker;
    loxc_read_bit(r, &marker);
    if (marker == 0) {
        // Diagonálny prípad: čítaj len 3 bity, X=Y=tieto bity
        uint32_t coord;
        loxc_read_bits(r, 3, &coord);
        return matrix[coord][coord];
    } else {
        // Normálny prípad: čítaj 6 bitov
        ...
    }
}

Output .h format:
  // Auto-generated by loxc_train. DO NOT EDIT.
  // Trained on: N bytes, M unique symbols
  // Module ID: 0x05, Version: 0x01
  #pragma once
  #include "loxc_base.h"
  #define LOXC_MODULE_MYMODULE 0x05
  static const loxc_matrix_value_t _loxc_mymodule_data[] = { ... };
  static const loxc_matrix_t LOXC_MYMODULE_MATRIX = { ... };

Test:
  - Train on 10 English text files
  - Encode test string with generated module
  - Verify encoded size is smaller than loxc_plain result
  - Verify decode roundtrip
```

### Hotovo keď
- [ ] `loxc_train` generuje validný `.h` súbor
- [ ] Vygenerovaný modul sa skompiluje bez chýb
- [ ] Encode s vlastným modulom → menší výsledok než `loxc_plain`

---

## FÁZA 9 – Benchmark

### Cieľ
Zmerať reálny výkon a porovnať s konkurenciou.

### Prompt pre Codex
```
Implement loxc_bench benchmark tool in C99.

File: tools/loxc_bench.c

Measures and compares:
  - loxc encode speed (MB/s)
  - loxc decode speed (MB/s)
  - loxc compression ratio (%)
  - ns per character for encode and decode
  - L1/L2 cache behavior estimate (via repeated runs)

Compare against (if available via system):
  - zlib (gzip level 6)
  - zstd (level 3)
  - memcpy (baseline)

CLI usage:
  loxc_bench --input testfile.txt --runs 1000 --warmup 100

Output format:
  ┌─────────────────────────────────────────────────────┐
  │ loxc_bench v0.1                                     │
  │ Input: testfile.txt (4,218 bytes)                   │
  ├──────────────┬──────────┬──────────┬────────────────┤
  │ Method       │ Size     │ Ratio    │ ns/char        │
  ├──────────────┼──────────┼──────────┼────────────────┤
  │ loxc encode  │ 1,842 B  │  56.3%   │  2.1 ns        │
  │ loxc decode  │ 4,218 B  │ 100.0%   │  1.8 ns        │
  │ zstd enc     │ 1,654 B  │  39.2%   │  8.4 ns        │
  │ zstd dec     │ 4,218 B  │ 100.0%   │  3.2 ns        │
  │ memcpy       │    —     │    —     │  0.3 ns        │
  └──────────────┴──────────┴──────────┴────────────────┘

Use clock_gettime(CLOCK_MONOTONIC) for timing.
Run --warmup iterations before measuring to warm cache.
Report min/avg/max across --runs iterations.


```

### Hotovo keď
- [ ] `loxc_bench` beží bez chýb
- [ ] Výsledky sú konzistentné medzi behmi (< 10% rozptyl)
- [ ] loxc decode je rýchlejší než zstd decode (cieľ)

---

## FÁZA 10 – CLI nástroj a dokumentácia

### Cieľ
Dokončiť `loxc_cli`, README a príklady.

### Prompt pre Codex
```
Implement loxc_cli tool and finalize documentation.

File: tools/loxc_cli.c

CLI usage:
  loxc encode --module plain input.txt output.lxc
  loxc decode input.lxc output.txt
  loxc info   input.lxc          # show header info, dict entries, stats
  loxc verify input.lxc          # verify CRC32

loxc info output example:
  File:      input.lxc
  Magic:     LXC
  Module:    plain (0x01)
  Version:   1
  Flags:     CRC=yes DICT=yes
  Dict:      12 entries
  Data:      1,842 bytes
  CRC32:     0xA4B2C3D1 (valid)

Also write README.md covering:
  1. What is loxc (one paragraph)
  2. Quick start (encode/decode in 5 lines of C)
  3. Available modules and when to use each
  4. How to train a custom module
  5. API reference (all public functions)
  6. Building: make all / make test / make bench
  7. License: MIT
```

### Hotovo keď
- [ ] `loxc encode` + `loxc decode` funguje end-to-end
- [ ] `loxc info` zobrazí správne informácie
- [ ] README je kompletné
- [ ] `make test` → všetky testy zelené

---

## Celkový prehľad

| Fáza | Čo | Hotovo |
|------|-----|--------|
| 1 | Štruktúra projektu | ☐ |
| 2 | Bitový stream | ☐ |
| 3 | Maticová navigácia | ☐ |
| 4 | Hlavička súboru | ☐ |
| 5 | Prvý modul loxc_plain | ☐ |
| 6 | LOCAL_DICT | ☐ |
| 7 | Streaming API | ☐ |
| 8 | Trénovací nástroj | ☐ |
| 9 | Benchmark | ☐ |
| 10 | CLI + dokumentácia | ☐ |

---

## Dôležité pravidlá pre Codex agenta

```
1. Vždy píš testy pred implementáciou (TDD)
2. Každá funkcia musí mať jasný error kód ako návratovú hodnotu
3. Žiadne dynamické alokácie v hot path (encode/decode)
4. Všetky buffery musia mať explicitnú kapacitu (žiadne strcpy bez bounds)
5. Každý commit musí prejsť make test
6. Nepoužívaj globálny stav – všetko cez context struct
7. Komentuj prečo, nie čo
```
