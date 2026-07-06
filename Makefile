CC      ?= cc
AR      ?= ar
RANLIB  ?= ranlib

CPPFLAGS ?=
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
LDFLAGS ?=

PROJECT_CPPFLAGS := -Iinclude
MODULE_CPPFLAGS := $(PROJECT_CPPFLAGS) -Imodules

LIB      := libloxc.a
LIB_EMBEDDED := libloxc_embedded.a

CORE_OBJS := \
	src/loxc.o \
	src/loxc_base.o \
	src/loxc_plain.o \
	src/loxc_matrix.o \
	src/loxc_dict.o \
	src/loxc_stream.o \
	src/loxc_strategy.o \
	src/loxc_hier.o

HOST_OBJS := \
	src/loxc_loader.o \
	src/loxc_simple.o

SRC_OBJS := \
	$(CORE_OBJS) \
	$(HOST_OBJS)

TOOLS := \
	tools/loxc_train \
	tools/loxc_cli \
	tools/loxc_bench

FUZZERS := \
	fuzz/loxc_tab_fuzz

EXAMPLES := \
	examples/01_hello_world \
	examples/02_compress_file \
	examples/03_embedded_mode \
	examples/04_error_handling \
	examples/05_training_pipeline \
	examples/06_compare_modes \
	examples/07_streaming_chunks

TESTS := \
	tests/test_basic \
	tests/test_matrix \
	tests/test_dict \
	tests/test_stream \
	tests/test_header \
	tests/test_plain \
	tests/test_strategy \
	tests/test_hier \
	tests/test_registry \
	tests/test_simple \
	tests/test_loader \
	tests/test_train_demo

.PHONY: all test bench examples fuzz clean host embedded

all: $(LIB) $(LIB_EMBEDDED) $(TOOLS) $(TESTS)

$(LIB): $(SRC_OBJS)
	$(AR) rcs $@ $^
	-$(RANLIB) $@

$(LIB_EMBEDDED): $(CORE_OBJS)
	$(AR) rcs $@ $^
	-$(RANLIB) $@

host: $(LIB)

embedded: $(LIB_EMBEDDED)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -c -o $@ $<

tools/%: tools/%.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

fuzz/%: fuzz/%.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -DLOXC_FUZZ_STANDALONE -o $@ $< $(LIB) $(LDFLAGS)

tools/loxc_bench: tools/loxc_bench.c $(LIB) modules/loxc_demo.o
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -DLOXC_BENCH_WITH_DEMO -o $@ $< modules/loxc_demo.o $(LIB) $(LDFLAGS)

tests/%: tests/%.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

examples/%: examples/%.c $(LIB) modules/loxc_demo.loxctab
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

modules/loxc_demo.c modules/loxc_demo.h modules/loxc_demo.loxctab: tools/loxc_train trainings/demo_corpus.txt
	./tools/loxc_train --input trainings/demo_corpus.txt --output modules/loxc_demo --module-name demo --module-id 10

modules/loxc_demo.o: modules/loxc_demo.c src/loxc_stream.o src/loxc_base.o
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -c -o $@ modules/loxc_demo.c

tests/test_train_demo: tests/test_train_demo.c $(LIB) modules/loxc_demo.o
	$(CC) $(CPPFLAGS) $(MODULE_CPPFLAGS) $(CFLAGS) -o $@ $< modules/loxc_demo.o $(LIB) $(LDFLAGS)

test: $(TESTS)
	@tests/test_basic
	@tests/test_matrix
	@tests/test_dict
	@tests/test_stream
	@tests/test_header
	@tests/test_plain
	@tests/test_strategy
	@tests/test_hier
	@tests/test_registry
	@tests/test_simple
	@tests/test_loader
	@tests/test_train_demo

bench: tools/loxc_bench
	@tools/loxc_bench

fuzz: $(FUZZERS)

tools/loxc_bench2: tools/loxc_bench2.c $(LIB)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS) -lm

.PHONY: bench2 bench-full bench-clean

bench2: tools/loxc_bench2 modules/loxc_demo.loxctab
	@mkdir -p bench_out
	@if [ ! -s benchmarks/suite.list ]; then \
	  printf '%s\n' \
	    'trainings/demo_corpus.txt' \
	    'benchmarks/plain_sample_text.txt' \
	    'benchmarks/tiny.txt' 'benchmarks/small.txt' \
	    'benchmarks/medium.txt' 'benchmarks/source.c' \
	    'benchmarks/data.json' \
	    > benchmarks/suite.list; \
	fi
	./tools/loxc_bench2 \
	  --table modules/loxc_demo.loxctab \
	  --suite benchmarks/suite.list \
	  --iterations $(ITER) --warmup 3 \
	  --csv  bench_out/loxc_demo.csv \
	  --json bench_out/loxc_demo.json

bench-full: tools/loxc_bench2 tools/loxc_train
	./tools/bench_run.sh $(ITER)

bench-clean:
	rm -rf bench_out
	rm -f benchmarks/suite.list benchmarks/plain_sample_text.txt
	rm -rf benchmarks/corpora trainings/extra
	rm -f modules/loxc_json.* modules/loxc_logs.* modules/loxc_csrc.*

examples: $(EXAMPLES)

clean:
	$(RM) $(SRC_OBJS) $(LIB) $(LIB_EMBEDDED) $(TOOLS) $(TESTS) $(EXAMPLES) $(FUZZERS) modules/loxc_demo.o tools/loxc_bench2
