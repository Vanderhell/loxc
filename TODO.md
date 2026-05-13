# OTODO - loxc progress log

## 2026-05-10

### Hotove (FAZA 1 - Zakladna struktura projektu)
- PridanĂˇ kostra projektu: `include/`, `src/`, `tools/`, `tests/`
- PridanĂ˝ `Makefile` s targetmi: `all`, `test`, `bench`, `clean`
- PridanĂ˝ `README.md` (zĂˇklad)
- VerejnĂ© hlaviÄŤky s include guards:
  - `include/loxc_base.h`
  - `include/loxc_types.h` (error kĂłdy + `LOXC_MAGIC`)
  - `include/loxc_lower.h`, `include/loxc_upper.h`, `include/loxc_digits.h`, `include/loxc_punct.h`, `include/loxc_whitespace.h`, `include/loxc_plain.h` (placeholder)
- PridanĂ© stub implementĂˇcie:
  - `src/loxc_base.c` (`loxc_magic_bytes()`)
  - `src/loxc_stream.c`, `src/loxc_matrix.c`, `src/loxc_dict.c` (placeholder stub funkcie)
- PridanĂ© stub nĂˇstroje:
  - `tools/loxc_train.c`, `tools/loxc_cli.c`, `tools/loxc_bench.c` (placeholder main)
- PridanĂ© testy:
  - `tests/test_basic.c` (overuje `LOXC_MAGIC` a error kĂłdy)
  - `tests/test_matrix.c`, `tests/test_dict.c` (zatiaÄľ len `SKIP`)

### Blokery (nedokoncene kriteria FAZA 1)
- NedĂˇ sa overiĹĄ `make all` / `make test`: v prostredĂ­ chĂ˝ba `make` aj C kompilĂˇtor (`gcc`/`clang`/`cl`).
- WSL nemĂˇ nainĹˇtalovanĂş distro.

### Dalsi krok
- NainĹˇtalovaĹĄ toolchain (napr. MSYS2/MinGW alebo LLVM + make), potom spustiĹĄ:
  - `make all`
  - `make test`
- Az ked bude build+test OK, pokracuj na **FAZA 2 - Bitovy stream** (TDD + implementacia `include/loxc_stream.h` + `src/loxc_stream.c` + `tests/test_stream.c`).

## 2026-05-10 (pokrok)

### Hotove (FAZA 2 - Bitovy stream)
- Pridane API `include/loxc_stream.h` (writer/reader)
- Implementovane `src/loxc_stream.c` (bit writer/reader)
- Pridane testy `tests/test_stream.c`
- Aktualizovany `Makefile` (pridal sa `tests/test_stream` do `make test`)
- Overene vo WSL: `make test` -> `test_stream: PASS` (2026-05-10)

### Hotove (FAZA 3 - Maticova navigacia)
- Implementovane `include/loxc_matrix.h` (typy + API)
- Implementovane `src/loxc_matrix.c` (`loxc_matrix_encode`, `loxc_matrix_decode`)
- Aktualizovany `tests/test_matrix.c` (roundtrip + unknown codepoint)
- Overene vo WSL: `make test` -> `test_matrix: PASS` (2026-05-10)

### Hotove (FAZA 4 - Hlavicka suboru)
- Implementovane v `include/loxc_base.h`: `loxc_header_t`, `LOXC_FLAG_CRC`, `LOXC_FLAG_DICT`, API `loxc_header_*`, `loxc_crc32`
- Implementovane v `src/loxc_base.c`: header write/read/validate + CRC32
- Pridane testy `tests/test_header.c` + zapojene do `Makefile`
- Overene vo WSL: `make test` -> `test_header: PASS` (2026-05-10)

### Hotove (FAZA 5 - Prvy modul: loxc_plain)
- Implementovane `include/loxc_plain.h` (API + module id/version)
- Implementovane `src/loxc_plain.c` (staticka "plain" matica + encode/decode s bigramami)
- Pridane testy `tests/test_plain.c` + zapojene do `Makefile`
- Overene vo WSL: `make test` -> `test_plain: PASS` (2026-05-10)

### Rozpracovane (FAZA 6 - LOCAL_DICT)
- Pridane `include/loxc_dict.h` (typy + API)
- Implementovane `src/loxc_dict.c`:
  - `loxc_dict_analyze()` (tokenizacia `[A-Za-z0-9_]`, count, gain formula, ref_id len pre gain>0)
  - `loxc_dict_encode()` / `loxc_dict_decode()` (binarny format pre header sekciu)
  - `loxc_dict_free()`
- Aktualizovane `tests/test_dict.c` z `SKIP` na realne testy
- Fix po prvom behu testov:
  - slova kratsie ako 3 znaky sa nedavaju do dict (`if` uz nema prejst)
  - odstraneny warning v decode (`wlen > 255` bol zbytocny)
### Hotove (FAZA 6 - LOCAL_DICT)
- Overene vo WSL: `make test` -> `test_dict: PASS` (2026-05-10)

## Co este treba spravit (roadmap)

- Dokoncit FAZA 6: dostat `test_dict: PASS` + idealne pridat integracny test "encode string s dict -> decode -> roundtrip"
- FAZA 7: Streaming API (`include/loxc_stream_api.h`, `src/loxc_stream_api.c`, `tests/test_streaming.c`)
- FAZA 8: Trenovaci nastroj (`tools/loxc_train.c`) generujuci .h moduly
- FAZA 9: Benchmark (`tools/loxc_bench.c`)
- FAZA 10: CLI + dokumentacia (`tools/loxc_cli.c`, rozsireny `README.md`)

### Poznamka
- V tomto Windows prostredi (Codex CLI) neviem spustit WSL build priamo, ale v tvojej WSL konzole to spustis cez `make test`.
