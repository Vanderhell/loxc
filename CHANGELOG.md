# Changelog

All notable changes to loxc will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- (future features)

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
