# Changelog

All notable changes to loxc will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- (future features)

## [0.2.0] - 2026-05-14

### Added
- Comprehensive benchmark suite comparing loxc against gzip, zstd, lz4
- Multi-corpus benchmarks (small text, large text, repetitive logs, random)
- Statistical benchmark reporting (min/median/max from 5 runs, MB/s throughput)
- Hardware and software environment documentation in benchmarks
- Language-agnostic positioning in all documentation
- "Language agnostic" section in README emphasizing universal byte-level operation
- CI matrix testing across gcc and clang on Ubuntu and macOS
- Automated valgrind memory safety verification in CI
- Automated GitHub release creation on version tags
- Release artifacts: source tarballs in tar.gz, tar.bz2, and zip formats

### Changed
- README redesigned with hero shot, 3-line example, benchmark comparison table
- README architecture diagram in ASCII art
- Examples list moved into README as scannable table
- BENCHMARKS.md regenerated from real comparative measurements
- Documentation purged of language-specific assumptions (no more "English text")
- Demo module repositioned as "incidental training sample" not "English module"

### Fixed
- README "Quick start" now contains actual runnable code, not just links
- README "Examples" section now lists what each example demonstrates
- info command displays "LOXC" magic correctly (was "LXC")
- info command shows "not used" for CRC when flag is not set (was "0x00000000")

## [0.1.0] - 2026-05-14

### Added
- Frequency-optimized matrix compression
- 3 strategies: FLAT, HIERARCHICAL_4, HIERARCHICAL_8
- Automatic strategy selector based on cost analysis
- Greedy global dictionary filter
- Multi-source training (`loxc_train --input X --input Y ...`)
- Portable `.loxctab` binary format
- Runtime module loading (`loxc_module_load_from_file`)
- External and embedded `.loxc` file modes
- Simple wrapper API (`loxc_simple.h`)
- CLI tool (`loxc compress/decompress/info`)
- 12 test suites
- 7 working examples

### Known limitations
- One runtime module at a time (global state)
- No Huffman / context-aware encoding yet
