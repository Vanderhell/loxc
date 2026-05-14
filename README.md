# loxc

Frekvenčne-optimalizovaný kompresor textu s hierarchickými maticami a runtime-loadable slovníkmi.

## Quick start

Build:

```sh
make
```

Trénuj vlastný modul:

```sh
./tools/loxc_train \
    --input mydata.txt --input more.txt \
    --output modules/loxc_mytable \
    --module-name mytable --module-id 10
```

Vygeneruje:

```text
modules/loxc_mytable.h        # C header
modules/loxc_mytable.c        # C source pre embedded
modules/loxc_mytable.loxctab  # binárny portable formát
```

Komprimuj s externou tabuľkou:

```sh
./tools/loxc_cli compress \
    --table modules/loxc_mytable.loxctab \
    input.txt output.loxc
```

Komprimuj so vstavanou tabuľkou (self-contained):

```sh
./tools/loxc_cli compress \
    --table modules/loxc_mytable.loxctab --embed \
    input.txt output.loxc
```

Dekomprimuj:

```sh
# Embedded súbor:
./tools/loxc_cli decompress output.loxc restored.txt

# External súbor (musí mať tabuľku):
./tools/loxc_cli decompress \
    --table modules/loxc_mytable.loxctab \
    output.loxc restored.txt
```

Inšpekcia:

```sh
./tools/loxc_cli info output.loxc
```

## Architecture

### Tréning

`loxc_train` analyzuje vstupné dáta, extrahuje frekvencie 1-gramov a n-gramov, aplikuje greedy filter na slovník (vyberá len entries ktoré globálne znižujú output size), a vyberie optimálnu stratégiu kódovania.

### Stratégie

- `FLAT_FIXED_WIDTH`: `ceil(log2(N))` bitov na symbol, pre malé abecedy alebo rovnomerné distribúcie
- `HIERARCHICAL_8`: 8×8 matice s ESCAPE, 6 bitov/úroveň, vhodné pre stredné abecedy so špicatou distribúciou
- `HIERARCHICAL_4`: 4×4 matice s ESCAPE, 4 bity/úroveň, vhodné pre malé abecedy

Selector vyberá automaticky podľa skutočných dát.

### Tabuľka v `.loxctab`

Binárny portable formát:

```text
Magic "LOXT" + version + strategy params
byte_to_symbol[256]
symbols[N] (type + index)
dict_offsets + dict_data
CRC32
```

Možno načítať za behu cez `loxc_module_load_from_file()`.

### `.loxc` súbor

Dva režimy:

- External: header + komprimované dáta. Dekodér potrebuje rovnakú tabuľku.
- Embedded: header + celá tabuľka + komprimované dáta. Self-contained, väčší ale prenosný.

## Benchmark

Detaily: [BENCHMARKS.md](BENCHMARKS.md)

Anglický korpus (Pride and Prejudice, 738 KB):

- External mode: 449 KB (60.8% pôvodnej veľkosti)
- Encode: ~110 ms
- Decode: ~13 ms (8× rýchlejší ako encode)

Loxc nie je primárne optimalizovaný na kompresný pomer, ale na **rýchle dekódovanie** cez lookup tabuľky.

## Library API

```c
#include "loxc.h"
#include "loxc_tab.h"

// Register modul (cez load_from_file alebo hardcoded register fn)
loxc_module_t *m = loxc_module_load_from_file("mytable.loxctab");
loxc_module_register(m);

// Compress
uint8_t out[N];
size_t cap = sizeof(out), actual;
loxc_compress("mytable", input, input_len, out, &cap, &actual);

// Decompress
char restored[M];
size_t rcap = sizeof(restored), ractual;
loxc_decompress(out, actual, restored, &rcap, &ractual);

// Cleanup
loxc_module_unload(m);
```

## Limitations

- Jeden runtime-loaded modul naraz (globálny stav)
- Modul je doménovo špecifický — trénuj na podobných dátach ako budeš komprimovať
- Žiadne Huffman / aritmetické kódovanie — kompresný pomer je horší ako gzip ale rýchlosť dekódovania vyššia

## Future work

- Viacero runtime modulov paralelne
- Huffman strategy
- Kontextové kódovanie `P(Y|X)`
- Streaming API pre súbory > RAM

## Build / Tests

```sh
make           # build libloxc.a + tools
make test      # run all tests (10 test suites)
```

## File structure

```text
include/    public headers
src/        library implementation
tools/      loxc_train, loxc_cli, loxc_bench
tests/      unit tests
modules/    generated modules (auto-generated, git-ignored)
benchmarks/ benchmark inputs
trainings/  training data
```
