# Changelog

## [0.1.0] - 2026-05-14

### Added
- Frequency-optimized matrix compression
- 3 strategies: FLAT, HIERARCHICAL_4, HIERARCHICAL_8
- Automatic strategy selection based on data cost analysis
- Greedy global dictionary filter
- Multi-source training (`loxc_train --input X --input Y ...`)
- Portable `.loxctab` binary format
- Runtime module loading (`loxc_module_load_from_file`)
- External and embedded `.loxc` file modes
- CLI tools for `compress`, `decompress`, and `info`
- 10 test suites
- Benchmarks with about 60% compression on English text

### Known limitations
- Only one runtime-loaded module can be active at a time
- No Huffman or context-aware coding yet
