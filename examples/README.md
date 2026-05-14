# Examples

This directory contains small, buildable examples that show the library in
practice.

## 01_basic_compress

Loads a runtime table, compresses text, decompresses it, and verifies the
round-trip.

## 02_embedded_mode

Builds a self-contained `.loxc` file with an embedded table and then decompresses
it without passing an external module.

## 03_train_and_use

Shows an end-to-end workflow using a trained module to compress its own source
text.
