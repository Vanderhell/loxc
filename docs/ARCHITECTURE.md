# Architecture

`loxc` is a small text compressor built around lookup-table encoding. It
belongs to the Liquid Oxygen (LOX) ecosystem, and LOX means Liquid Oxygen.
The project favors fast decode paths, simple data structures, and generated
modules that can be embedded at build time or loaded at runtime.

## Stream layer

The stream layer provides bit-level readers and writers on top of byte arrays.
Everything above it is built in terms of:

- `loxc_writer_t` for emitting packed bits
- `loxc_reader_t` for consuming packed bits
- `loxc_write_bits()` and `loxc_read_bits()` as the basic primitives

This keeps the encoder and decoder independent from the final container format.

## Strategy selection

`loxc_train` measures several candidate encodings and picks the cheapest one:

- `FLAT_FIXED_WIDTH`
- `HIERARCHICAL_8`
- `HIERARCHICAL_4`

The selector compares estimated bit cost using the actual symbol distribution.
The chosen strategy is the one that minimizes total encoded size for the
training corpus.

## Hierarchical encoding

Hierarchical strategies model the symbol space as a sequence of fixed-width
levels. Each level uses an escape value to continue to the next matrix.

```
level 0  ->  [direct symbols ... | ESCAPE]
level 1  ->  [direct symbols ... | ESCAPE]
level 2  ->  [direct symbols ... | ESCAPE]
```

The direct slots are encoded with 4-bit or 6-bit chunks depending on the
selected strategy. This gives compact representation for common symbols while
still supporting larger symbol tables.

## Greedy dictionary filter

`loxc_train` first collects candidate substrings, then evaluates whether each
dictionary entry reduces output size globally.

The filter accepts only entries with positive net gain after accounting for:

- symbol table overhead
- dictionary metadata
- actual usage frequency

This keeps the final module small and avoids dictionary bloat.

## File formats

- `.loxctab` is the portable runtime table format.
- `.loxc` is the compressed container format.

Both formats are byte-packed and explicitly little-endian. They do not depend on
C struct layout or platform alignment.

## Module registry

The registry maps module names to `loxc_module_t` instances. Compression and
decompression look up the module by name unless the `.loxc` file contains an
embedded table.

Runtime-loaded modules are represented with a private context object that owns
the decoded table data. The wrapper API keeps that ownership explicit: the
caller owns the context handle and must close it, while buffers returned from
the simple API must be freed with `loxc_buffer_free()`.

## Data flow

```
training text
   -> loxc_train
      -> .h / .c / .loxctab
      -> module registry or runtime loader
         -> loxc_compress / loxc_decompress
```

## Design goals

- fast decode
- small runtime footprint
- portable runtime-loaded tables
- generated modules for embedded deployments
- simple file formats that can be inspected and debugged with standard tools
